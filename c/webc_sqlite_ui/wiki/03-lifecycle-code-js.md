# Program Lifecycle — JavaScript (the glue)

Every function the page owns, in plain language: what it does, how it runs,
what state it touches. Line numbers refer to `js/sqliteBrowser.js`.

## `boot()` — start the engine (line 478)

```js
const res = await fetch('/wasm/sqlite.wasm', { cache: 'no-store' });
const { instance } = await WebAssembly.instantiate(
    await res.arrayBuffer(), { wasi_snapshot_preview1: imports });
const wasm = instance.exports;
memory = wasm.memory;
wasm._initialize();
wasm.sqlite3_open(cstring(':memory:'), dbSlot);   // one connection
```

- Fails only if the wasm file is missing (message: "run ./build-wasm.sh").
- Defines every helper and API call the UI will use, then returns `api`.

## Helpers the whole file leans on

- **`cstring(s)`** — copies a JS string into wasm memory (raw `malloc`),
  returns the pointer. The caller frees it in a `finally`.
- **`read(ptr)`** — reads a C string out of wasm memory (used for results).
- **`check(rc)`** — throws a readable error unless SQLite says OK/ROW.
- **`bindParams()`** — JS values → SQL parameters: `null`→NULL, string→TEXT,
  integer→i64, decimal→REAL, boolean→0/1.

## `load(bytes)` — bring a file image in (line 968)

```js
const ptr = wasm.sqlite3_malloc(bytes.length);   // sqlite-owned buffer
const copy = new Uint8Array(memory.buffer, ptr, bytes.length);
copy.set(bytes);
const patched = bytes.length > 19                // a WAL-format file?
    && (bytes[18] === 2 || bytes[19] === 2);
if (patched) copy[18] = copy[19] = 1;            // present as rollback
wasm.sqlite3_deserialize(db, 'main', ptr, n, n, 3);  // engine adopts ptr
walFormat = patched ? [bytes[18], bytes[19]] : null;
dirty = false;
return api.schema();
```

- The buffer **belongs to SQLite** from the `deserialize` call on — the
  engine grows it and frees it at close; JS must never free it.
- Only the wasm copy is patched; your original bytes stay pristine so
  `dump()` can restore the real format.
- `api.schema()` reads `sqlite_master` and returns the object tree.

## `dump()` — take a snapshot out (line 641)

```js
const ptr = wasm.sqlite3_serialize(db, 'main', sizePtr, 0);
const bytes = new Uint8Array(memory.buffer, ptr, len).slice();
wasm.sqlite3_free(ptr);                 // sqlite-allocated → sqlite-freed
if (walFormat) {                        // restore device journal bytes
    bytes[18] = walFormat[0];
    bytes[19] = walFormat[1];
}
return bytes;
```

- Produces a plain JS byte array — used by Save, Export and Reload checks.

## `queryRows()` — run one statement (line 547)

1. Prepare: `sqlite3_prepare_v2` → statement handle.
2. Bind parameters (see `bindParams`).
3. Read-only check: writes flip `dirty = true` (pragma reads do not).
4. Step loop: each `SQLITE_ROW` → one JS row, capped by `maxRows`.
5. `finally`: `sqlite3_finalize`, free the SQL string and slot.

## `execAll()` — run a whole script (line 619)

- Wraps `sqlite3_exec` (multi-statement, no parameters) for `.sql` imports.
- On error SQLite allocates an error message; the code frees it with
  `sqlite3_free` — same ownership rule as everywhere else.

## The buttons that move bytes

| Button | Code path |
| --- | --- |
| Save | `dump()` → `POST /db/save` + `X-DB-Meta` → `dirty=false` |
| Reload | `GET /db/file` → `load()` → rebuild tree, clear grid |
| Export | `dump()` → Blob → `export.db` download |
| Import .db | file bytes → `load()` → rebuild tree |
| Import .sql | file text → `execAll()` |
| Ctrl+S | synthesizes a click on Save |

## The WASI shim — a tiny fake OS (line 427)

```js
fd_write(fd, iovs, count, written) {     // stdout/stderr: consume, no log
    ...; return fd === 1 || fd === 2 ? 0 : 8;  // 8 = EBADF
},
fd_prestat_get() { return 8; },          // "no files are preopened"
clock_time_get(...) { ... },             // Date.now / performance.now
random_get(ptr, len) { ... },            // crypto.getRandomValues
```

- Returning "bad descriptor" for everything ends wasi-libc's filesystem
  scan cleanly — the engine concludes *no disk exists* and stays in memory.
- An `imports` `Proxy` answers any unknown call with `ENOSYS` (52), so the
  engine degrades instead of crashing.
