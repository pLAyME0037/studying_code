# webc wiki

This directory is the project's **memory**. It lives in the repo on purpose: the
history of *why the code is the way it is* is as much a deliverable as the code.

If you are an agent or a human picking this up cold, read in this order:

1. [simplexity.md](simplexity.md) — the rules we build under. Read before writing anything.
2. [architecture.md](architecture.md) — how a request becomes bytes, and where each piece lives.
3. [dev-log.md](dev-log.md) — what is done, what is next, what broke, what is knowingly wrong.
4. [decisions.md](decisions.md) — one entry per architectural choice, with the
   rejected alternatives. Check here before inventing a new pattern.

## Map

| Page | What it answers |
|---|---|
| [simplexity.md](simplexity.md) | What rules constrain the code we write? |
| [architecture.md](architecture.md) | How does a request flow, and what is in which file? |
| [templates.md](templates.md) | How does HTML become a C function? |
| [database.md](database.md) | How do three SQL dialects share one schema? |
| [testing.md](testing.md) | How do we run the app without touching a real database? |
| [cli.md](cli.md) | What is the command-line contract for users and agents? |
| [decisions.md](decisions.md) | Why did we do it this way and not that way? |
| [dev-log.md](dev-log.md) | Where are we right now? |
| [other_wiki_put_here.md](other_wiki_put_here.md) | Placeholder — no page has claimed it yet. |

## How to keep this wiki honest

- **A decision is not a decision until it is written down here.** If you changed
  something and did not update the wiki, the change is not finished.
- **The dev log is append-mostly.** Add a dated section at the top. Do not rewrite
  old entries; if something turned out wrong, say so in the new entry.
- **Status means "compiles and is covered by a test",** not "the file exists".
- **Write down what is broken too.** A wiki that only records successes teaches
  the next reader to trust code that should not be trusted. The known gaps in
  [dev-log.md](dev-log.md) are load-bearing.
- Keep pages short. If a page is long, it is really two pages.

## Conventions used across these pages

- **`display/*.tt`** are hand-written. **`build/h_to_html/**.h`** are generated
  from them and are never edited.
- **`src/crud/`** and the **postgres driver** are in-tree but not wired to
  anything that runs. Both are described as such wherever they appear — see
  [dev-log.md](dev-log.md).
- Line counts and file references were measured on 2026-10-15, not guessed.
