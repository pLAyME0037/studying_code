# Decisions

One entry per architectural choice, with the alternatives that lost and why.
Check here before inventing a new pattern.

Convention: this records the decision **as the code embodies it**, with the
rationale that follows from the constraints in
[simplexity.md](simplexity.md). Where the code no longer honours its own
decision, that is said out loud.

---

## D1 — Templates compile to C; there is no template engine in the binary

**Decision.** `display/*.tt` is HTML with `<c> C </c>` islands. `src/tt.c` runs at
build time and emits `build/h_to_html/*.h` containing `OUT("\x..", N);` byte
literals interleaved with the C verbatim. Those headers are `#include`d
*inside* function bodies. See [templates.md](templates.md).

**Why.** The two standard alternatives both cost something real: interpreting
templates at runtime means parsing strings on every request and maintaining a
second language the compiler cannot check; writing HTML as C string literals
means unreadable pages and `\"` escaping everywhere. Compiling removes the
runtime cost entirely and makes the template a typed part of the program.

**Rejected.**

- *Runtime template engine.* Per-request parsing, a second language, and no
  compile-time check that the variables a template names actually exist.
- *HTML as C string literals.* Unwritable past a few lines.
- *A heavier preprocessor / macro DSL.* A second language again, plus the
  maintenance burden of a real compiler.

**Cost.** The generated header only makes sense inside its host function, so a
template cannot be read standalone. And a stale `build/h_to_html/` is
possible if you bypass `build.sh`.

---

## D2 — Coroutines, one per connection, no thread pool

**Decision.** `coroutine_server_run()` accepts on a non-blocking socket and
`coroutine_go(serve_request_coro, fd)` per connection. I/O that would block
returns `-1`/`-2` from `conn_read`/`conn_write`, which the caller turns into
`coroutine_sleep_read/write(fd)` and retries.

**Why.** The retry pattern is uniform and local: every handler is written as
if it were blocking, and the scheduler handles the yield. No handler shares
state with any other, so there is nothing to lock.

**Rejected.**

- *Thread per connection.* Stack memory per client plus locking around the
  DB pool, for no gain on a single-process app.
- *Thread pool.* Pool size becomes a hard concurrency limit and handlers must
  tolerate blocking the pool's threads.
- *fork per connection.* No shared connection pool, and `db_pool_init()` up
  front would be pointless.
- *epoll with an explicit state machine.* The classic scalable answer — and
  it turns every handler into a callback chain. Reads worse for the same
  scale.

**Cost.** Blocking calls inside a handler stall its coroutine (and only its
coroutine), but nothing catches that for you.

---

## D3 — A static route table with three match kinds

**Decision.** `route.c` builds one array at first use. Entries are
`ROUTE_EXACT`, `ROUTE_PREFIX`, or `ROUTE_ID_ACTION` (`/<prefix>/<id>/<suffix>`),
scanned linearly, with an optional method per entry (`NULL` = the handler
dispatches).

**Why.** Twenty-odd routes do not justify a trie or a regex engine, and a
linear scan over an array is the shortest code that cannot be wrong.
`ROUTE_ID_ACTION` centralises id extraction so handlers never re-parse the
URI, and `parse_uri_id()` classifies all-digits segments as `ID_INT` for
legacy integer keys.

**Rejected.**

- *Hand-written if/else chain per route.* Differs per route, so adding one
  means re-reading the whole chain to place it correctly.
- *Regex router.* Allocation and failure modes per request, and the
  backreference semantics are a common source of bugs.
- *Trie/prefix tree.* Real complexity for a table this size.

**Cost.** Linear scan — irrelevant at this size, but it is not a scaling
answer. Order matters and nothing enforces that a `ROUTE_PREFIX` is not
shadowed by an earlier exact route.

---

## D4 — Postgres is an honest stub while the dialect stays first-class

**Decision.** Every `[SQL_POSTGRES]` entry — query tables, DDL, migrations —
exists. `driver_postgres.c` is a complete `Sql_Driver` vtable whose every
function fails, and `pg_drv_open()` writes
`"postgres driver is not implemented yet"` into `db->err`.

**Why.** It fails at the single cheapest point — `db_open()` — with a sentence
that names the problem. Every alternative fails later and more expensively.

**Rejected.**

- *Drop Postgres from the schema layer until it is implemented.* Then
  implementing it later means rewriting every query table and every migration,
  because the dialect axis would have been removed.
- *Pretend to connect.* Fails somewhere obscure, likely with a misleading
  error, or worse returns wrong results.
