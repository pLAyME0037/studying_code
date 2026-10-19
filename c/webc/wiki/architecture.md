# Architecture

How a request becomes bytes, and which file owns each step.

## The shape of a request

```
        ┌────────────────────────── src/webc.c ───────────────────────────┐
        │ main(): resolve $HOME, WEBC_DB, dispatch CLI verb               │
        │   serve ─▶ coroutine_server_run(addr, port)                     │
        └───────────────────────────────┬─────────────────────────────────┘
                                        ▼
                     ┌────────── src/http/serve.c ──────────┐
                     │ coroutine_init()                     │
                     │ tls_server_init()   ← abort if bad   │
                     │ db_pool_init()      ← 8 connections  │
                     │ socket/bind/listen(80), non-blocking │
                     └──────────────────┬───────────────────┘
                                        │ accept()
                                        ▼
                     coroutine_go(serve_request_coro, fd)
                                        │
       ┌────────────────────────────────┼────────────────────────────────┐
       │  1. TLS handshake (if tls_ctx) │  SSL_accept loop w/ co-sleep   │
       │  2. read headers until \r\n\r\n│  1 KB buffer, straddle-safe    │
       │  3. Content-Length > 8 MB?  ───┴──▶ 413, skip body              │
       │  4. read body → sc.body                                         │
       │  5. split status line → method, uri; split query at '?'         │
       │  6. route_request(&sc)                                          │
       │  7. write response, then close (Connection: close)              │
       └───────────────────────────────┬─────────────────────────────────┘
                                       ▼
                    ┌────────── src/http/route.c ───────────┐
                    │ linear scan of a static route table   │
                    │   ROUTE_EXACT      "/notes"           │
                    │   ROUTE_PREFIX     "/css/"            │
                    │   ROUTE_ID_ACTION  "/notes/{id}/edit" │
                    │ no match ─────────────────────▶ 404   │
                    └──────────────────┬────────────────────┘
                                       ▼
                     handler (feature module)
                       ├─ db_pool_get()
                       │    └─ open_webc_db(): verify migration history
                       │       byte-for-byte, apply pending, one transaction
                       ├─ SQL through the Sql_Driver vtable
                       └─ render:
                            PAGE_BEGIN(sc, title, route)
                            #include "build/h_to_html/..."   ← generated template
                            PAGE_END(sc)
                                        ▼
                     http_render_response(sc, 200, type, body)
                       HTTP/1.1 + Content-Type + Last-Modified
                       + Content-Length + Connection: close
```

## Startup — `src/webc.c`

`main()` does four things and then gets out of the way:

1. Requires `$HOME` — exits 1 with a message naming `~/.sqlite3/webc/` if unset.
2. Resolves `WEBC_DB` into `WEBC_DB_PATH` + `WEBC_DIR_PATH`; with no override
   these are `~/.sqlite3/webc/db` and its parent.
3. Reads `WEBC_TRACE_MIGRATION_QUERIES`.
4. Dispatches the CLI verb, defaulting to `help`.

`serve` calls `coroutine_server_run()` directly. `dev` is a separate loop: it
hashes mtimes over a set of source roots every second, and when the hash moves
it stops the server, re-runs `./build/bin/nob`, and relaunches. A failed build
does not kill the watcher — it prints `build failed waiting for the next
change...` and keeps polling.

## Server — `src/http/serve.c`

- **Concurrency model:** one coroutine per accepted connection on
  non-blocking sockets. I/O that would block returns `-1`/`-2` from `conn_read`
  / `conn_write`, which the caller turns into `coroutine_sleep_read(fd)` /
  `coroutine_sleep_write(fd)` and retries. No thread pool, no shared request
  state between connections.
- **Backlog** 80. **`SIGPIPE` ignored** (a client that vanishes mid-write must
  not kill the process).
- **TLS** is decided once at startup from `WEBC_TLS_CERT` + `WEBC_TLS_KEY`.
  A misconfigured pair aborts before `listen()` — `tls_server_init()` returning
  false stops the server rather than serving plaintext by accident. The
  handshake runs inside the per-connection coroutine, so a slow or non-TLS
  client blocks only itself.
- **Body cap:** `MAX_REQUEST_BODY` = 8 MiB, checked against `Content-Length`
  *before* the body is buffered → **413**.
- **Teardown:** best-effort `SSL_shutdown`, `shutdown(SHUT_WR)`, then drain up
  to 4096 bytes within a 100 ms `poll` before `close()`. This absorbs the
  client's own close so it does not surface as an error.

### Protocol facts worth knowing

