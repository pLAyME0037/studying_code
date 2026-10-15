# Simplexity

The project rule. Not "keep it simple" as an excuse to skip the work —
**the work is genuinely complex, so the code must not add more.**

A Minecraft world editor has irreducible complexity:

- ~75 distinct chunk serialisation formats across Java and Bedrock versions.
- A block-translation graph spanning ~100 game versions.
- A voxel mesher that must be fast enough to run every frame of movement.
- A GUI with a live 3D viewport, selections, and undo across all of it.

None of that can be wished away. Simplexity says: **solve exactly that much,
and stop.** Every line that is not solving one of those four problems is a line
that makes the real problems harder to see.

## The rules

1. **Plain C, no cleverness.** `if` beats a table of function pointers until you
   have 20 cases. Recursion beats a hand-rolled stack. A struct with 6 fields
   beats a struct with 6 fields and an inheritance shim.

2. **No framework a human did not write.** We vendor single-file libraries
   (stb, miniz, lz4, cJSON, nob) and call system libraries (GTK4, leveldb).
   We do not build a widget framework, an ECS, a job-graph engine, or a
   generic "format plugin system" ahead of needing one.

3. **The CLI is the product; the GUI is a client of it.** Anything worth doing
   must be reachable from `mcme <verb>`. If a feature only exists behind a
   button, it is not testable and therefore not finished.

4. **Write the simple thing first, then measure.** Do not pre-optimise the
   mesher with SIMD before a correctness test exists. Do not add an LOD system
   the original does not have (`_create_lod1` in the Python is a `# TODO` stub).

5. **Prefer fewer, larger files over many, tiny ones.** File count is not
   modularity. A 400-line `region.c` that does one job is simpler than five
   80-line files that share state.

6. **No speculative abstraction.** Two similar things are two things. The third
   time, consider factoring. Never build the abstraction before the third
   caller exists.

7. **The caller owns the memory, and the docs say who frees what.** Any function
   that returns a pointer must document the owner. Ambiguity here is the single
   biggest source of bugs in a C port.

8. **Errors are values.** Fallible functions return `bool`. There is no
   `longjmp`-based error handling and no hidden global error state beyond the
   one `mcme_last_error()` string for human-readable context.

9. **Test the parser before the pretty part.** NBT, SNBT, `.lang`, JSON and the
   region headers get tests and fuzz targets before any pixel is drawn.

10. **If a page in this wiki is needed to understand a function, the function
    is too clever.** Rename or restructure until it reads on its own.

## What Simplexity explicitly allows

- Long functions when they are linear and read top-to-bottom.
- Duplicating a small amount of logic to avoid a bad abstraction.
- Storing things as text (JSON) when a binary format would be "faster".
- Shipping a feature that works for one version before it works for all of them,
  as long as the limitation is logged in [dev-log.md](dev-log.md).

## What it forbids

- A "core framework" written before the second subsystem needs it.
- Macro magic beyond the handful in `core.h` that earn their keep.
- cleverness that needs a comment to explain the control flow.
- Leaving a TODO that has no issue reference in the dev log.