- *Leave it half-wired without a message.* A silent `false` from `prepare()`
  with no text is the worst of both.

**Cost.** `WEBC_DB=postgres:...` looks supported in the docs table and is not.
Mitigated by saying so in [README.md](../README.md), [database.md](database.md)
and here.

---

## D5 — Migration history stores the exact SQL text

**Decision.** `Migrations(applied_at, query)` stores the full text of each
applied migration. On every open, stored text is compared with the current
files, byte for byte, and a difference is `SCHEMA_HISTORY_MISMATCH`.

**Why.** A version number cannot detect that an *already applied* migration was
edited — which is the actual way people break their databases. Storing the text
turns silent corruption into a detectable state, and lets the backup/restore
path (`db.bak` → fresh migrate → copy shared tables) run automatically.

**Rejected.**

- *Version number only.* Blind to edits; the app then runs against a schema
  nobody validated.
- *Hash of the file.* Detects a difference but tells you nothing about what
  differs — you cannot print `EXPECTED:` / `FOUND:` as the error does.
- *No history, just `CREATE TABLE IF NOT EXISTS`.* Cannot apply anything after
  the first version.

**Cost.** Editing a shipped migration file breaks every existing database —
which is exactly what `test/golden/` exists to prevent. See
[testing.md](testing.md).

---

## D6 — One translation unit per `.c` file; never unity-include a `.c`

**Decision.** `nob.c` globs every `.c` under `src/` (excluding `tt.c` and
`nob.c`) and compiles each once. Files are shared through `.h` only. Nothing
in `src/` may `#include "*.c"`.

**Why.** The glob means a new `.c` compiles automatically with no list to
update — that is the whole point of globbing. But a file that is *also*
included as text by another source is compiled twice, and the linker reports
`multiple definition of ...`.

**Rejected.**

- *Explicit source list in `nob.c`.* No duplicate-symbol hazard, but every new
  file needs a manual edit, and a forgotten entry fails silently as an
  undefined reference.
- *Unity builds.* Deliberately opposite: faster builds, but every symbol in
  the unity blob shares one TU, so `static` stops meaning file-scope and
  include order starts changing behaviour.

**This regression happened here.** `src/user/user.c` and `src/notes/notes.c`
carried four unity-includes of `.c` files, and the link failed with duplicate
`render_page_header` / `render_page_footer`. The fix was to revert those four
to the matching `.h` — restoring the decision, not working around it.

**Cost.** No compile-time merging; the glob's exclusion list must be kept
current when a file must not be compiled directly.

---

## D7 — `Connection: close` on every response

**Decision.** `http_render_response()` always writes
`Connection: close`, and `serve_request_coro` shuts down and closes after one
response.

**Why.** Keep-alive requires connection-reuse state in a scheduler that has
none: parsing pipelined requests, tracking which response belongs to which
request, idle timeouts, and half-closed sockets. With one response per
connection, teardown is unconditional and provable — the drain-then-close
sequence in the cleanup path exists for exactly one case.

**Rejected.**

- *Keep-alive (HTTP/1.1 default).* Real throughput win for a browser fetching
  many assets, at the cost of a state machine the scheduler does not have.
- *HTTP/1.0-style request-response with reuse negotiated.* Same problem.

**Cost.** A new TCP connection per asset. For a local/single-user app this is
noise; at scale it is the first thing to change — and the two places that must
change together are the response builder and the `cleanup` path
(see [architecture.md](architecture.md)).

---

## D8 — Static assets are embedded, with a disk fallback

**Decision.** `nob.c` bakes `css/output.css`, two images and the four JS files
into `build/bundle.h` as `bundle[]` + a `resources[]` table.
`serve_resource()` looks the path up there first, and only reads from disk if
it is not bundled.

**Why.** The binary runs standalone — no asset directory to ship, and no
"works in the repo, 404s when deployed" failure. The disk fallback covers what
cannot be known at build time, notably uploaded avatars.

**Rejected.**

- *Disk only.* The app then depends on its working directory, and a missing
  `css/output.css` at runtime is a silent blank page.
- *Bundle only.* Uploaded files cannot be bundled; would have forced a second
  serving path anyway.

**Cost.** A full rebuild is required to pick up a CSS or JS edit — which is
also why `dev_visit_file` explicitly excludes `css/output.css` from the
rebuild trigger, so a Tailwind output write does not immediately retrigger its
own build.

---

## D9 — Refuse to open a database whose schema is newer than the code

