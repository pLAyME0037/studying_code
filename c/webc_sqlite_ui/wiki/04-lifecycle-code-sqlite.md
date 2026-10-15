# Program Lifecycle — SQLite & the Allocator (inside the wasm)

What happens inside the compiled engine when data moves, in plain language,
with the one memory rule everything depends on.

## The rule that keeps memory safe

SQLite keeps a tiny **8-byte size header in front of every block it
allocates**. So buffers cross the JS↔engine boundary in exactly one
direction each way:

| Buffer | Who allocates | Who frees | With which call |
| --- | --- | --- | --- |
| file image in `load()` | JS calls `sqlite3_malloc` | SQLite (close/failure) | `sqlite3_free` |
| `dump()` snapshot | SQLite (`sqlite3_serialize`) | JS | `sqlite3_free` |
| error message from `sqlite3_exec` | SQLite | JS | `sqlite3_free` |
| strings/slots for JS itself | JS `malloc` | JS | `free` |

**Plain English:** if SQLite may free or grow it, it came from
`sqlite3_malloc` — and vice versa. Mixing the two corrupts the heap.

## `sqlite3_deserialize()` — the engine adopts your bytes

```c
// flags: FREEONCLOSE | RESIZEABLE  (value 3)
pStore->aData  = pData;   // the buffer becomes the database image
pStore->sz     = szBuf;   // current size
pStore->szMax  = 1 GB;    // how far it may grow
```

- Called by `load()`; from that moment the engine owns the buffer and may
  grow it when you insert data.
- Attaching a new image closes the previous store: its buffer is freed
  with `sqlite3_free(aData)` — the moment where ownership matters.

## The memory store: adopt → grow → close

| Stage | What happens | State after |
| --- | --- | --- |
| adopt | `deserialize` takes the buffer as-is | image = file bytes |
| grow | a write exceeds the buffer → `sqlite3Realloc(..., size*2)` | bigger buffer, same data |
| close | last reference gone → `sqlite3_free(aData)` | store deleted |

- A read-only session (browse, no edits) never grows: the very first
  buffer is also the one that gets freed at close.

## `sqlite3_malloc` vs plain `malloc`

```c
void *sqlite3MemMalloc(size_t n) {
    void *q = malloc(n + 8);      // 8 bytes extra…
    *(int64_t *)q = n;            // …for our size header
    return (char *)q + 8;         // hand out the part after it
}
void sqlite3MemFree(void *p) {
    free((char *)p - 8);          // step back over the header first
}
```

- The header is what `sqlite3_free` steps over — a plain `malloc` pointer
  would make it read the wrong 8 bytes.
- That is why `load()` allocates with `sqlite3_malloc`: the engine's own
  bookkeeping must line up with the engine's own frees.

## Growing the database image

1. You insert enough rows to pass the current buffer size.
2. `memdbEnlarge` doubles it (capped at `szMax`), via `sqlite3Realloc`.
3. Data is copied; the store keeps pointing at the (possibly new) buffer.
4. `RESIZEABLE` is set, so growth is allowed; without it you'd get
   `SQLITE_FULL`.

## `sqlite3_serialize()` — copy the database out

1. `dump()` asks for `main` as contiguous bytes.
2. SQLite allocates the result with `sqlite3_malloc`, fills it with a
   consistent snapshot (like a backup file).
3. JS copies the bytes out to a normal array, then frees the buffer with
   `sqlite3_free`.
4. If the session reads a WAL-format file, `dump()` writes the original
   journal bytes back into the copy before returning it.

## Why the header rule matters (one sentence each)

- **Right pairing** → buffers free and grow silently, all session long.
- **Wrong pairing** → the engine reads size information from the wrong
  place: at best the buffer leaks, at worst the heap is damaged.
- The codebase keeps the rule visible: `sqlite3_malloc`/`sqlite3_free` are
  exported from the wasm on purpose, and every call site is commented.
