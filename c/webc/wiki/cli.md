<!-- EXAMPLE cli.md structure -->
# `mcme` command-line contract

This is a **stable interface**. Scripts and agents depend on it. Add to it; do
not change existing meanings. If you must break it, bump the major version and
note it in [dev-log.md](dev-log.md).

## Invocation

```
mcme [global options] <command> [command options] [args]
```

## Global options

| Option | Effect |
|---|---|
| `-h`, `--help` | Print help for the given command, exit `MCME_OK` |
| `-V`, `--version` | Print version, exit `MCME_OK` |
| `--json` | Machine-readable output on stdout. Diagnostics stay on stderr. |
| `-q`, `--quiet` | Suppress non-essential output on stderr |
| `-v`, `--verbose` | `MCME_LOG_DEBUG` on stderr. Repeatable is not supported; one level is enough. |

`--json` is the important one: **when set, stdout is exactly one JSON value and
nothing else.** No progress bars, no banner, no trailing prose.

## Exit codes

Declared in `include/mcme/cli.h` as `McmeExit`. **Never renumber.**

| Code | Name | Meaning |
|---:|---|---|
| 0 | `MCME_OK` | Success |
| 1 | `MCME_EXIT_USAGE` | Bad command line: unknown command, missing arg |
| 2 | `MCME_EXIT_NOT_FOUND` | Path, world, or key does not exist |
| 3 | `MCME_EXIT_BAD_FORMAT` | Path exists but is not the expected format |
| 4 | `MCME_EXIT_IO` | Read/write failed (permissions, disk full) |
| 5 | `MCME_EXIT_NOT_IMPLEMENTED` | Recognised but not built yet |
| 6 | `MCME_EXIT_INTERNAL` | Should not happen; a bug |

**Rule for agents:** an exit code of `MCME_EXIT_NOT_IMPLEMENTED` (5) is not a
failure to report — it is a known gap. Check it against
[porting/parity.md](porting/parity.md) rather than treating it as an error.

## Output discipline

- **Human mode:** prose on stdout, progress and warnings on stderr.
- **JSON mode:** one JSON value on stdout, diagnostics on stderr.
- Errors go to stderr in both modes. In JSON mode an error is *not* printed as
  JSON — the exit code carries it. This keeps stdout parseable even when the
  command fails halfway.

## Commands

### `mcme selftest`

Runs the built-in self-tests for every subsystem compiled in.

```
mcme selftest              # human summary
mcme selftest --json       # {"subsystems":[{"name":"core","passed":3,"failed":0}],...}
```

Exit `0` if all pass, `MCME_EXIT_INTERNAL` if any fail.

### `mcme lang`

Works on the `.lang` translation files shipped in `assets/lang/`. The format is
the original project's, unmodified.

```
mcme lang list                       # ids, one per line
mcme lang list --json                # {"languages":["en","en_GB",...]}
mcme lang get <key> [--lang <id>]    # resolved string, falls back to en then the key
mcme lang get <key> --lang de --json # {"key":"...","lang":"de","value":"..."}
```

`lang get` exits `MCME_EXIT_NOT_FOUND` if the key is absent in every language.
By default it falls back to returning the key itself with exit `0`, because
that is what the UI does.

### `mcme config`

