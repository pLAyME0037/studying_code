# Database

How three SQL dialects share one schema, one pool and one migration history.

## Choosing a dialect

The dialect is read from `WEBC_DB`, not from a config file. `webc_db_lang()`
(`src/db/db.c:444`) is three lines:

```c
if (strncmp(WEBC_DB_PATH, "mysql:", 6) == 0)     return SQL_MYSQL;
if (strncmp(WEBC_DB_PATH, "postgres:", 9) == 0)  return SQL_POSTGRES;
return SQL_SQLITE;                               // anything else is a file path
```

`webc_db_dsn()` then hands the driver the string with the prefix stripped, so
the driver never sees the selector.

| `WEBC_DB` | Dialect | Driver |
|---|---|---|
| unset → `~/.sqlite3/webc/db` | SQLite | bundled amalgamation (3.46.1) |
| `/tmp/x.db` | SQLite | bundled amalgamation |
| `mysql:user=root;password=;database=webc;socket=/run/mysqld/mysqld.sock` | MySQL/MariaDB | libmariadb |
| `postgres:...` | PostgreSQL | **stub — fails with a clear message** |

`WEBC_DB`'s parent directory becomes `WEBC_DIR_PATH`, which is where the
runtime creates what it needs (including the recursive `nested/deep/db` the
test harness deliberately exercises).

### Why SQLite is the default

It needs no server, no credentials and no daemon, so a clone builds and runs
with nothing configured. Everything else is opt-in through one variable.

### Postgres: honest stub

`src/db/driver_postgres.c` is a complete `Sql_Driver` vtable in which every
function fails:

```c
static bool pg_drv_open(db_t *db, const char *dsn) {
    (void) dsn;
    if (db) snprintf(db->err, sizeof(db->err), "postgres driver is not implemented yet");
    return false;
}
```

The dialect is **first-class everywhere else** — `q[]` query tables have a
`[SQL_POSTGRES]` entry, `migrations_table_sql[SQL_POSTGRES]` exists, every
migration directory has a `postgres.sql`. Only the wire protocol is missing.

This is deliberate, and the alternative was worse: dropping Postgres from the
schema layer would mean rewriting the whole query table when it is implemented,
and pretending it connects would fail somewhere obscure. Failing in
`db_open()` with a sentence naming the problem is the cheapest possible
mistake. See [decisions.md](decisions.md) D4.

## The driver interface — `src/db/sql.h`

One vtable, four functions, plus column access:

```c
open / close / prepare / step / finalize / column_text
```

`sql.h` gives callers a dialect-neutral statement API — `db_t`, `sql_stmt`,
`sql_step()` returning `SQL_ROW`/`SQL_DONE`/`SQL_ERROR` — so feature code never
branches on dialect for reads. Dialect differences are confined to:

- `driver_*.c`
- `migrations_table_sql[]`
- `migrations/<name>/{sqlite3,mysql,postgres}.sql`
- the `[SQL_*]` entries in per-file query tables

| Driver | Lines | Notes |
|---|---|---|
| `driver_sqlite.c` | 172 | bundled, `third_party/sqlite-amalgamation-3460100` |
| `driver_mysql.c` | 481 | libmariadb, found via `pkg-config` at build time |
| `driver_postgres.c` | 101 | stub |

## The pool

```c
#define DB_POOL_SIZE 8          // src/db/db.h:27
db_t *db_pool_get(void);        // round-robins db_pool_index over the array
void  db_pool_put(db_t *db);    // release, does not close
void  db_pool_init(void);       // opens all 8 up front, prints the count
```

`db_pool_init()` runs inside `coroutine_server_run()` before `listen()`, so a
bad DSN fails at startup rather than on the first request. It is idempotent,
and `db_pool_get()` will initialise it if called first.

`db_pool_get()` picks the next index round-robin; it does not wait for a free
slot. With `DB_POOL_SIZE` 8 and one coroutine per connection, 9 concurrent
requests that all hold a connection at the same moment can hand out a `NULL`.
Handlers must handle `db_pool_get() == NULL`.

> **Round-robin, not checkout-tracking.** Nothing marks a slot as in-use, so
> two connections can be handed the same `db_t*`. That is safe today because a
> coroutine does not yield while holding a connection in the current handlers —
> but it is an invariant, not something the type system enforces. If a handler
> ever awaits between `db_pool_get()` and `db_pool_put()`, this must become a
> real checkout list. Logged in [dev-log.md](dev-log.md).

## Migrations

### Layout

```
migrations/
  0001_notes/
    sqlite3.sql    mysql.sql    postgres.sql
  0002_users/
    sqlite3.sql    mysql.sql    postgres.sql
```

