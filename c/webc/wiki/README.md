# webc wiki

This directory is the project's **memory**. It lives in the repo on purpose: the
history of *why the code is the way it is* is as much a deliverable as the code.

If you are an agent or a human picking this up cold, read in this order:

1. [simplexity.md](simplexity.md) — the rules we build under. Read before writing anything.
2. [dev-log.md](dev-log.md) — what is done, what is next, what broke.
3. [decisions.md](decisions.md) — one file per architectural decision, with the
   rejected alternatives. Check here before inventing a new pattern.

There is no `porting/` directory. The reference map of the original project
lives in [decisions.md](decisions.md) where each choice was made, and in the per- phase
pages below; the parity checklist promised by earlier drafts of this page
was never written, and a link to a page that does not exist is worse than
no link.

## How to keep this wiki honest

- **A decision is not a decision until it is written down here.** If you changed
  something and did not update the wiki, the change is not finished.
- **The dev log is append-mostly.** Add a dated section at the top. Do not rewrite
  old entries; if something turned out wrong, say so in the new entry.
- **Status means "compiles and is covered by a test",** not "the file exists".
- Keep pages short. If a page is long, it is really two pages.

## Map

| Page | What it answers |
|---|---|
| [simplexity.md](simplexity.md) | What rules constrain the code we write? |
| [dev-log.md](dev-log.md) | Where are we right now? |
| [decisions.md](decisions.md) | Why did we do it this way and not that way? |
| [cli.md](cli.md) | What is the command-line contract for users and agents? |

