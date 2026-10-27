# webc

A small server-side web framework written in C. It compiles HTML templates
**into C functions at build time**, so a page renders as straight-line code
with no template engine, no interpreter and no allocation per tag.

```c
// display/user.h.tt  →  build/h_to_html/user.h  →  #include'd inside render_users_page()
<c> if (!fragment) { </c>
<form method="POST" action="/users/create" ...>
<c> } </c>
```

Out of the box it ships a working app: a dashboard, a paginated `/users` list
with create/edit/delete, a `/notes` CRUD set, a master-detail `/people` page
(users with their notes as child tabs), an `/api/notes` JSON API, and static
asset serving — behind a coroutine-based HTTP/1.1 server with optional TLS.

---

## Requirements

Linux, a C compiler, and `pkg-config`.

| | Needed for | Required? |
|---|---|---|
| `cc` + `pkg-config` | build | yes |
| MariaDB/MySQL client dev files (`libmariadb`) | MySQL driver | **yes** — build fails without it |
| OpenSSL (`libssl`, `libcrypto`) | TLS support | **yes** — always linked |
| `tailwindcss-linux-x64` on `PATH` | `css/output.css` | yes (build prints a download hint) |
| `python3` | test suites | for `test/run.sh` |
| `mariadbd`, `mariadb-install-db`, `mariadb` CLI | MySQL dialect suite | optional — suite skips itself |
| `openssl` CLI | HTTPS suite | optional — suite skips itself |

## Quick start

```bash
./build.sh                        # → build/bin/webc
./build/bin/webc serve            # http://127.0.0.1:8080/
./build/bin/webc serve 9000       # explicit port
./build/bin/webc dev              # rebuild + restart on source change
./test/run.sh                     # full test suite
```

`build.sh` is a thin wrapper around `./build/bin/nob`, the build driver. The
first run compiles `nob.c` into `build/bin/nob`; that binary then rebuilds
itself when `nob.c` changes (`NOB_GO_REBUILD_URSELF`).

## CLI

```
webc serve [port]     Start the web server. Default port 8080.
webc dev   [port]     Serve with auto rebuild + reload on source change.
webc help  [command]  Help; prefix match is enough (`webc help se` → serve).
webc version          Print the build time.
```

The default command is `help`, so a bare `./build/bin/webc` prints usage.
Full contract in [wiki/cli.md](wiki/cli.md).

## Configuration

Everything is environment-driven — there is no config file.

| Variable | Effect |
|---|---|
| `HOME` | **Required.** Used to locate `~/.sqlite3/webc/`. Exit 1 if unset. |
| `WEBC_DB` | Database location. Its parent directory becomes the "webc dir". |
| `WEBC_TLS_CERT` + `WEBC_TLS_KEY` | PEM paths; set **both** to enable TLS. Neither → plaintext. |
| `WEBC_TRACE_MIGRATION_QUERIES` | Print each migration's SQL as it applies. |

### Choosing a database

`WEBC_DB` doubles as the dialect selector — a `mysql:` or `postgres:` prefix
selects the driver, anything else is a SQLite file path:

```bash
WEBC_DB=/tmp/my.db                          # sqlite (the default)
WEBC_DB=$HOME/.sqlite3/webc/db              # sqlite, default location
WEBC_DB='mysql:user=root;password=;database=webc;socket=/run/mysqld/mysqld.sock'
WEBC_DB='postgres:...'                      # dialect understood, driver is a stub
```

The default is `~/.sqlite3/webc/db`. SQLite needs no setup, so that is the
path you get if you export nothing.

> **Postgres status:** migrations, queries and the driver slot are all
> first-class, but the wire protocol is not implemented — `db_open()` fails
> with `"postgres driver is not implemented yet"` rather than pretending to
> connect. SQLite and MySQL/MariaDB are the two working dialects.

### TLS

```bash
WEBC_TLS_CERT=cert.pem WEBC_TLS_KEY=key.pem ./build/bin/webc serve
# → Listening to https://127.0.0.1:8080/ (coroutine mode)
```

Setting only one of the two is a hard error, and a misconfigured TLS setup
aborts startup instead of silently falling back to plaintext. Minimum protocol
version is TLS 1.2.

## Routes

