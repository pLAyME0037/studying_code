# The Process: How SQLite (C) Works with the Local Web and JS

How the pieces fit together: a C web server, the SQLite engine compiled from
C to WebAssembly, and JavaScript gluing them into a database manager that
runs entirely in your browser.

## The big picture

- **SQLite** is written in C. We take its single-file source and compile it
  to **WebAssembly (wasm)** — a binary format the browser can execute.
- Your **C web server (`webc`)** serves the page and ships files. It never
  runs SQL and never touches the database while you browse.
- The **browser** opens the database file as raw bytes, loads the wasm
  engine, and runs every query locally — like a desktop app, but served
  from localhost.
- JavaScript is the glue: it boots the engine, moves bytes in and out, and
  renders the tables, grid and buttons.

## The pieces

| Piece | What it is | Role |
| --- | --- | --- |
| `module/sqlite-amalgamation-3460100/sqlite3.c` | SQLite's whole C source in one file | input to the compiler |
| WASI SDK (`clang`) | C→wasm compiler with browser-friendly libc | builds `build/sqlite.wasm` |
| `build/sqlite.wasm` (1.27 MB) | the compiled SQLite engine | runs in the browser |
| `webc` (your C server) | HTTP server + file transport | serves pages and DB bytes |
| `js/sqliteBrowser.js` | ~1600 lines of JavaScript | boots the engine, binds the UI |

## Step 1 — compile the C engine to wasm

`build-wasm.sh` calls the WASI SDK's clang with three groups of flags:

```sh
clang -O2 -mexec-model=reactor          # library that can be re-initialized
  -DSQLITE_THREADSAFE=0                 # single-threaded: no mutexes needed
  -Wl,--export=sqlite3_prepare_v2 ...    # every function JS may call
  -o build/sqlite.wasm module/.../sqlite3.c
```

- `--export` lines **are** the API surface: JS can only call exported
  functions, so we export exactly what the UI needs (`prepare`, `step`,
  `bind_*`, `deserialize`, `serialize`, `malloc`, `sqlite3_free`, …).
- No filesystem flags: the engine runs inside browser memory only.

## Step 2 — the server serves files, never SQL

| Route | What it does |
| --- | --- |
| `GET /db` | the HTML page (tabs, grid, sidebar) |
| `GET /wasm/sqlite.wasm` | the compiled engine |
| `GET /db/file` | the database as download bytes + `X-DB-Meta` token |
| `POST /db/save` | accepts new bytes, verifies and replaces the file |
| `GET /css/*`, `/js/*` | stylesheets and scripts |

The same rules hold for every file route: **read bytes, send bytes** — no
query ever executes on the server.

## Step 3 — the browser boots the engine

```js
const res  = await fetch('/wasm/sqlite.wasm');       // 1. get the engine
const { instance } = await WebAssembly.instantiate(
    await res.arrayBuffer(), { wasi_snapshot_preview1: imports }); // 2. start it
const wasm = instance.exports;
wasm._initialize();                                  // 3. runtime init
wasm.sqlite3_open(cstring(':memory:'), dbSlot);      // 4. empty connection
```

- The `imports` object is a tiny fake OS (WASI): time, random numbers and
  output — deliberately **no disk**, so the engine cannot touch your files.
- One connection stays open for the life of the page.

## Step 4 — move the database in and out

1. **Open:** `GET /db/file` → `bytes` → `sqlite3_deserialize()` hands the
   image to the engine, which treats it as its `main` database.
2. **Work:** every click becomes `prepare → bind → step → finalize` calls
   inside wasm. All SQL runs in the browser.
3. **Save:** `sqlite3_serialize()` copies the whole database back to bytes
   → `POST /db/save` → the server checks and writes the real file.
4. **Export/import:** same bytes, delivered as a download or read from a
   file you picked.

## Step 5 — keep the device file safe

- The open token `X-DB-Meta` remembers what you loaded; if the file changed
  on disk since then, save is refused (**409**) until you reload.
- WAL-mode files (journal bytes 18/19 = 2) are presented to the engine as
  normal rollback files — content is identical, and there are no
  `-wal` sidecars in a browser. `dump()` puts the original bytes back so
  the saved file keeps its journal mode.
- Sidecar files (`-wal`/`-shm`) on the device mean someone else may be
  writing: the server flags them (`X-DB-Wal`) and refuses save.
- Before replacing, the server copies the old file to `<db>.bak` and
  renames atomically.

## Where everything lives

| Path | Contents |
| --- | --- |
| `core/`, `src/` | C server: routes, coroutine HTTP, `.tt` templates |
| `module/` | vendored `sqlite3.c` amalgamation |
| `build-wasm.sh` | C → `build/sqlite.wasm` |
| `js/sqliteBrowser.js` | browser engine + UI logic |
| `css/db.css` | the workspace look |
| `test/` | `run.sh` suite: HTTP, dbadmin, click, wasm smoke |
| `wiki/` | this documentation |