Numbered directory = migration name; three dialect files inside. Registered
in a **compile-time array** in `db.c`, not discovered at runtime:

```c
static const Migration migrations[] = {
    { "0001_notes", { [SQL_SQLITE] = "migrations/0001_notes/sqlite3.sql",
                      [SQL_MYSQL]  = "migrations/0001_notes/mysql.sql",
                      [SQL_POSTGRES] = "..." } },
    { "0002_users", { ... } },
};
```

A migration that exists on disk but is not in `migrations[]` **does not run**.
Adding a migration means adding the directory *and* an array entry.

### The history table

```sql
CREATE TABLE IF NOT EXISTS Migrations (
    applied_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    query      TEXT NOT NULL
);
```

(`TIMESTAMP` instead of `DATETIME` on Postgres — this is why the DDL is a
per-dialect array rather than one string.)

`query` stores the **exact text of the migration that was applied**, not a
version number. On every open the code compares each stored string against
the current file, byte for byte.

Why not a version number: it cannot detect that an already-applied migration
was edited. Storing the text turns "someone rewrote history" from silent data
corruption into a detectable state.

### The open sequence

`create_schema_status()`:

```
migration_files_load(lang)     -- read the three files, cache per dialect
sql_txn_begin()
exec migrations_table_sql[]    -- ensure Migrations exists
SELECT query FROM Migrations
compare stored vs current files, in order
  more in DB than in code   → ERROR "Database scheme is too new..."
  a stored string differs   → SCHEMA_HISTORY_MISMATCH
  pending                   → apply them all now
sql_txn_commit()
```

**Everything runs in one transaction.** Two consequences, both encoded as
comments in `db.c`:

1. Migration files must not contain `PRAGMA` statements — SQLite rejects
   `journal_mode`/`synchronous` changes inside a transaction. PRAGMAs run in
   `open_webc_db()` *before* the transaction starts.
2. A failure commits nothing; there is no partially-migrated state.

Set `WEBC_TRACE_MIGRATION_QUERIES=1` to print each migration's SQL as it
applies.

### When history mismatches: rebuild, keep the data

```c
if (status == SCHEMA_HISTORY_MISMATCH && attempt == 0) {
    db_backup_and_reset();          // db → db.bak (+ wal/shm), close first
    // fresh db created and migrated from migrations[]
    db_restore_from_backup(...)     // copy shared tables/columns
}
```

The recipe, verbatim from the comment at `db.c:206`:

1. `db` → `db.bak` (old file, untouched)
2. fresh db is created and migrated from `migrations[]`
3. every table/column **both** databases share is copied from `db.bak` into the
   new schema

What it tolerates, each logged as `INFO:`:

- backup table with no counterpart → skipped
- table sharing no columns → skipped
- more than `MAX_TABLE_COLS` columns, or more tables than the array holds →
  refused with an explicit error rather than a partial copy

What it does **not** cover:

- **SQLite only.** `db_restore_from_backup()` takes a `sqlite3*`, so a
  history mismatch on MySQL/MariaDB is not auto-repaired.
- `db.bak` is overwritten on the next mismatch — keep a copy if it matters.

### Migration ordering note

`0001_notes` deliberately has **no `REFERENCES` clause** on `notes.user_id`:
the `users` table does not exist until `0002_users`. The comment in the file
says so. Cross-table integrity between the two is therefore not enforced by
the schema.

### IDs

```sql
id VARCHAR(36) PRIMARY KEY DEFAULT (UUID())          -- sqlite/mysql
id TEXT PRIMARY KEY DEFAULT (gen_random_uuid()::text) -- postgres
```

IDs are **text**, not integers. `parse_uri_id()` in `route.c` tags an
all-digits segment as `ID_INT` so a legacy integer-keyed table still routes;
everything else flows through as text. See [architecture.md](architecture.md).

### Golden files

`test/golden/` pins the `sqlite3.sql` files **byte for byte**. Existing
databases hold those exact strings in `Migrations.query`, so editing a
`sqlite3.sql` that has already shipped is a history mismatch for every user of
that database — which is precisely why the golden test exists. See
[testing.md](testing.md).

## Things that will bite you

| | |
|---|---|
| Editing a shipped migration file | history mismatch on every existing SQLite DB |
| Adding a file without a `migrations[]` entry | silently never applied |
| `PRAGMA` inside a migration file | fails — it runs inside a transaction |
| `WEBC_DB` with only a `mysql:` prefix change | the parent dir becomes the webc dir; nothing else moves |
| Pool exhaustion | `db_pool_get()` returns `NULL`, no blocking |
| Postgres | `db_open()` fails with "postgres driver is not implemented yet" |
