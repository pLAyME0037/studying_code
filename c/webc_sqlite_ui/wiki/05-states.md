# State — What Can Be True at Any Moment

The state machines of the session, the file format, the memory store and
the heap — what changes them, and what stays true.

## Boot states of the page

```
              fetch + instantiate + open
   idle ───────────────────────────────────► engine up, UI unbound
    │                                            │ first load(bytes)
    │ fetch/open failed                          ▼
    ▼                                        error ──► FAILED
   FAILED                              (status line only, no handlers)
   FAILED = "Browser SQLite unavailable: …" in the status bar;
            buttons stay dead until you reload the page.
   engine up ── bind all buttons/tabs ──► READY (normal use)
```

- Everything — engine boot, first load, handler binding — sits inside one
  `try`: the first error stops it, which is why boot failures look like a
  dead UI rather than an exception dialog.

## The dirty flag

| Moment | Value | Why |
| --- | --- | --- |
| after `load()` | false | fresh bytes, nothing unsaved |
| write statement / `execAll` | true | in-memory differs from file |
| pragma *reads* (e.g. `journal_mode`) | unchanged | reading isn't editing |
| successful Save | false | disk now matches memory |
| `isDirty()` / status dot | — | the UI's only signal to save |

- `updateDirty()` refuses to run while already running (re-entrancy
  guard), so no update loop can ever grow unbounded.

## The WAL format memory (`walFormat`)

| Where | Bytes 18/19 | Meaning |
| --- | --- | --- |
| your file on disk | `2, 2` | written in WAL mode |
| wasm copy during `load()` | `1, 1` | read as a normal rollback file |
| `walFormat` variable | `2, 2` | remembers the original |
| `dump()` output | `2, 2` | save/export round-trips it |
| `/db/file` response | — | `X-DB-Wal: 1` if sidecars exist |

- Content pages are byte-identical in both formats, so the browser reads a
  safe view while remembering the truth about your file.

## The memory store record

Fields: `aData` (the bytes), `sz` (used), `szAlloc` (allocated),
`szMax` (limit), `mFlags` (may free / may grow), `nRef` (reference count).

| From | Event | To |
| --- | --- | --- |
| — | `deserialize` adopts buffer | live store, size = file |
| live | write past `szAlloc` | grown ×2, new buffer |
| live, ref > 0 | another image attaches | ref decremented |
| last ref | close | buffer freed, record deleted |

## Heap blocks: two shapes

```text
engine-owned (from sqlite3_malloc):
[ dlmalloc header ][ int64 size ][ payload... ]
                    ^ sqlite3_free steps back 8 bytes to here
plain (raw malloc, for JS-private slots):
[ dlmalloc header ][ payload... ]
                    ^ sqlite's view would be wrong here — never share
```

- Two shapes exist so that each side (JS or SQLite) manages exactly the
  blocks it allocated — see the ownership table in the previous page.

## The rules (invariants)

1. SQLite-freed buffers come from `sqlite3_malloc`; JS-freed buffers come
   from `malloc` — never crossed.
2. The wasm copy of a WAL image reads as rollback; `walFormat` is the
   single source of truth for restoring it.
3. `X-DB-Wal` on `/db/file` ⇔ sidecars exist on the device; save is
   refused (409) while they exist or the meta token changed.
4. One `try` wraps boot: an error before binding ⇒ inert buttons, status
   line only.
5. `dirty` flips only on real writes; `updateDirty` cannot recurse.
6. Opening never modifies your file — reading is pure, saving is explicit.