**Decision.** In `create_schema_status()`, if `Migrations` holds more rows than
`migrations[]` has entries:

```c
ERROR: <path>: Database scheme is too new. Contains more migrations applied
than expected. Update your application.
```

→ `SCHEMA_ERROR`, the open fails, `db_pool_init()` stops. The server still
listens; DB-backed routes return 500.

**Why.** The application genuinely does not know the schema it would be
running against — columns, tables and constraints exist that no query in this
binary was written for. Silently ignoring the unknown migrations would run
half the app against a schema it does not understand, and *that* failure mode
hides data damage. Failing loudly at open, with a message that says what to do
("update your application"), is the honest outcome. Note this is a different
path from D5's `SCHEMA_HISTORY_MISMATCH`, which **is** recoverable
automatically — an unknown *count* is not, because the code has no
corresponding expectation to restore against.

**Rejected.**

- *Ignore extra migrations and open anyway.* The code would issue queries
  against tables it never declared; failures would be scattered and misleading.
- *Truncate / roll back the history.* Destructive to a database the user may
  have real data in — and it would throw away information about what the
  schema actually is.
- *Best-effort downgrade.* No such thing exists for arbitrary DDL.

**Cost.** A database migrated by a newer or different checkout of this project
becomes unopenable by this one, and the failure surfaces as 500s on DB routes
rather than a refusal to start. **This is the live state of this checkout** —
9 migrations applied, 2 registered. Analysed in
[dev-log.md](dev-log.md).

---

## D10 — The migration registry is a compile-time array

**Decision.** `migrations[]` in `db.c` names each migration directory and its
three dialect files. Files on disk that are absent from the array never run.

**Why.** The set of applied migrations must be reproducible and reviewable in
the diff. A compile-time array also lets `migration_sql(index)` index straight
into it for the byte-for-byte comparison in D5, and gives `ARRAY_LEN(migrations)`
as the expected count.

**Rejected.**

- *Scan `migrations/` at runtime.* Order becomes filesystem-dependent unless
  the sort is careful, adding a migration is invisible in review, and the
  expected count becomes an artifact of the deploy rather than the binary.
- *Embedding the SQL as C strings.* What the code had before; D5's note in
  `db.c` records that `sqlite3.sql` files must still match those old inline
  strings byte for byte, because existing databases hold them.

**Cost.** Adding a migration is two steps — directory *and* array entry — and
forgetting the second means it silently never applies.

---

## D11 — SQLite is the default; the dialect is a prefix on `WEBC_DB`

**Decision.** `webc_db_lang()` looks only at `mysql:` / `postgres:` prefixes on
`WEBC_DB`; anything else is a SQLite file path. No config file exists.

**Why.** SQLite needs no server, credentials or daemon, so a clone builds and
runs with nothing configured — the smallest possible onboarding. One variable
selects both the dialect and the location, so there is exactly one thing to
set.

**Rejected.**

- *Config file.* A second source of truth to keep in sync with the
  environment, and a file that must exist before `help` can run.
- *`--dialect` flag plus `--db`.* Two arguments that must agree; `mysql:` with
  a file path is a combination that has no meaning.
- *MySQL default.* Requires a running daemon — the project would not run on a
  fresh clone at all.

**Cost.** No way to discover a dialect from the path alone, and the parent
directory of `WEBC_DB` silently becomes the "webc dir" that legacy-file logic
operates on.

---

## D12 — No `-std=` is pinned

**Recorded as-is.** The build invokes `cc` with `-Wall -Wextra -Wswitch-enum`
and no `-std=`, so the standard is whatever the compiler defaults to. On this
machine (GCC 16.2.1) that is `__STDC_VERSION__ 202311L` — C23.

**Why it reads like an omission, not a decision.** Nothing in the tree
justifies it, and `-Wswitch-enum` *is* pinned, so standards discipline exists
in a neighbouring line. Left as D12 rather than silently "fixed" because
pinning a standard changes what the compiler accepts and deserves its own
build-and-test run — the same reasoning as the `tt` escape note in
[templates.md](templates.md).

**Rejected (should it be pinned).**

- *`-std=c11`.* Portable, but would need auditing against what the code
  actually uses today.
- *`-std=gnu11`* / *`gnu17`.* Fixes a dialect while keeping GNU extensions,
  which the code does not obviously rely on.

**Cost.** The same tree compiles as different dialects on different compilers,
so "works on my machine" cannot be assumed for standard-dependent behaviour.
