# System Lifecycle — From Power-On to Ready

How the whole system (server + page + engine) comes alive, lives, and shuts
down, step by step.

## The system at rest

- `webc dev` watches the project; for each session it starts
  `webc serve 8030` on `127.0.0.1:8030`.
- Nothing SQL-related is running: the server only has files on disk —
  HTML templates, CSS, JS, `sqlite.wasm`, and your `.db` file.
- Opening a browser is what starts the engine.

## Page load — from URL to ready UI

1. You visit `http://127.0.0.1:8030/db`.
2. Server sends the HTML shell (tabs, sidebar, grid, status bar).
3. The page loads `js/sqliteBrowser.js`, which immediately starts `boot()`.
4. `boot()` fetches `sqlite.wasm` and instantiates it with the WASI shim.
5. Engine opens an empty `:memory:` connection, then `/db/file` supplies
   the database image, which `load()` adopts and reads (schema tree).
6. Only now are all buttons and tabs given their click handlers → **ready**.

## One engine session per page

```
page open ─► sqlite3_open(':memory:') ─► load(file bytes)
                                              │
             ┌── any number of queries ◄──────┘
             │        (prepare → step → finalize)
             ▼
      serialize() ─► save/export ─► (or) page close: wasm instance discarded
```

- The connection object lives until you close or reload the tab.
- Unsaved edits live only in browser memory; the disk file changes only
  when you press **Save**.

## Opening a database

1. `GET /db/file` returns bytes plus `X-DB-Meta` (change token) and
   optionally `X-DB-Wal` (sidecars exist on the device).
2. `load(bytes)` copies the bytes into wasm memory, patches WAL format
   bytes for safe reading, and calls `sqlite3_deserialize`.
3. The engine now owns that image as its `main` database.
4. `api.schema()` walks `sqlite_master` → the object tree on the left.
5. `dirty` resets to false — nothing unsaved yet.

## Running SQL

- **Grid click / page / sort / filter** → one SELECT via `tableRows()`.
- **SQL console (single statement)** → `query()`, results in the grid.
- **.sql script** → `execAll()` runs everything in one call.
- Any statement that is not read-only flips `dirty = true`; the status bar
  shows the unsaved marker until a successful save.

## Saving back to disk

1. `dump()` serializes the whole database to bytes (WAL format bytes
   restored to the original pair).
2. `POST /db/save` sends bytes + the `X-DB-Meta` you opened with.
3. Server checks: real SQLite header → same meta (file unchanged) → no
   `-wal`/`-shm` sidecars. Any failure returns **409** with a reason.
4. Success: `<db>.bak` written, file replaced atomically, new meta
   returned; `dirty` clears.

## Export, import, and .sql scripts

- **Export .db** — `dump()` → browser download (`export.db`).
- **Import .db** — file picker bytes → `load()` replaces the session image
  (the device file is untouched until you save).
- **Import .sql** — text file → `execAll()`; errors surface as messages.
- **Reload from device** — re-fetch `/db/file` and start fresh; this is the
  escape hatch whenever a save is refused.

## When boot fails

- Any error during boot or the first `load()` is caught in one place: the
  status line shows `Browser SQLite unavailable: …` (with `role="alert"`).
- Because handlers are bound *after* that first load, a boot failure means
  the buttons stay inert — the status line is your signal to reload or
  check the server log.
