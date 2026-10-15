# Appendix — Debug Forensics (excluded from the presentation)

Chronology of the work that produced this codebase's boot path and its final
two fixes: a WAL-mode file making every button dead, and a heap-corruption
crash found by the regression test written for the first fix.

## Scope and hard constraints

- The server is a **file transport only**: `GET /db/file` streams bytes,
  `POST /db/save` replaces the file. No SQL ever executes server-side.
- Opening a file must **never modify it**: no journal recovery, no
  `PRAGMA journal_mode` writes, no sidecar creation.
- Saves persist to real device files, guarded by content meta (409 on
  mismatch) and by WAL sidecar presence (409: `core/display/db.c:341`).
- Everything SQLite runs in the browser: `sqlite3.c` → `build/sqlite.wasm`
  (WASI reactor), instantiated with a hand-written WASI shim (no filesystem).

## Timeline of milestones

- **P0–P1**: green baseline; `sqlite3.c` compiled to wasm; in-page boot via
  `wasm._initialize()` + `sqlite3_open(':memory:')`.
- **P2**: `GET /db/file` → `sqlite3_deserialize` in the browser (the function
  that later caused all the trouble).
- **P3–P8**: schema tree, paged grid, SQL console, DML with bound parameters,
  designer, export (`sqlite3_serialize`)/import/exec.
- **P9**: save-back with meta guard + WAL sidecar guard (server side).
- **P10–P18**: sort/filter/paging, drop objects, dirty tracking, CSV, index/view
  editors, trigger editor, transactions, bulk delete, info panel.
- **P19**: workspace UI redesign, plus three boot bugs (below) fixed inside
  the same milestone.

## Bug 1 — PRAGMA recursion killed boot (fixed first)

`PRAGMA journal_mode` (and other get-forms) is reported **writable** by
`sqlite3_stmt_readonly`, so the info panel's auto-refresh looped
`dbInfo → updateDirty → infoHook` until the wasm stack died and every button
stayed unbound. Fix: pragma get-forms no longer mark the DB dirty, and
`updateDirty` is re-entrancy guarded. Covered by the click suite.

## Bug 2 — WAL-mode file made every button dead

**Symptom.** Buttons worked on the plain page, then died after opening a
WAL-mode database (e.g. `~/.sqlite3/webc/db`, file-format bytes 18/19 = 2).

**Chain of cause.**

1. `/db/file` streams the WAL-format image; `load()` deserializes it as-is.
2. First prepare → `lockBtree()` sees read-version 2 → tries to open the
   `-wal` sidecar.
3. This engine has **no filesystem** (`fd_prestat_get` returns EBADF for
   every fd), so the open fails → `SQLITE_NOTADB`.
4. The throw happens inside the **single `try`** that wraps all page
   initialization, *before* any `.onclick =` binding runs → the `catch` only
   writes the status line → **zero handlers bound → all buttons dead.**

**Repair.** `load()` presents the image as rollback-journal (bytes 18/19 ← 1)
— content pages are identical in both formats, and a WAL image without
sidecars is exactly what a cleanly closed WAL file looks like. `walFormat`
remembers the original pair; `dump()` restores it so save/export round-trips
the device's journal-mode declaration. `/db/file` flags real sidecars with
`X-DB-Wal: 1` (`core/display/db.c:298`) → the status bar warns "view may be
stale". Coverage: wasm smoke p19, `dbadmin_test.py` (lines 102/123), the
WAL-fixture click suite.

## Bug 3 — regression test exposed heap corruption

**Repro (minimal, deleted after use).** `load(fixture)` → 400 `INSERT`s
(engages `memdbEnlarge`) → `dump()` → `load(dumpBytes)` → `load(fixture)` →
`RuntimeError: memory access out of bounds`.

**Observed call stack** (V8 wasm frames == `llvm-objdump` code-section
addresses — verified three times):

```
dlfree            0xf300b   ← i32.load(next+4): next->head, out of bounds
sqlite3MemFree    0xe8ef8
memdbClose        0x3bd63   ← closes the previous MemStore on attach
sqlite3_free / attach / sqlite3_deserialize
```

**Investigation steps.**

1. *Bisection:* crash tied to close of the previously deserialized store, not
   to INSERT, dump, or the WAL patch.
2. *First hypotheses, eliminated:* mutex-less build (`sqlite3_mutex_free` is a
   no-op under `THREADSAFE=0`), JS out-of-bounds writes (heap prefix intact),
   double free (guard would trap earlier).
3. *Instrumentation:* a `fprintf`-instrumented `sqlite3.c` printed the real
   dlmalloc header — and **contradicted** the JS trap: C read the same address
   as valid while wasm trapped on it.
4. *Disassembly breakthrough* (`llvm-objdump`, addresses above):
   - `sqlite3MemMalloc(n)`: `q = malloc(n+8); *(i64)q = n; return q+8;`
   - `sqlite3MemFree(p)`: **`free(p-8)`** — it re-reads a header *8 bytes
     before the pointer it was given*.
5. *Proof:* instrumented runs printed two different formats at the same
   offset `aData-8`:
   - never-grown buffer (came straight from JS): `[prev_foot=0, head=0x7013]`
     — a **raw dlmalloc header**;
   - grown buffer (passed through `sqlite3Realloc`): `[0x8000, 0]` —
     sqlite's **i64 size header**.

