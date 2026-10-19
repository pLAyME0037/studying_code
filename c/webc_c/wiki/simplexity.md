# Simplexity

The project rule. Not "keep it simple" as an excuse to skip the work —
**the work is genuinely complex, so the code must not add more.**

A server-side web framework has irreducible complexity:

- Parsing HTTP without leaking or hanging on a hostile client.
- Keeping HTML, SQL and C in sync across three database dialects.
- Rendering pages fast enough that the approach is worth choosing over a
  framework someone else already wrote.
- Allowing a new resource to be added without editing the router, the render
  engine and the schema layer in lockstep.

None of that can be wished away. Simplexity says: **solve exactly that much,
and stop.** Every line that is not solving one of those problems is a line that
makes the real problems harder to see.

## The rules

1. **Plain C, no cleverness.** A `switch` beats a table of function pointers
   until you have 20 cases. Linear code that reads top-to-bottom beats a state
   machine that needs a diagram. A struct with 6 fields beats a struct with 6
   fields and an inheritance shim.

2. **No framework a human did not write.** webc vendors three single-file
   libraries — `nob` (build), the SQLite amalgamation, and a coroutine
   scheduler — and calls two system libraries, OpenSSL and libmariadb. It does
   not build a router DSL, an ORM, a dependency injector, or a plugin system
   ahead of needing one. The router is an array of 20 structs.

3. **Compile the HTML, do not interpret it.** Templates are turned into C by
   `tt` at build time and compiled with the program. There is no template
   engine in the binary, no string scanning per request, and no second
   language to learn at runtime. See [templates.md](templates.md).

4. **Errors are values.** Fallible functions return `bool`. Failure paths use
   `return_defer` into a single `defer:` label rather than nested cleanup.
   There is no `longjmp` and no hidden global error state. Where a message
   needs human context, it is printed at the point of failure with file and
   line (`LOG_SQLITE3_ERROR`).

5. **Generated is generated.** `build/` and `build/h_to_html/` are outputs.
   Nothing in them is ever hand-edited, and nothing in them is checked in as a
   source of truth. If a generated file needs a change, change the input.

6. **No speculative abstraction — and when one slips in, say so.** Two similar
   things are two things; the third time, consider factoring. `src/crud/`
   currently violates this (a declarative CRUD API with no callers), and it is
   recorded as a gap rather than quietly left in place. Honesty about a
   violation costs a paragraph; pretending it is architecture costs the next
   reader an hour.

7. **The caller owns the memory, and the docs say who frees what.** Any
   function that returns a pointer or a `String_Builder` documents the owner.
   `crud_rows_free`, `crud_form_options_free`, `MD_ChildRows` and the pool's
   `db_pool_put` all exist because ownership is stated rather than inferred.

8. **Test the parsing before the pretty part.** Route parsing, form extraction,
   migration history and the HTTP body cap get tests before a page is styled.
   The suites run against a throwaway `$HOME` so no test can damage a real
   database — see [testing.md](testing.md).

9. **Prefer fewer, larger files over many tiny ones.** File count is not
   modularity. `serve.c` at ~620 lines doing I/O, TLS, parsing and response
   rendering is simpler than four files that share connection state by
   convention.

10. **If a page in this wiki is needed to understand a function, the function
    is too clever.** Rename or restructure until it reads on its own.

## What Simplexity explicitly allows

- Long functions when they are linear and read top-to-bottom.
- Duplicating a small amount of logic to avoid a bad abstraction.
- Shipping a feature for one dialect before the others, **as long as the
  limitation is logged** — which is exactly how Postgres is handled: the
  dialect is first-class in schema and queries, the driver is an honest stub
  that fails with a clear message instead of a silent wrong answer.
- Leaving a known wart visible in [dev-log.md](dev-log.md) rather than
  restructuring to hide it.

## What it forbids

- A "core framework" written before the second subsystem needs it.
- Macro magic beyond the handful in `webc_template.h` and `nob.h` that earn
  their keep — and those two files are the entire sanctioned surface.
- Cleverness that needs a comment to explain the control flow.
- A TODO with no corresponding entry in the dev log.
- Two ways to do the same thing (there must not be a hand-written CRUD path
  *and* a macro-generated one both in service; today there is — see
  [dev-log.md](dev-log.md)).
