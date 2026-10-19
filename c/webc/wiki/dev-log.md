<!-- Log starts 2026-10-15. Entries before that date were a different project's
     (mcme) and were removed from this wiki by request on 2026-10-15; they are
     still recoverable from git history. Nothing is back-filled here. -->
# Dev log

Newest entry at the top. Append; do not rewrite history.

Status vocabulary:

**scaffold** = files exist, not functional · **wip** = being written ·
**works** = runs, may lack tests · **done** = runs and covered by tests ·
**blocked** = needs something first

---

## 2026-10-15 — Definition pass, first full test run, and a database this code refuses to open

### Built

Documentation only; no source file under `src/`, `display/`, `migrations/` or
`test/` was touched.

- **`README.md`** — was 6 bytes. Now defines the project: what it is,
  requirements, quick start, CLI, environment, dialect selection, TLS, route
  table, project layout, request flow, tests, and pointers into the wiki.
- **`wiki/`** — was 1,200+ lines about *`mcme`*, a Minecraft world editor
  (`simplexity.md`, `decisions.md` D1/D2 on C11+GTK4, `dev-log.md`, and a
  914-line `cli.md` documenting `mcme <verb>`). That content existed nowhere
  else in the repo; it was overwritten on request and remains recoverable from
  git history. Rewritten for webc:
  [README](README.md) (index), [simplexity](simplexity.md),
  [architecture](architecture.md), [templates](templates.md),
  [database](database.md), [testing](testing.md), [cli](cli.md),
  [decisions](decisions.md), and this log.

Measured while writing: `src/` 5,484 lines (`.c`+`.h`), `display/*.tt` 1,366,
`test/` 1,167, `js/` 730, `nob.c` 390.

### Verified — build and tests

| | |
|---|---|
| `./build.sh` | **exit 0**, `build/bin/webc` ~8.36 MB |
| warnings/errors in full build | **0** |
| `./test/run.sh` | **exit 0 — 140 passed, 0 failed** |
| └ `http_test.py` (SQLite) | 94 passed, 0 failed |
| └ `mysql_test.py` (own `mariadbd`) | 40 passed, 0 failed |
| └ `https_test.py` (own cert) | 6 passed, 0 failed |

Both optional suites **actually ran** — `mariadbd` and `openssl` are both
installed, so neither skipped. No `FAIL:` lines in the log. This is the first
recorded full-suite result for this tree; treat it as the baseline.

### Discovered — `~/.sqlite3/webc/db` cannot be opened  ·  status: **blocked on a decision**

The app does not run against its own default database.

```
$ ./build/bin/webc serve 8123
ERROR: /home/playme/.sqlite3/webc/db: Database scheme is too new. Contains
more migrations applied than expected. Update your application.
ERROR: Failed to initialize DB pool connection 0
Listening to http://127.0.0.1:8123/ (coroutine mode)

GET /        → 200, 29018 bytes   (dashboard needs no DB)
GET /notes   → 500,  257 bytes    (needs DB)
```

The log repeats `Database scheme is too new` once per DB request, and the
`DB pool initialized with 8 connections` line never appears.
`db_pool_init()` returns at connection 0 **without** setting
`db_pool_initialized`, so `db_pool_get()` re-attempts and fails every time —
hence 500s rather than one clean refusal at startup.

**Root cause: 9 migrations applied, 2 registered.** `migrations[]` in
`src/db/db.c` and the `migrations/` directory both contain exactly
`0001_notes` and `0002_users`; the database holds 9 rows in `Migrations`,
applied 2026-10-04 → 2026-10-07:

| # | Migration (from its SQL header) | SQL size |
|---|---|---|
| 1 | `0001_notes` | 510 B |
| 2 | `0002_users` | 530 B |
| 3 | `SQLITE3 POS + E-SHOP SCHEMA` | 37,281 B |
| 4 | `0004_seed_pos` — demo/seed data | 25,531 B |
| 5 | `0005_auth` — staff login sessions | 2,597 B |
| 6 | `0006_i18n` — storefront/log | 8,438 B |
| 7 | `0007_users_contact` — contacts | 6,407 B |
| 8 | `0008_role_workspace` | 1,478 B |
| 9 | `0009_geo_admin` — Phase 16 | **2,449,513 B** |

The database therefore has **34 tables** — `orders`, `order_items`, `payments`,
`products`, `customers`, `roles`, `permissions`, `audit_logs`, `stock_ledger`,
`financial_ledgers`, `provinces`/`districts`/`communes`/`villages`,
`translations`, `languages`, … — a POS/e-shop schema, not the notes/users
schema this tree declares.

### Evidence — these migrations were never committed