| | |
|---|---|
| Responses | `HTTP/1.1`, always `Connection: close` — **no keep-alive** |
| Request bodies | `Content-Length` only — **no `Transfer-Encoding: chunked`** |
| `Last-Modified` | set to `WEBC_BUILD_TIME` on every response |
| Redirect targets | `?redirect=` must start with a single `/` — `//` and `/\` rejected (open-redirect guard) |

No keep-alive is a deliberate trade: it removes connection-reuse state from a
coroutine scheduler that has none. If keep-alive is ever wanted, the response
builder and the `cleanup` path in `serve_request_coro` are the two places that
must change together.

## Router — `src/http/route.c`

A static array built once by `route_initialize()`. Three match kinds:

- **`ROUTE_EXACT`** — full URI equality, plus a method if the entry names one.
- **`ROUTE_PREFIX`** — `sv_starts_with`, for static trees.
- **`ROUTE_ID_ACTION`** — the pattern `/<prefix>/<id>/<suffix>`; `parse_uri_id`
  extracts the middle segment and classifies it, storing it in `sc->route_id`
  **once**, so handlers never re-parse the URI.

A `method` of `NULL` means "the handler dispatches methods itself" — that is
how `/api/notes` accepts GET/POST/PUT/DELETE and answers 405 for the rest.

Two safety checks live at the routing edge:

- `uri_has_dotdot()` — rejects `..` as a whole segment anywhere in the URI
  before any path is built from it.
- Static paths are constructed as `"./js/" + rest` only after that check.

Unmatched → `serve_error(sc, 404)`.

## Handlers and rendering

Handlers are generated by macros in `src/core/webc_template.h`:

```c
SERVE_CRUD(users, user, Users, User, fields, opt_fields)
```

expands to `serve_users_read`, `serve_users_edit`, `serve_users_delete`,
`serve_users_create`, `serve_users_update`. `SERVE_READ` is where the
pagination contract lives: parse `?page`/`?per_page`, `count_*` for the total,
read the window, and when `?fragment=all` was requested **skip the page shell
entirely** and emit only the pagination store. That bare payload is what
`js/PaginationSwitcher.js` fetches to flip pages client-side.

Rendering is three steps:

```c
PAGE_BEGIN(sc, "Users", "/users");   // declares sb, page_title, current_path;
                                     // calls render_page_header()
#include "build/h_to_html/user.h"    // the template, as C
PAGE_END(sc);                        // render_page_footer()
```

`PAGE_BEGIN` and `PAGE_END` are macros, not calls into a layout engine — see
[templates.md](templates.md).

## Source map

| File | Responsibility |
|---|---|
| `src/webc.c` | CLI verbs, `dev` file-watcher, env resolution |
| `src/http/serve.c` | sockets, coroutines, TLS, HTTP parse/serialize, escaping, static files |
| `src/http/route.c` | route table, URI/id parsing, traversal guard |
| `src/http/utils.c` | form/query field lookup, shared helpers |
| `src/http/id.h` | `Route_Id` classification (all-digits → integer tables) |
| `src/core/webc_template.h` | `PAGE_BEGIN/END`, `SERVE_*` — the macro layer |
| `src/db/db.c` | dialect selection, migration history, backup/restore |
| `src/db/sql.h` + `src/db/sql.c` | dialect-neutral statement API |
| `src/db/driver_sqlite.c` | SQLite (bundled amalgamation) |
| `src/db/driver_mysql.c` | MySQL/MariaDB via libmariadb |
| `src/db/driver_postgres.c` | **stub** — fails with a clear message |
| `src/layout/header.c`, `footer.c` | page chrome (shell HTML) |
| `src/layout/paging.c` | windowed pagination maths |
| `src/layout/master_child.c` | the master-detail engine |
| `src/layout/dashboard.c` | the `/` landing page |
| `src/notes/`, `src/user/`, `src/people/`, `src/dashboard/`, `src/version/` | feature modules: column shapes + `SERVE_*` invocations |
| `src/crud/` | declarative CRUD API — **compiled, no callers** |
| `src/tt.c` | the `.tt` → C compiler (build-time only) |
| `nob.c` | the build driver |

### Two things that look load-bearing but are not

- **`src/crud/`** exports `crud_rows_load`, `crud_insert`, `crud_update`,
  `crud_delete`, `crud_form_options_load`. Nothing in `src/` calls any of them.
  The live path is `SERVE_*` + `master_child.c`. The `crud_path` field that
  *does* appear everywhere belongs to `master_child.h` and is unrelated to
  `crud.h`.
- **`driver_postgres.c`** is a complete `Sql_Driver` vtable whose every
  function returns failure with `"postgres driver is not implemented yet"`.
  This is intentional: the dialect is real everywhere else (queries, DDL,
  migrations), so the slot exists — but nothing should assume Postgres works.

## Front-end

`js/` holds four vanilla files: pagination, list expand, sidebar, theme.
`css/output.css` is compiled from `css/input.css` by Tailwind at build time.

Static serving is **two-tier**: `serve_resource()` first looks the path up in
the build-time `resources[]` table and answers from the `bundle[]` byte array
embedded in the binary; if it is not there (an uploaded avatar, say) it falls
back to reading the file from disk. The bundle covers `css/output.css`, two
images and the four JS files.

## Build pipeline — `nob.c`

```
build/bin/tt          (compile src/tt.c)
   └─ display/*.tt  ──▶  build/h_to_html/**.h
tailwindcss-linux-x64 css/input.css ──▶ css/output.css
build_bundle()        css/js/images ──▶ build/bundle.h
cc  src/**.c  + build/sqlite3.o + build/coroutine.o ──▶ build/bin/webc
```

The `cc` step **globs every `.c` under `src/`** (excluding `tt.c` and
`nob.c`). Consequences:

- a new `.c` file compiles automatically — no list to update;
- a file that is `#include`d as text by another source *and* picked up by the
  glob is compiled twice → duplicate-symbol link error. Include the `.h`.