| Method | Path | Handler |
|---|---|---|
| GET | `/` | dashboard |
| GET | `/version` | build/version page |
| GET | `/people` | master-detail: users + their notes |
| GET | `/users` | paginated list |
| POST | `/users/create` | |
| GET | `/users/{id}/edit` | |
| POST | `/users/{id}/update` · `/users/{id}/delete` | |
| GET | `/notes` | paginated list |
| POST | `/notes/create` | |
| GET | `/notes/{id}/edit` | |
| POST | `/notes/{id}/update` · `/notes/{id}/delete` | |
| GET, POST, PUT, DELETE | `/api/notes` | JSON API (405 on other methods) |
| GET | `/css/*` `/js/*` `/resource/*` `/favicon.ico` | static |

`..` path segments anywhere in the URI are rejected with a 404.

## Project layout

```
display/            .tt templates — HTML with <c> C </c> islands
  component/          reusable fragments (pager, sidebar, sweet-alert, MD tabs)
  layout/             page shell (header/footer/dashboard)
src/
  webc.c              main: CLI, dev watcher, server bootstrap
  http/               serve.c (coroutine server + TLS), route.c, utils.c
  db/                 db.c (migrations, pool), sql.h, driver_*.c
  core/webc_template.h  the SERVE_* / PAGE_BEGIN macro layer
  layout/             header/footer/paging/master_child page engines
  crud/               declarative CRUD module API (see note below)
  notes/ user/ people/ dashboard/ version/   feature modules
  tt.c                the .tt → C template compiler
build/h_to_html/      generated from display/ — do not edit
migrations/NNNN_name/{sqlite3,mysql,postgres}.sql
js/ css/ resource/    front-end assets (also bundled into the binary)
third_party/          nob, sqlite amalgamation, coroutine
test/                 run.sh + python suites, test/golden/
wiki/                 project memory — decisions, dev log, design rules
```

Two directories are **generated** and must not be edited by hand:
`build/` and `build/h_to_html/`.

## How a request is served

```
accept() ──▶ coroutine_go(serve_request_coro, fd)
                │
                ├─ read headers (8 MB body cap)
                ├─ route_request()   exact | prefix | id-action
                ├─ handler()
                │     ├─ db_pool_get() ── migrations verified/applied on open
                │     ├─ SQL via the Sql_Driver vtable
                │     └─ render: PAGE_BEGIN → #include generated template → PAGE_END
                └─ http_render_response() → HTTP/1.1 + Content-Length
```

One coroutine per connection on non-blocking sockets, `listen()` backlog 80,
`SIGPIPE` ignored, HTTP/1.1 responses. Details in
[wiki/architecture.md](wiki/architecture.md).

## Tests

```bash
./test/run.sh
```

The runner builds, creates a **throwaway `$HOME`** so your real
`~/.sqlite3/webc/db` is never touched, picks a free port, spawns the server,
then runs three suites in parallel:

- `http_test.py` — main SQLite suite
- `mysql_test.py` — boots a throwaway `mariadbd`, verifies rows via the CLI
- `https_test.py` — self-signed cert, verifies TLS-only serving

All three skip themselves cleanly when their tooling is missing. Upload files
created during a run are recorded and removed on exit. See
[wiki/testing.md](wiki/testing.md).

## Notes

- `src/crud/` is compiled but currently has **no callers** — the live CRUD path
  is the `SERVE_*` macros in `src/core/webc_template.h` plus
  `src/layout/master_child.c`. Tracked in
  [wiki/dev-log.md](wiki/dev-log.md).
- Adding a `.c` file anywhere under `src/` compiles it automatically; the build
  globs the tree (excluding `tt.c` and `nob.c`).
- Do not `#include "*.c"` from a file that is also compiled on its own — it
  produces duplicate-symbol link errors. Include the matching `.h`.

## Further reading

The [wiki/](wiki/) directory is the project's memory — why the code is shaped
the way it is, not just what it does.

1. [wiki/simplexity.md](wiki/simplexity.md) — the rules the code is written under
2. [wiki/architecture.md](wiki/architecture.md) — request flow and source map
3. [wiki/templates.md](wiki/templates.md) — the `.tt` system
4. [wiki/database.md](wiki/database.md) — dialects, pool, migrations
5. [wiki/testing.md](wiki/testing.md) — the test harness
6. [wiki/cli.md](wiki/cli.md) — command-line contract
7. [wiki/decisions.md](wiki/decisions.md) — choices and rejected alternatives
8. [wiki/dev-log.md](wiki/dev-log.md) — where the project stands today
