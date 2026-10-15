<!-- EXAMPLE dev-logs.md structure -->
# Dev log

Newest entry at the top. Append; do not rewrite history.

Status vocabulary:
**scaffold** = files exist, not functional · **wip** = being written ·
**works** = runs, may lack tests · **done** = runs and covered by tests ·
**blocked** = needs something first

---

## 2026-10-14 — Load test: the game keeps the documents and rewrites the id to its own spelling

### Built

- Nothing; this entry is the experiment D16 closed on. A copy of
  the D15 snapshot holds three full chests (27 lava-bucket stacks
  each) side by side: a control the game itself wrote (`Chest`,
  -994,18,79), one mcme wrote as `minecraft:chest` (-994,20,79)
  and one as `chest` (-993,19,79) — the namespaced spelling
  mcme's own writes used, and a wrong-case one that is neither
  spelling's canonical form. Loaded in the game (1.26.52.3),
  played about five minutes (Time +5,996 ticks), Save & Quit,
  store read back.

### Measured

- Every document survived, and all three ids came back `Chest`.
  The game accepts either spelling on load — nothing dropped, no
  items lost — and rewrites the id to its own spelling at the
  next save. The `minecraft:chest` chest demonstrably worked
  in-game: its contents changed during the session (27 stacks
  down to 4, the player interacting with it).
- The re-serialization is the game's usual touch-up, not the
  id's fate: it added `BlockEntityVersion: 0` and dropped
  `isMovable` — on the control document mcme never touched, the
  same as on the two it did. `Items`,
  `pairlead`/`pairx`/`pairz` and the position carried unchanged,
  and level.dat kept the name and spawn mcme wrote.
- D16 does not reverse: nothing dropped, so there is no repair
  job — mcme's namespaced writes load, work, and the game
  canonicalises them itself. `put` keeps storing what it was
  given; the game's save is the normaliser.

### Method notes

- Two earlier sessions went to the operator's live world instead
  of the prepared copy — the world list shows both under the same
  name. The store told them apart without guessing: in the
  prepared world `level.dat` still held the prep write and
  `Time`/`currentTick` never advanced, while the live world's db
  and `level.dat` carried the session. A world that ticked says
  so in level.dat; document reads alone cannot.
- The load-test world survives at
  `minecraftWorlds/mcme_D16_loadtest` with the post-load store in
  place.

### Next

- None for D16 — the question it named is closed. The load-test
  world and the operator-world strays from the two misdirected
  sessions await disposal.

---

## 2026-10-14 — The write side meets the game's spelling: `put` takes a bare savegame id

### Built

- `mcme_bedrock_put_block_entity` now refuses an id in *neither*
  spelling instead of an id without a colon. The old premise —
  "the reference's decoder requires [a namespace] … the game and
  amulet drop" anything else — measures backwards: the real-world
  store holds 23,806 documents across 43 distinct ids and every
  one is bare (`Chest`, `TrappedChest`, `BrushableBlock`), the
  wiki calls the field "the savegame ID of the block entity", and
  `strings` on the game's `libminecraftpe.so` (v1.26.52.3) holds
  the registry literals bare — `Chest`, `Barrel`, `Hopper`,
  `BlastFurnace` and a dozen more — with no bare
  `minecraft:chest` anywhere. The refuser was refusing the game's
  own format.
- What stays refused is a shape neither spelling has: no id, an
  empty side of the colon, a second colon, a character an id in
  neither form holds (letters, digits, `_`, `-`, `.`) — USAGE,
  before any byte is written. The id itself is stored exactly as
  given: mcme checks the shape both spellings share, not the
  registry, which is the game's and versioned. `Chest` in,
  `Chest` out; `minecraft:chest` in, `minecraft:chest` out — and
  D15's read path takes either back.
- Pins: `test_bedrock_entity_write` refuses an id-less document,
  an empty name and a stray space, and round-trips a bare id
  (put `{id: "Chest"}` at a block, read `Chest` back with the
  position's coordinates set). `test_convert_carry` now puts the
  furnace under the bare id, so the write API itself feeds both
  spellings to convert — same five carried, the same
  `minecraft:furnace` answer under Java. The reference
  (`ref_bedrock_entity`, 27 checks) stores a bare id verbatim,
  cross-reads it against amulet's own loader, and keeps the
  refusal pin on documents in neither spelling, record untouched.

### Cross-check

- 5 suites PASS under ASan (debug + release); battery 11/11
  rc=0; goldens untouched (`git status tests/` empty); release
  selftest PASS.
- Measurements behind the rule: the D15 world snapshot's id
  census (43 distinct, 0 namespaced), the wiki's field
  definition, and the game binary's own strings.

### Known gaps

- Still untested, and only the game can test it: which
  spellings survive a load. mcme's earlier writes are
  namespaced; this world holds none, so nothing in it says what
  the game does with them — or with the wrong-case `{id:
  "chest"}`, which now stores. The experiment is one world
  holding both spellings, loaded, store read back after.

### Next

- That load test: a prepared copy with a `Chest` doc and a
  `minecraft:chest` doc side by side, opened in the game, the
  store compared afterwards — then decide from what the game
  left standing whether put writes one spelling after all.