```
$ ls migrations/
0001_notes  0002_users

$ git ls-files c/webc/migrations/      → same 6 files, 0001 + 0002 only
$ git status -- c/webc/migrations/     → empty (no deletions reported)

$ git log -S'0003_' -- c/webc/src/db/db.c
  → empty: db.c never contained them in any commit

$ git show HEAD:c/webc/src/db/db.c | grep -c '"[0-9]\{4\}_'
  → 2   (identical to the working copy — db.c is not locally modified)
```

So a `migrations[]` with 9 entries and 7 untracked migration directories
existed on disk between 2026-10-04 and 2026-10-07, migrated the database, and
were then removed **without ever being committed**. Nothing in git can restore
them.

Corroborating:

- Commit `03882263 "pos database madness AI don't understand data"` and the
  migration names themselves point at the POS/e-shop work.
- `migrations/pos_and_eshop_db_schema.sql` was **deleted** by
  `ce80dedb "Try to compress webc filestructure for more compress"`. Git still
  has it at `ce80dedb^` — **30,890 bytes**, which does *not* match DB row 3's
  37,281 — so git's copy is an older variant and the database's own record is
  the authoritative one.
- `~/.sqlite3/webc/db.bak` (430,080 B, Oct 4) with `-shm`/`-wal` (Oct 7) — the
  D5 backup/restore path has run before.

### Options — none chosen

**A. Reconstruct migrations 0003–0009 from the database itself.**
D5 exists precisely for this: `Migrations.query` stores the **exact SQL that
was applied**, so the seven missing files can be dumped straight out
(`SELECT query FROM Migrations WHERE rowid BETWEEN 3 AND 9`) and
`migrations[]` re-extended. The database opens as-is with all 34 tables and
its data intact.

- Caveat: only the **`sqlite3.sql`** siblings are recoverable. The `mysql.sql`
  and `postgres.sql` variants of 0003–0009 are gone entirely.
- Caveat: names for 0004–0009 are legible in their SQL headers; row 3's array
  name is not recoverable and would have to be chosen.
- Decision required: **should this tree carry a POS/e-shop schema at all?**
  That is a product question, not a technical one.

**B. Set the POS database aside and start fresh.**
Point `WEBC_DB` elsewhere, or rename `~/.sqlite3/webc/db`. The app works
immediately on a clean notes/users schema — which is exactly what the test
suite already proves happens (140 passing tests all run against a fresh DB).
The 34-table database stays on disk untouched.

**C. Ship as-is.** Not viable: any DB-backed route returns 500.

Recommendation: decide the product question first — *is the POS schema part of
webc?* If yes → A (it is fully recoverable, thanks to D5). If no → B, and keep
the file where it is until the question is settled.

### Known gaps

All verified today; each is documented where it lives.

| # | Gap | Where |
|---|---|---|
| 1 | **Database refuses to open — DB routes 500** | above; [decisions.md](decisions.md) D9 |
| 2 | `src/crud/` compiles but has **zero callers** (live path is `SERVE_*` + `master_child.c`) | [architecture.md](architecture.md) |
| 3 | `driver_postgres.c` is an honest stub; `postgres:` fails at `db_open()` | [database.md](database.md), D4 |
| 4 | `dev` watches two dead paths: `core` and `nob.c` — **editing `nob.c` does not trigger a rebuild** | [cli.md](cli.md) |
| 5 | `dev_visit_file` excludes `module/sqlite-amalgamation-…`, now at `third_party/` — vendored SQLite retrigger builds again | [cli.md](cli.md) |
| 6 | Port argument is `atoi()` with no validation: `serve notanumber` → port 0, `serve 70000` → 4464 | [cli.md](cli.md) |
| 7 | DB pool round-robins with **no checkout tracking** — invariant rests on handlers not yielding while holding a connection | [database.md](database.md) |
| 8 | `tt` prints `char` with `%02x` → 8-digit escapes for bytes ≥ 0x80; bytes correct, build warning-free, but ugly | [templates.md](templates.md) |
| 9 | No `-std=` pinned; compiles as C23 on GCC 16 | [decisions.md](decisions.md) D12 |
| 10 | No concurrency test, no keep-alive/chunked (neither implemented), no Postgres suite | [testing.md](testing.md) |

### Next

1. **Decide A vs B for the database.** Everything else can wait; this is the
   only gap a user hits on their first `serve`.
2. If B: move the DB aside and re-run `serve` → confirm `GET /notes` → 200.
3. If A: dump rows 3–9 to `migrations/000{3..9}_*/sqlite3.sql`, extend
   `migrations[]`, rebuild, re-run the suite **and** the real-DB smoke test.
4. Cheap follow-ups once the above is settled: gaps 4, 5 and 6 are each a
   one-line fix in `src/webc.c`.