**Root cause.** `load()` handed `sqlite3_deserialize` a raw `malloc()`
buffer, but sqlite adopts it (`pStore->aData = pData`, flags
`FREEONCLOSE|RESIZEABLE`) and later frees it with `sqlite3MemFree` = `free(p-8)`.
`free(aData-8)` makes `dlfree` read its size head at `aData-12` — four bytes
inside the **previous chunk's tail**:

- junk reads as 0 → `!(head & CINUSE)` → silent early return → the block
  **leaks** (why it usually "worked");
- junk reads with size bits set → `next = p + (junk & ~7)` lands past the end
  of linear memory → `i32.load` at `0xf300b` **traps** → the browser tab dies.

Same leak on two other paths: `dump()`'s `sqlite3_serialize` result and
`sqlite3_exec()`'s errmsg were freed with raw `free()` (head reads as 0 →
no-op). `sqlite3Realloc` happens to *re-wrap* a raw block with a fresh i64
header, which is why grown buffers looked correct and only never-grown
images crashed on close.

**Repair.**

- `build-wasm.sh` now exports `sqlite3_malloc` / `sqlite3_free`
  (`-Wl,--export=…`, line 18); wasm rebuilt.
- `load()` allocates with `wasm.sqlite3_malloc` (failure path of
  `sqlite3_deserialize` frees `pData` itself — JS must never touch it).
- `dump()` frees the serialize result with `wasm.sqlite3_free`; `execAll()`
  frees the sqlite errmsg the same way.
- Raw `malloc`/`free` remain **only** for JS-private slots (cstring, `dbSlot`,
  `stmtSlot`, bind temporaries) — raw↔raw stays consistent.

## Verification

- Compact crash probe `node test/_alter_probe2.mjs p1..p5`: all pass
  (p2 was the crashing sequence) — probe files then deleted.
- Full suite `./test/run.sh`: **SUITE-RC=0**, 141 PASS/ok lines, 0 failed —
  http 27, mysql 10, https 5, dbadmin 73, click 5; `wasm_smoke.mjs` prints
  through `p19:` in place, RC=0.
- Real WAL database: `~/.sqlite3/webc/db`, 5 525 504 bytes, header `[2,2]`,
  34 tables, 51 tree nodes — loads read-only, `dump()` preserves `[2,2]`,
  reload round-trips.
- `node --check js/sqliteBrowser.js` clean; no debug shims or `__dbg` traces
  left; all `/tmp` forensic artifacts removed.

## Method notes

- V8's wasm stack PCs equal code-section offsets in `llvm-objdump` output —
  cheap symbolization without DWARF.
- `pkill -f` patterns need brackets (`'bin/we[b]c serve'`) to avoid matching
  the shell itself; the two live `webc dev` servers are user processes and are
  never killed.
- The server caches JS at startup: any `js/*.js` edit requires a server
  restart (or `test/run.sh`, which spawns its own throwaway instance).

## Corruption timeline (bug 3, step by step)

| # | Event | Heap state |
|---|------|-----------|
| S0 | `load()`: raw `malloc(n)` → `deserialize` adopts | `adata` = raw; real head at `adata-4` (`0x7013`-style) |
| S1 | 400 `INSERT`s → `memdbEnlarge` → `MemRealloc(adata-8, …)` | realloc misreads junk, still returns a block; **MemRealloc re-wraps it** with i64 → this buffer becomes correctly owned; the old raw block leaks (junk=0 early return) |
| S2 | `dump()` — serialize output freed with raw `free()` | head read at `ptr-4` = i64 high half = 0 → silent return → **leak, no crash** |
| S3 | `load(dumpBytes)` → adopts raw buffer #2, closes S1 store | S1 store is owned now → frees cleanly |
| S4 | `load(fixture)` → closes S2's store (never grown = still raw) | `memdbClose` → `sqlite3_free(raw)` → `free(raw-8)` |
| S5 | `dlfree` reads head at `raw-12` | junk = 0 → **silent leak** (the usual case: fresh heap tail is zeros) |
| S5' | junk has size bits set | `next = p + (junk & ~7)` past linear-memory end |
| S6 | `i32.load(next+4)` @ `0xf300b` | **`RuntimeError: memory access out of bounds`** — tab dies |

The nondeterminism of S5/S5′ (depends on what the *previous* chunk's tail
happens to contain) is why the bug looked intermittent: leak most of the
time, trap occasionally — and always only on never-grown images.

## Failure-mode matrix

| Wrong pattern | Where | What happens |
|---|---|---|
| raw `malloc` → `deserialize` (grows) | first enlarge | realloc leaks old block, block re-wrapped → works, leaks |
| raw `malloc` → `deserialize` (never grows) | close | head from junk: 0 → leak; nonzero → **trap @0xf300b** |
| raw `free(serialize result)` | `dump()` | head=0 → silent **leak per dump** |
| raw `free(sqlite errmsg)` | `execAll()` | head=0 → silent leak per error |
| `sqlite3_free(JS slot)` | anywhere | would *underflow* → crash — never done |
| WAL bytes left as `2` | first prepare | `SQLITE_NOTADB` → boot FAILED → **dead buttons** |
