# webc_sqlite_ui Wiki

How SQLite (written in C) is compiled, served and driven from JavaScript by
the local web server — the **process**, the **lifecycles** (system and
code), and the **states**. Companion to the project [README](../README.md).

## Pages

- **[01 — The Process](01-process.md)** — how SQLite + C work with the
  local web and JS: the pieces, the wasm build, the routes, the boot
  sequence, how data moves in and out, and what keeps the device file safe.
- **[02 — System Lifecycle](02-lifecycle-system.md)** — from "server
  resting on disk" to "UI ready": page load, engine session, open / run
  SQL / save round trips, and what happens when boot fails.
- **[03 — Program Lifecycle: JavaScript](03-lifecycle-code-js.md)** — each
  function of the glue layer: `boot`, `load`, `dump`, `queryRows`,
  `execAll`, the byte-moving buttons, and the WASI shim — steps and state.
- **[04 — Program Lifecycle: SQLite & allocator](04-lifecycle-code-sqlite.md)**
  — inside the engine: the ownership rule, `sqlite3_deserialize`, the memory
  store, `sqlite3_malloc`'s 8-byte header, growing and serializing.
- **[05 — States](05-states.md)** — boot states, the dirty flag, the WAL
  format memory, the memory store record, heap block shapes, invariants.
- **[99 — Appendix: Debug Forensics](99-debug-forensics.md)** — the bug
  hunt that shaped the loader (kept for the record, **not** part of the
  presentation).

## Convert to an Impress presentation

```sh
python3 tools/wiki2impress.py                  # → webc_sqlite_wiki.odp
python3 tools/wiki2impress.py --pptx           # also → .pptx (LibreOffice)
python3 tools/wiki2impress.py --include-all    # appendix pages too
```

Stdlib only. Each `#` heading becomes a title slide, each `##` heading a
content slide (sections longer than `--max-lines`, default 12, continue on
"… (cont.)" slides); code blocks render monospace on a light background;
tables keep their columns; speaker notes carry the raw markdown of the
section. Pages whose names start with `9` (the appendix) are skipped by
default so the deck stays presentation-friendly.
