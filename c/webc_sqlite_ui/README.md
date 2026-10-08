# webc

C web server + SQLite admin UI framework. No npm, no framework — hand-rolled
HTTP server (coroutine based), `.tt` templates compiled to C headers, Tailwind
for styling, vendored SQLite amalgamation.

## Build / run

```sh
./build/bin/nob                  # build (also: ./build.sh)
./build/bin/webc serve [port]    # serve on 127.0.0.1:8030
./build/bin/webc dev [port]      # auto rebuild + reload on source change
```

Build browser SQLite separately with [WASI SDK](https://github.com/WebAssembly/wasi-sdk)
(Preview 1; set `WASI_SDK_PATH` to its installation directory):

```sh
WASI_SDK_PATH=/opt/wasi-sdk ./build-wasm.sh
```

This creates `build/sqlite.wasm`; `/db` serves it to the page, which runs
**all SQL in the browser** (WASI Preview 1, no host filesystem): schema tree,
paged data grid with inline insert/update/delete, SQL console, table
designer (`CREATE TABLE`), .db/.sql import, .db export. The server only
transports bytes:

| Route | Role |
| --- | --- |
| `GET  /db/file` | raw bytes of the active database + `X-DB-Meta` change token; `X-DB-Wal: 1` when `-wal`/`-shm` sidecars exist (served bytes exclude their frames → view may be stale) |
| `POST /db/save` | save-back: verifies SQLite header, matching `X-DB-Meta` (file unchanged), no `-wal`/`-shm` sidecars; writes `<db>.bak` then replaces atomically |

If `build/sqlite.wasm` is missing, `/db` shows an error instead of silently
using the server as a SQL fallback.

App database defaults to `~/.sqlite3/webc_sqlite_ui/sqlite.db3`;
`WEBC_DB=/path/to/db` overrides it (parent directories are created as needed).
Recent-file history stays in `~/.sqlite3/webc_sqlite_ui/recents`.

## Test

```sh
./test/run.sh         # build + throwaway HOME + sqlite/mysql/https suites
SKIP_BUILD=1 ./test/run.sh   # reuse the current binary
```

Tests never touch the real `~/.sqlite3/webc/db`. The suite also drives
`/db` in headless Chromium over CDP (`test/click_test.py` → `test/click.mjs`):
tab/pane switching, tree→grid, SQL run, dirty indicator, info-panel refresh,
reload — it skips itself when chromium or a WebSocket-capable node is absent.

## Status

The sample demo app (notes / users / people / dashboard, migrations
`0001_notes` + `0002_users`, golden files, clinic test scripts) has been
removed — this repo is now the bare framework for a **browser-based SQLite
management UI powered by WASM** (Navicat-style).

Phases done (tests: `http`/`fresh`/`golden`/`dbadmin` HTTP checks, the Node
WASI smoke `test/wasm_smoke.mjs`, which instantiates `sqlite.wasm` and drives
load / query / edit / create / dump / exec end to end, and the headless
Chromium click suite `test/click_test.py`):

| Phase | What |
| --- | --- |
| P0 | baseline green |
| P1 | `sqlite3.c` → `build/sqlite.wasm` (WASI-SDK reactor), in-page boot |
| P2 | `GET /db/file` transport → `sqlite3_deserialize` in browser |
| P3 | schema tree (tables/views/columns/indexes) rendered client-side |
| P4 | paged data grid (rowid-keyed, 50 rows/page) |
| P5 | SQL console (single statement, bound-free, runs in WASM) |
| P6 | row insert/update/delete with bound parameters |
| P7 | table designer → `CREATE TABLE` |
| P8 | export `.db` (serialize), import `.db` (replace), run `.sql` (exec) |
| P9 | save-back: browser posts bytes, server guards meta/WAL, backs up, replaces |
| P10 | grid sort (click header), text filter across columns, page jump; reload-from-device button for save conflicts |
| P11 | drop table/view from the tree (confirm) |
| P12 | dirty-tracking indicator (`sqlite3_stmt_readonly`-based); alter existing table: rename/add/drop column via designer with DDL preview confirm |
| P13 | console result cap (500 rows) + localStorage history (last 20); CSV export of the current grid view (filter/sort applied, ≤100k rows) |
| P14 | index designer (unique, column checkboxes) + view editor (compile-checked save) + index drop from the tree |
| P15 | trigger editor (full CREATE TRIGGER, compile-checked with restore-on-failure) + triggers in the tree |
| P16 | console transactions (BEGIN/COMMIT/ROLLBACK) + autocommit indicator (`sqlite3_get_autocommit`) |
| P17 | grid row selection (page select-all) + bulk delete via validated `rowid IN (...)` |
| P18 | database info panel (page/encoding/journal/freelist/counts, auto-refresh), `foreign_keys` session toggle, Ctrl+S save shortcut |
| P19 | workspace UI redesign: Catppuccin Latte/Mocha palette (`css/db.css`), phpMyAdmin/Navicat-style layout — toolbar, object sidebar, tabbed panes (Browse / SQL / Structure / Connection / Info), status bar; flat color regions + 1px rules instead of nested rounded cards. Fixed boot-time recursion: `PRAGMA journal_mode` is reported writable by `sqlite3_stmt_readonly`, so the info panel's auto-refresh looped `dbInfo → updateDirty → infoHook` until the wasm stack died and every button stayed unbound — pragma get-forms no longer mark the DB dirty and `updateDirty` is re-entrancy-guarded; covered by the click suite. Second dead-button bug: opening a WAL-mode file (file-format bytes 18/19 = 2) made `lockBtree` demand the `-wal` sidecar this fileless engine cannot have → `SQLITE_NOTADB` at the first prepare → boot died before binding handlers — `load()` now presents such images as rollback-journal (content pages are identical) and `dump()` restores the original format bytes so save/export round-trips the journal mode; `/db/file` flags sidecars with `X-DB-Wal` (status-bar stale-view warning); covered by the wasm smoke, dbadmin and the WAL-fixture click suite. Third bug, found while regression-testing that fix: `load()` handed `sqlite3_deserialize()` a raw `malloc()` buffer, but `sqlite3MemFree()` assumes `sqlite3_malloc()`'s 8-byte size header (`free(p-8)`) — close/realloc then read a garbage size from the bytes preceding the block, so `dlfree` trapped out of bounds (or silently leaked when the junk read as 0), and `dump()`'s serialize result and `sqlite3_exec()`'s message were freed with raw `free()` the same leaky way; every sqlite-owned buffer now crosses the boundary through new `sqlite3_malloc`/`sqlite3_free` wasm exports |

Server never executes SQL for the UI; it opens files only for its own
Phase-1 helpers (open/switch/page rendering) and for byte transport.

An older database that still carries the demo schema is rebuilt on first
start: the previous file is kept next to it as `db.bak`, the new schema is
empty (`Migrations` table only).

Known limits: request bodies cap at 64 MB; save-back refuses when `-wal`/`-shm`
sidecars exist, and while they do the served main file excludes their
un-checkpointed frames — `/db/file` marks that with `X-DB-Wal` and the status
bar warns the view may be stale; console results cap at 500 rows and CSV
export at 100k rows;
`ALTER TABLE` cannot change a column's type (SQLite restriction — rename/add/
drop only); visual styling itself (colors, spacing, theme switching) still
needs eyeballing — the click suite verifies behavior, not pixels.

## Layout

| Path | What |
| --- | --- |
| `src/webc.c` | CLI (`serve` / `dev` / `help` / `version`) |
| `core/http/` | server, routing, request/response utils |
| `core/layout/` | page header/footer, pagination, master-child engine |
| `core/display/` | page handlers |
| `display/` | `.tt` templates (compiled into `build/h_to_html/`) |
| `css/` | stylesheets: `output.css` (framework build) + `db.css` (Catppuccin workspace theme) |
| `src/db/` | portable statement layer + sqlite/mysql/postgres drivers |
| `src/crud/` | compile-time CRUD module helper |
| `src/tt.c` | template compiler (built as `build/bin/tt`) |
| `build/nob.c` | build driver (nob) |
| `test/` | black-box suites |