Works on the user config, which is **JSON** (not the original's pickle).

```
mcme config path                 # where the config file lives
mcme config get <dotted.key>
mcme config set <dotted.key> <json-value>
mcme config dump --json
```

### `mcme nbt dump`

Reads **raw (uncompressed) NBT** and prints it. Implemented in Phase 2.

```
mcme nbt dump <file>                  # SNBT, one line
mcme nbt dump <file> --json           # the same tree as JSON
mcme nbt dump <file> --le             # little-endian (Bedrock); default is big-endian
mcme nbt dump <file> --path a.b.0     # print only that subtree (dotted, lists indexed)
mcme nbt dump <file> --indent 2       # pretty-print
```

Exits `MCME_EXIT_BAD_FORMAT` if the bytes are not NBT, `MCME_EXIT_NOT_FOUND`
if `--path` does not resolve, `MCME_EXIT_USAGE` for a bad command line.

SNBT is the default because it is faithful: it keeps the type distinctions
(`4`, `4L`, `4f`) that JSON flattens away. `--json` exists so a tree can be fed
to `jq`, and trades type fidelity for a stable shape — note that infinity and
NaN become `null`, since JSON has no literal for them.

**Gzip is stripped here, not in the parser.** A file starting with `1f 8b` is
inflated before parsing. Compression is deliberately kept out of
`mcme_nbt_parse`: NBT has no framing of its own, and conflating the two would
make it impossible to read one NBT stream out of the middle of a file — which
is exactly what reading Bedrock chunk data requires. A corrupt gzip stream exits
`MCME_EXIT_BAD_FORMAT`.

Bedrock's `level.dat` is *not* gzip-framed the way Java's is: it carries an
8-byte header first. Use `mcme info` on the world directory rather than `nbt
dump` on that file.

### `mcme info`

Identifies a world directory and summarises it. Read-only; never writes, never
opens a database. Implemented in Phase 3.

```
mcme info <world>            # human-readable summary
mcme --json info <world>     # the same facts as one JSON object
```

```
$ mcme info myworld
path:     myworld
format:   java
name:     World Created By Amulet
data:     DataVersion 3463
played:   1791643789
leveldat: gzip
regions:  2
chunks:   5
```

Exit codes:

| Code | Meaning |
|---|---|
| `0` | A world was identified |
| `2` | The path exists but is not a world |
| `4` | The path is not a readable directory |

`2` and `4` are distinguished on purpose so a script can tell "wrong directory"
from "no permission".

Fields vary by edition and only appear when present. Java reports
`data: DataVersion <n>`; Bedrock reports `version:` from its
`lastOpenedWithVersion` tuple, printed in full (`1.10.0.0.0`) rather than
truncated to a plausible-looking depth. `leveldat:` reports how the file was
framed, which is the most useful clue when a world is mis-detected.

For Bedrock, chunk inventory comes from the LevelDB store: `keys:` is every
live key, `chunks:` is the distinct (dimension, x, z) that any chunk-marker key
marks. Several markers can address one chunk, so the two numbers differ and
neither is derivable from the other. A store that cannot be opened prints
`database: present, unreadable: <reason>` rather than a chunk count of zero —
zero would say the world is empty, which is the one thing that is not known.

### `mcme chunks`

What chunks a world contains. Counts distinct chunk coordinates; for Bedrock a
chunk counts once no matter how many marker keys address it, and the count is
grouped by dimension because a store mixes dimensions in one flat keyspace.

```
mcme chunks <world>            # text summary, per-dimension breakdown
mcme chunks <world> --count    # one number and nothing else
mcme --json chunks <world>     # path, format, keys, chunks_by_dimension
```

```
$ mcme chunks mybedrock
format:   bedrock (leveldb)
keys:     23
chunks:   9
by dimension:
  overworld  5
  nether     2
  end        2
```

`--count` prints a bare number so `mcme chunks w --count | xargs ...` needs no
parsing. Under `--json` the number is a field instead; the two are not printed
together.

Exit codes: `0` counted, `1` usage, `2` not a world, `4` store present but
unreadable.

Chunk markers are the subchunk prefix in both spellings the game has used
(0x2c pre-1.18, 0x2f after) and the chunk-version record (0x76). Tags that
ride along with a chunk — 0x2d biome/heightmap — are not markers and are never
counted.

### `mcme region`

What one Java region file holds: which chunks, how each is compressed, how
large its payload is, and when the file says it was written. The container
only — no chunk's NBT is decompressed or parsed, so the listing is what the
sector table says, not what the chunks contain.

```
mcme region <world> <rx> <rz> [--dimension <name|number>]
```

```
$ mcme region myjava 0 0
myjava/region/r.0.0.mca: 4 of 1024 chunks, 24576 bytes
  chunk (0, 0)      zlib       248 bytes  2026-10-10T14:49:49Z
  chunk (1, 0)      zlib       252 bytes  2026-10-10T14:49:49Z
  chunk (0, 1)      zlib       252 bytes  2026-10-10T14:49:49Z
  chunk (31, 31)    zlib       251 bytes  2026-10-10T14:49:49Z
```

`(rx, rz)` is the region's own coordinate — the file is
`r.<rx>.<rz>.mca` — and every row is a world chunk coordinate, so the
listing joins against anything else that speaks in chunks without doing the
times-32 arithmetic. An empty region is an answer, not a fault: `0 of 1024
chunks` is what a file with an all-zero header holds, and it lists on exit 0.

`--dimension` picks the Java directory the way the game lays it out: the
overworld is `region/`, the nether `DIM-1/region/`, the end `DIM1/region/`.
Java stores nothing else, so any other number is usage rather than a
directory mcme would invent.

The compression is the file's own byte — `gzip`, `zlib`, `stored`
(uncompressed), `external` (the payload names a file beside the region) — and
a byte mcme does not know is printed as the number it is: the inventory
answers what the file says, not what mcme can open. `bytes` is the payload's
own length, and `timestamp` the file's seconds since the epoch, printed as
UTC.

Under `--json`:

```json
{"path":"myjava/region/r.0.0.mca","rx":0,"rz":0,"chunks":4,"bytes":24576,
 "entries":[{"x":0,"z":0,"compression":"zlib","bytes":248,
             "timestamp":1791643789}]}
```

`entries` is in the file's own slot order. Exit codes: `0` listed, `1`
usage (bad coordinate, a dimension Java has no directory for), `2` not a
world, not a Java world, or no region file at that name, `3` the region
file is not a format mcme reads — shorter than the header, an entry
pointing past its end, or a present slot whose payload header will not
read — `4` store error.

### `mcme region compact`

Rewrites one region file packed: every live chunk copied to its final
place in slot order, every dead byte — payloads that writes replaced,
holes, tails no entry claims — gone. Silent on success; the inventory
reports the new sizes.

```
mcme region compact <world> <rx> <rz> [--dimension <name|number>]
```

Each chunk is copied as its raw frame — length, compression byte,
compressed data — so compaction never decompresses anything: gzip,
zlib, stored and external payloads ride through byte-identical, and
timestamps are carried over. The sector counts written are the counts
that are true — the number of sectors each payload actually occupies,
not whatever the file was carrying.

The file changes by rename: the new one is written beside the old as
`r.<rx>.<rz>.mca.tmp` and renamed over it, so a reader finds the old
file or the new one, never a half-packed one, and a failure leaves the
original untouched. `compact` is matched before the world path, like
`chunk set`: a world directory named `compact` reads as
`./compact` for the other verbs.

A chunk whose frame mcme cannot read refuses the whole file: a
compactor that skipped what it could not parse would drop it.

Exit codes: `0` compacted, `1` usage (bad coordinate, a dimension Java
has no directory for), `2` not a world, not a Java world, or no region
file at that name, `3` the region file is not a format mcme reads —
the header, an entry, a payload header, or a chunk too big for a slot
to claim, `4` store error.

### `mcme region chunk`

One Java chunk read out of its region file: the chunk's own NBT
document, printed the way `mcme nbt dump` prints a file — SNBT by
default, the JSON view under `--json`, so both commands feed `jq` the
same tree. What is printed is the world's own: nothing added, renamed
or reordered. Its write half is `mcme region chunk set`, below.

```
mcme region chunk <world> <cx> <cz> [--dimension <name|number>]
```

`(cx, cz)` are chunk coordinates — the unit `mcme chunks` counts in,
not block coordinates. The region file and the slot follow from them
by the game's own sharding rule, floor(chunk / 32): chunk (-1, -1) is
read from `r.-1.-1.mca` at slot (31, 31), where the inventory lists
it. `chunk` is matched before the world path, so a world directory
named `chunk` reads as `./chunk`.

The chunk is the unit: a payload that will not inflate, a
compression byte mcme does not know, or NBT that will not decode
refuses the whole answer rather than printing what survived — any
part of which mcme cannot read is exit 3.

Exit codes: `0` printed, `1` usage, `2` not a world, not a Java
world, no region file at that name, or no chunk stored at those
coordinates, `3` the chunk is stored but is not a format mcme reads,
`4` store error.

### `mcme region chunk set`

Writes one Java chunk back: the SNBT document replaces the chunk's
payload in its region file. Silent on success, like every other
write verb.

```
mcme region chunk set <world> <cx> <cz> <snbt-document>
                      [--dimension <name|number>]
```

The document is the read verb's own output, so the loop an agent
runs is read → edit → write: `mcme region chunk` prints the SNBT,
the edit happens in between, and the same text goes back in here.
The root must be a compound — a chunk is one. SNBT carries no root
name, so the written document gets the empty one the modern game
writes.

What is actually written: the new payload is compressed as zlib —
the codec Java writes — appended after the file's current end
(zero-padded to a sector boundary first), and the slot's location
entry repointed at it, timestamp set to now. Every other byte of
the file stays exactly as it was; the old payload becomes slack for
a future compactor to reclaim. A slot the world has not stored
gets a chunk, and a region file that does not exist is created with
its 8 KiB header — but the region directory for that dimension must
already be there: mcme writes into a store the world has, it does
not invent one, and it will not rewrite a region file it cannot
read.

`set` is matched before the world path, like `chunk entity set`:
a world directory named `set` reads as `./set` for the other
verbs.

Exit codes: `0` written, `1` usage (bad coordinate, unparseable or
non-compound document, a dimension Java has no directory for), `2`
not a world, not a Java world, or no region directory for that
dimension, `3` the region file exists but is not a format mcme
reads and so will not be rewritten, `4` store error.

### `mcme chunk set`

Sets one block in a Bedrock or Java world. The first editing verb (D3:
anything worth doing is a verb) and deliberately the smallest one: one
coordinate, one block, no undo.

```
mcme chunk set <world> <x> <y> <z> <block>
```

The block is a namespaced name, optionally with SNBT states in braces:

```
$ mcme chunk set mybedrock 12 64 -30 'minecraft:stone'
$ mcme chunk set mybedrock 12 64 -30 'minecraft:oak_log{pillar_axis:"x"}'
$ mcme chunk set mybedrock 12 64 -30 'minecraft:netherrack' --dimension nether
```

States must parse as one SNBT compound; the whole spec is otherwise unvalidated
— mcme has no block registry and stores the name as given, the way the
reference tools do. The version written for a new palette entry is 17694720
("1, 14, 0, 0"), amulet's default for a block that arrives without one.

`--dimension` names the dimension: `overworld`, `nether`, `end`, or a raw
number (the store's own dimension codes). Without it, the overworld. A zero
dimension is omitted from the key the way the game omits it, so naming the
overworld changes nothing on disk.

A subchunk the world has not stored is created: all air, in the version its
neighbours use — 9 when a neighbour carries the Y index, 8 otherwise, which is
what the reference writes for a new subchunk — and then edited like any other.
mcme writes that payload and nothing else: not the version record, not the
heightmap, not the other keys a real save touches, so this edits a stored world
rather than generating one. A subchunk the game deleted is not resurrected: it
is "not there", exit 2. A no-op edit — the same block that is already stored —
is exit 0 and leaves the payload byte-identical.

The store is opened with the game's own LOCK for the write, so a world
Minecraft has open is refused, like `db put`.

A Java world is edited the same way, through the chunk's palettes. The block
replaces what its position points at — reusing an entry the section's
palette already holds when the block is one, appending a new entry when it
is not — and the section's long array is repacked at the bits that palette
needs. A section the chunk does not list is created and kept in Y order —
except for air, which a position no section covers already holds, so that
edit invents nothing — and a section stored without `block_states` starts as the all-air palette the
format says it is, so the first edit into an untouched section makes a
palette of air plus that block and every other position in the section keeps
its air. The whole chunk document is then re-encoded, compressed and
appended to its region file the way `region chunk set` writes, and a no-op
edit — the block already stored there — is exit 0 with the region file
untouched, the same honesty this verb keeps for Bedrock. A chunk the region
file does not have is not resurrected: a chunk document carries bookkeeping
the game would have to trust (`Status`, `Heightmaps`, the rest), and mcme
will not invent it, so it is "not stored", exit 2. Java worlds take
`--dimension` the way the game lays the directories out — overworld
`region/`, nether `DIM-1/region/`, end `DIM1/region/` — and take no LOCK,
since region files are not locked.

Exit codes: `0` set, `1` usage (bad coordinate, bad block spec, a Y the
format cannot address), `2` not a world, not a world that has the kind
asked for, a subchunk the game deleted (Bedrock) or a chunk the region file
does not have (Java), `3` the stored payload is not a format mcme reads,
`4` store error
(including the world being open elsewhere), `6` internal. Success prints
nothing.

### `mcme chunk get`

Prints one block of a Bedrock or Java world as the same spec `chunk set`
accepts, so
`set` followed by `get` prints what went in and an agent can verify its own
edit. The printed text can be passed straight back to `set`.

```
mcme chunk get <world> <x> <y> <z> [--dimension <name|number>]
```

```
$ mcme chunk get mybedrock 12 64 -30
minecraft:oak_log{pillar_axis: "x"}
```

The text is canonical — SNBT as mcme's printer emits it, with the printer's
spacing, which is not necessarily the spacing that was typed — so compare
against a `get` of the same block, not against hand-typed text. The palette
entry's version is not printed: the spec syntax has no version field, and the
version mcme writes for entries it adds is a constant.

The same verb answers for a Java world: the block comes from the chunk's
section palettes — the 1.18+ layout, `sections` at the root, each with its
absolute Y and a `block_states` palette — and prints in the same spec form.
A position no stored section covers, or a section stored without
`block_states`, is `minecraft:air`, which is what the format says it holds.
A chunk the region file does not have is "not stored", exit 2 — the same
answer Bedrock gives for a missing subchunk.

Under `--json` the same text is a field, escaped, with the coordinates
echoed: `{"block":"minecraft:stone","x":12,"y":64,"z":-30}`.

Exit codes: `0` printed, `1` usage, `2` not a world, not a world of the kind
the world turned out to be (Bedrock and Java both answer; nothing else
does), or the subchunk (Bedrock) / chunk (Java) is not stored, `3` the
stored payload is not the format mcme reads, `4` store error. Reading does
not take the LOCK — the game can hold the world open.

### `mcme chunk fill`

Sets a box of blocks to one block — the whole box, in one command, one
payload write per subchunk the box touches (Bedrock), one repack per
section of every chunk the box touches (Java).

```
mcme chunk fill <world> <x1> <y1> <z1> <x2> <y2> <z2> <block>
                                  [--dimension <name|number>]
```

```
$ mcme chunk fill mybedrock 12 64 -30 30 70 -12 'minecraft:stone'
```

The corners are given in any order; both are inside the box. A subchunk the
box touches that the world has not stored is created — all air, in its
neighbours' version, then filled — so a box may reach into ground that was
never saved, the same way `chunk set` does. Positions the box only reaches
into keep what they held, and the subchunks' fluid layers are never touched.

The fill checks the whole box first, under the same LOCK it then edits with,
so a subchunk it cannot edit refuses the whole fill — nothing written —
instead of filling up to a hole. What it will not edit: one the game deleted
("not there"), one whose payload is not a format mcme reads, and the all-air
marker, which has no layer to fill. The cost is the subchunks touched, not
the blocks in the box; a box entirely inside one subchunk is one payload
write. There is no transaction across puts, so a subchunk that fails
part-way leaves the ones before it already written.

A Java world is edited the same way at a different grain: the box is
applied to each section of each chunk it touches — one decode and one
repack per section, not one palette edit per block. A section the box
covers end to end lands as the format's own shorthand for one block: a
single-entry palette and no data array. Sections the chunk does not
list are created and kept in Y order, and air keeps the rule `set`
gives it — a position no section covers already holds air, so that
edit invents nothing. What differs is the unit of refusal. Bedrock
fill creates the subchunks it needs; Java will not invent a chunk
document — the bookkeeping (`Status`, `Heightmaps`) would have to come
from somewhere — so every region file the box touches is checked
first, and a chunk the region file does not have refuses the whole
fill, exit 2, with nothing written anywhere. A fill that changes
nothing rewrites nothing: a box already standing in this block leaves
every region file byte-identical. There is no transaction across
chunks either — a chunk that fails part-way leaves the ones before it
already written, the same honesty Bedrock keeps across puts.

Exit codes: `0` filled, `1` usage (bad coordinate, bad block spec, a Y the
format cannot address), `2` not a world, not a Bedrock or Java world, a
subchunk the box touches the game deleted (Bedrock), or a chunk the box
touches the region file does not have (Java), `3` a subchunk the box
touches is not a format mcme reads (or is the all-air marker), or a
chunk the box touches that will not read or is not the format the edit
needs (Java), `4` store error, `6` internal.
Success prints nothing.

### `mcme chunk prune`

Deletes whole chunks — the box names the chunk columns, and the chunk goes
as a unit.

```
mcme chunk prune <world> <x1> <y1> <z1> <x2> <y2> <z2>
                                  [--dimension <name|number>]
```

```
$ mcme --json chunk prune mybedrock 0 0 0 4095 0 4095
{"chunks":169,"keys":1014,"region_files":0}
```

The corners are given in any order, like `chunk fill`, and the box's X/Z
name the chunk columns it reaches — floor division by 16, the rule every
chunk verb speaks. **Y is parsed and ignored**: a chunk is all of its Y, so
the selection is a box of columns and a chunk the box touches is deleted
whole. A column the world has never stored is nothing to delete: no chunk,
no error, and the same box run twice is `0` chunks the second time with no
write at all.

Bedrock deletes every key the chunk owns — subchunks in both spellings, the
chunk version record, the biome/heightmap record, block entities, pending
ticks, every fixed-shape record addressed by the chunk's x/z and dimension —
in batches of one log record each, under the LOCK the game takes, so a
world the game has open refuses the whole prune before a key is
tombstoned and no single append comes near the log's ceiling. Keys that
are not fixed-shape chunk records (level metadata, actor records,
portallists) are never touched. Tombstones do not shrink the store; `db
compact` reclaims the space, the way it does after every write.

Java zeroes the chunk's slot in its region file — the format's own
"nothing here", the same bytes a slot the file never had carries — and the
payload bytes it pointed at stay until `region compact` rewrites the file
around what remains. Every region file the box touches is checked first,
like fill: a file that will not read refuses the whole prune, exit 3, with
nothing punched, and a chunk the file does not have is already gone. There
is no transaction across region files — a file that fails part-way leaves
the earlier ones already punched, the same honesty fill keeps. No LOCK:
region files have none.

`--json` prints the counts: `chunks` deleted, `keys` tombstoned (0 for
Java), `region_files` written (0 for Bedrock). Human success prints
nothing.

Exit codes: `0` pruned (nothing to prune included), `1` usage (a bad
coordinate, or a dimension number Java keeps no directory for), `2` not a
world, not a Bedrock or Java world, a dimension the game has not made,
`3` a region file the box touches that will not read (Java), `4` store
error, including the LOCK held by the game (Bedrock), or a region file
that will not write, `6` internal.

### `mcme chunk stats`

What a box of a world is made of — the counts an agent plans an edit with,
before it writes anything.

```
mcme chunk stats <world> <x1> <y1> <z1> <x2> <y2> <z2>
                         [--dimension <name|number>]
```

```
$ mcme chunk stats mybedrock 12 64 -30 30 70 -12
     4913 minecraft:stone
      120 minecraft:dirt
       11 minecraft:oak_log{pillar_axis: "y"}
```

One line per distinct block, `<count> <spec>` (count in a nine-wide
right-aligned column), most frequent first (ties by spec). The spec is the
same text `chunk get` prints, so a line can be pasted back into `set` or
`fill`.

A subchunk the box touches that is not stored counts as its positions of
`minecraft:air` — that is what the world is there, empty — and the count of
such subchunks is reported (`mcme: chunk stats: N subchunks not stored,
counted as air` on stderr, suppressed by `-q`). This is the one verb where
unstored ground is not exit 2: `get` will not read what was never saved, and
`set` and `fill` would create it, so stats simply reports the ground as empty
— which is what a plan needs before writing to it.

Only layer zero is counted — the layer `set`, `get` and `fill` act on.
Waterlogging layers are not blocks and are not counted.

Under `--json`:

```json
{"missing_subchunks": 1,
 "blocks": [{"block": "minecraft:stone", "count": 4913},
            {"block": "minecraft:air", "count": 4096}]}
```

`blocks` is sorted the same way as the text lines. Counts are exact while a
count fits a 64-bit integer; a box that large cannot be counted in practice
anyway.

Exit codes: `0` counted, `1` usage (bad coordinate, a Y the format cannot
address), `2` not a world or not a Bedrock world, `3` a stored payload is not
the format mcme reads, `4` store error.

### `mcme chunk height`

How high the ground is in one column: the topmost and bottommost non-air block
of the column through block (`x`, `z`), plus every subchunk the world stores
for it. The answer comes from the stored subchunk payloads, not from the
world's own heightmap record, and that is what makes it usable right after an
edit — `set` and `fill` do not update a heightmap, so a height read that
trusted one would report the ground before the edit.

```
mcme chunk height <world> <x> <z> [--dimension <name|number>]
```

```
$ mcme chunk height mybedrock 0 0
path:       mybedrock
column:     (0, 0)
dimension:  0
subchunks:  -4 -1 0 1 2 4
surface:    y 79 minecraft:oak_log{pillar_axis: "x"}
floor:      y -64 minecraft:dirt
```

`subchunks` is every subchunk Y the store holds for the column, ascending —
the vertical extent of what was saved, which is not the same as what is
solid: the gaps are ground the world never stored. A subchunk the game
deleted is not listed at all: removed, so no longer part of the answer.
`surface` and `floor` are the topmost and bottommost non-air block of layer
zero, the layer the editing verbs act on; a column whose stored blocks are
all air says `none` for both, and that is an answer rather than an error.

Air means `minecraft:air`, `minecraft:cave_air` or `minecraft:void_air` —
the names the game uses for empty space. Anything else counts as ground,
including fluid blocks.

Under `--json`:

```json
{"path": "mybedrock", "dimension": 0, "x": 0, "z": 0,
 "subchunks": [-4, -1, 0, 1, 2, 4],
 "surface": {"y": 79, "block": "minecraft:oak_log{pillar_axis: \"x\"}"},
 "floor": {"y": -64, "block": "minecraft:dirt"}}
```

`surface` and `floor` are `null` when the column holds no non-air block.
`x` and `z` are the block coordinates asked for, echoed; the column is the
single 1-wide stack through them, so two columns of one chunk differ.

The whole signed subchunk-Y range is probed by point lookup, so a column
answer costs 256 reads against a store that may hold millions of keys, and
never a scan. A world that still uses the pre-1.18 subchunk prefix is read
under that spelling.

Exit codes: `0` the column is stored — printed even when every block in it is
air, `1` usage, `2` no subchunk is stored for the column, so there is nothing
to measure, `3` a stored payload is not the format mcme reads, `4` store
error.

### `mcme chunk entity`

The block entity stored at one block position — the chest's inventory, the
sign's text, the spawner's spawn data. Blocks and block entities are stored
separately: the block is in a subchunk, the entity in the chunk's own record,
and `chunk get` reads only the block.

```
mcme chunk entity <world> <x> <y> <z> [--dimension <name|number>]
```

```
$ mcme chunk entity mybedrock 12 64 3
{CustomName: "", id: "minecraft:chest", Items: [{count: 3b, damage: 0s, id: "minecraft:diamond", Slot: 0b}, {count: 64b, damage: 0s, id: "minecraft:stone", Slot: 1b}], x: 12, y: 64, z: 3}
```

The document is printed as the world stores it: the compound verbatim, in
SNBT (the same text `nbt dump` prints, compact) or, under `--json`, as JSON —
which is a view and not a round trip, since JSON has no way to say a number
is a byte or a long. `id`, `x`, `y` and `z` are the entity's own fields;
nothing is added to them. The keys come back in the order the world wrote
them, which is not alphabetical and is not sorted for you.

A chunk's entities are one record holding several documents, so two entities
of one chunk are read by two calls and neither disturbs the other. An entity
belongs to the chunk its own `x`/`z` fall in, so the record it is read from
is the one at `floor(x/16)`, `floor(z/16)`. A document in that record that
carries no `x`/`y`/`z` is not addressable by position and is not matched; a
record any part of which mcme cannot read is exit 3, naming the chunk.
Half a record is never an answer: `set` and `del` rewrite the record as a
whole, and a read that answered from the part before a break would disagree
with what a write is willing to touch.

The subcommands `set` and `del` are matched before the path (see below), so
a world directory that is itself named `set` is read as `./set`.

Under `--json`:

```json
{"id":"minecraft:chest","x":12,"y":64,"z":3,
 "Items":[{"Slot":0,"id":"minecraft:diamond","count":3,"damage":0},
          {"Slot":1,"id":"minecraft:stone","count":64,"damage":0}],
 "CustomName":""}
```

Exit codes: `0` an entity is stored at that block, `1` usage, `2` not a
world, not a Bedrock world, or no block entity at that position, `3` the
chunk's entity record is not a format mcme reads, `4` store error.

### `mcme chunk entity set`

Stores a block entity at one block. The document is an argument in SNBT —
the same form `chunk entity` prints — so read, edit, write back is one
loop.

```
mcme chunk entity set <world> <x> <y> <z> <snbt-document>
```

```
$ mcme chunk entity set mybedrock 12 64 3 '{id: "minecraft:chest", CustomName: "stores", Items: []}'
```

The position is the argument, so it wins: the document's own `x`, `y`, `z`
are set from it, overwriting any they carried, because a document naming
another block would be stored where nothing can find it. `id` must be in
one of the two spellings the game has: the bare savegame id its own
records carry — `"Chest"`, no colon — or the namespaced one —
`"minecraft:chest"`. It is stored exactly as given, letters, digits, `_`,
`-` and `.`, at most one colon with neither side empty; anything else is
exit 1, and so is SNBT that does not parse or a root that is not a
compound.

The game takes either spelling on load — the document survives as a
working container — and rewrites the id to its own spelling (`"Chest"`)
at its next save (measured 2026-10-14; see the dev log). mcme stores
what it is given; the game is the normaliser.

The documents beside it in the record are untouched, byte for byte: the
record is spliced, not re-encoded, so changing one chest cannot reorder
the sign beside it. A chunk with no entity record at all gets one —
the way `chunk set` creates a subchunk the world never stored — and a
record the game deleted counts as none: the chunk holds no entity, and
storing one there is the operation, not a resurrection.

Nothing is printed on success; like `chunk set`, the exit code is the
answer.

Exit codes: `0` written, `1` usage (bad coordinate, SNBT that does not
parse, a root that is not a compound, an `id` that is neither a bare
savegame id nor a namespaced one), `2` not a world or not a Bedrock
world, `3` the record is not a format mcme reads, `4` store error,
including a store the game has open.

### `mcme chunk entity del`

Removes the block entity stored at one block.

```
mcme chunk entity del <world> <x> <y> <z>
```

The documents beside it stay byte for byte, and when it was the last one
the record itself is deleted: mcme does not keep a record holding
nothing, which is also what the game stores — no key at all. Nothing is
printed on success.

Exit codes: `0` removed, `1` usage, `2` not a world, not a Bedrock
world, or no block entity at that block, `3` the record is not a format
mcme reads, `4` store error.

### `mcme db`

Raw key/value access to a Bedrock store. This is the write path's CLI surface
(D3: anything worth doing is a verb) and it is deliberately raw — Bedrock keys
carry binary chunk coordinates, so keys and values are hex on the command
line.

```
mcme db get <world> <key-hex>             # print value-hex, or exit 2
mcme db put <world> <key-hex> <value-hex>
mcme db del <world> <key-hex>
mcme db compact <world>                   # flush the log into one new table
```

```
$ mcme db get mybedrock 656467652f73696e676c65
6e6577657220696e20746865206c6f67
$ mcme db del mybedrock 656467652f73696e676c65 ; echo $?
0
```

`get` opens read-only and does **not** take the store's LOCK, so it works on a
world the game has open. `put`, `del` and `compact` open writable, take an
exclusive `flock` on `LOCK` (the same lock Minecraft takes), and refuse with
exit 4 if the store is locked. Writes append to the store's log as one
WriteBatch record each; past 4 MiB of log the next write flushes everything
into a new SSTable first (compaction, see
[decisions.md](decisions.md)#D11), so a long session does not depend on
anyone remembering to run `compact`. `compact` is that flush as a manual verb:
the same view, one table, no logs. A write past 32 MiB with compaction failing
(disk full, permissions) refuses rather than growing a store only that session
could read.

Exit codes: `0` ok, `1` usage, `2` key absent (for `get`), `4` store locked or
unreadable.

### `mcme convert`

Converts a world into another format. One pair converts today: a Bedrock
world to Java 1.20. The output is a new world the game can open —
`<out>/region/` with the chunks rewritten as Java sections and
`<out>/level.dat` carrying the seed, name, spawn, game type and difficulty
across, so the terrain the game has not generated yet comes from the same
seed.

```
mcme convert <in> <out> --to <platform>/<version>
```

```
$ mcme convert mybedrock out --to java/1.20
converted 2 chunks (6 subchunks, 1908 blocks) to java/1.20
dropped: 1 subchunk mcme cannot read, 1 water layer, 9 other keys
$ mcme --json convert mybedrock out2 --to java/1.20
{"from":"bedrock","to":"java/1.20","chunks":2,"subchunks":6,"blocks":1908,"skipped":{"block_entities":0,"chunk_data":1,"entities":0,"legacy_subchunks":1,"other":9,"water_layers":1}}
```

`<in>` is probed like every other verb and must be a Bedrock world. `<out>`
must **not** exist: convert writes a new world and never mixes into an old
one, so a path that is already there refuses instead of being touched. On a
failure part-way, whatever `<out>` got is removed before the error stands —
an output world is either whole or absent, never a directory someone could
mistake for a finished conversion.

Each overworld chunk of the store becomes one Java chunk: layer zero of every
subchunk through the block map — blocks and states both editions spell the
same pass through, states Bedrock spells differently are renamed to Java's
(`pillar_axis` to `axis`, `minecraft:sand` with red sand to
`minecraft:red_sand`), and a state the map does not know takes Java's default,
the same choice the Python reference makes (see
[decisions.md](decisions.md)#D12). Sections are written in Y order with a
plains biome and `Status: minecraft:full`; `Heightmaps` is not computed yet
(the gap is logged in [dev-log.md](dev-log.md)).

What the map cannot carry it drops **and reports**, never failing the run for
it: block entities, entities, tick lists, the second (waterlogging) layer of a
subchunk, subchunks whose payload this build does not decode (the pre-palette
versions), chunks outside the overworld, and the store's own bookkeeping keys.
A world full of chests converts; the report says what was left behind, and the
blocks are air in the output where the input could not be carried.

Exit codes: `0` converted (a conversion that dropped things still converts),
`1` usage (malformed `--to`, `<out>` exists, `<out>` inside `<in>`), `2` `<in>`
is not a world, `3` `<in>`'s level.dat is missing or not gzip NBT mcme
can parse,
`4` store, directory or region-file read/write failed, `5` the target is
recognised but not built yet — any other `<platform>/<version>` pair, or a
Java `<in>`, converts in a later entry, `6` internal.
The `--json` shape:

```json
{"from": "bedrock", "to": "java/1.20",
 "chunks": 2, "subchunks": 6, "blocks": 1908,
 "skipped": {"block_entities": 0, "chunk_data": 1, "entities": 0,
             "legacy_subchunks": 1, "other": 9, "water_layers": 1}}
```

`chunks` is overworld chunks written, `subchunks` payloads read and decoded
(all-air markers included; the ones that failed are under `skipped`),
`blocks` non-air positions the output holds. `skipped.chunk_data` counts
chunk keys that are not subchunks — biome records, chunk versions, entity
lists; `skipped.other` counts everything else in the store, level metadata
and other dimensions among it.

### Reserved, not yet implemented

Parses and then exits `MCME_EXIT_NOT_IMPLEMENTED`. It exists so the surface is
settled early and tests can assert the gap. A well-formed target
`convert` cannot build yet is this same answer, exit 5.

## Writing agent-friendly commands

When adding a command, do all four:

1. Give it an `--json` shape and document it here before implementing.
2. Pick the narrowest correct exit code from the table.
3. Make it work on a path argument, not a GUI dialog.
4. Add a case to `tests/unit/` that runs it and asserts the exit code.
