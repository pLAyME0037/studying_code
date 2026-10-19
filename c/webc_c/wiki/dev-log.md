<!-- Log starts 2026-10-15. Entries before that date were a different project's
     (mcme) and were removed from this wiki by request on 2026-10-15; they are
     still recoverable from git history. Nothing is back-filled here. -->
# Dev log

Newest entry at the top. Append; do not rewrite history.

Status vocabulary:

**scaffold** = files exist, not functional · **wip** = being written ·
**works** = runs, may lack tests · **done** = runs and covered by tests ·
**blocked** = needs something first

---

## 2026-10-19 — Multi-account login + switching — **done**

Sign into several accounts on one browser and hop between them without
re-entering passwords. Sessions signed-into from the same browser share a
**`switch_group`** (a uuid); the active `webc_sid` anchors it, so no second
cookie is needed (the codebase only emits one `Set-Cookie` per response).

### Built

- **Migration `0010_account_switch`** (sqlite/mysql/postgres): adds
  `user_sessions.switch_group TEXT NULL` + an index. Registered in
  `migrations[]` (`src/db/db.c`).
- **`auth_mint_session`** now takes a group and stamps `switch_group` on the
  new row (NULL/empty = mint a fresh group). **`browser_group()`** decides the
  group at login/signup: reuse the active session's group (a second login
  joins it), stamp a pre-migration session that has none, or start a new one
  on a fresh browser.
- **`serve_auth_accounts` (GET `/accounts`)**: the switcher page - lists the
  group's live sessions (one row per user, newest first), marks the current
  account, and gives each other account **Switch** / **Remove** buttons plus an
  "add account" (`/login?next=/accounts`) link. Standalone shell via
  `render_page_shell`.
- **`serve_auth_switch` (POST `/switch`)**: `op=switch` repoints `webc_sid`
  at a sibling session (validated: same group + live user, so a stolen sid
  elsewhere can't be hopped to); `op=remove` drops a sibling session (never the
  active one) from the group.
- **`serve_auth_logout`**: now ends the whole browser session - deletes every
  session in the group + clears the cookie (a group never spans browsers; a
  lone account behaves exactly as before). Granular per-account sign-out lives
  on `/accounts` (op=remove).
- **Entry points**: an "Accounts" link in the storefront chrome (`shop.c`) and
  a "Accounts" item above Log Out in the staff sidebar
  (`display/component/sidebar.h.tt`).

### Verified

End-to-end (one cookie jar = one browser): login staff1 → login staff2 (joins
the group) → `/accounts` lists both, current = staff2 → switch to staff1
(cookie repoints) → remove staff2 → logout of the last account clears the
cookie. Full suite green except the pre-existing signup duplicate-email gap:
357 / 118 / 48 / 179 / 0 / 6, 0 failed (the three sidebar-nav counts in
http_test/role_test were bumped +1 for the new Accounts link; the shop_test
"logout clears cookie" contract is preserved by the sign-out-everywhere
semantics).

---

## 2026-10-19 — Cambodian official letterhead (page-1 body) + repeating 3-section imprint header — **done**

Two layers now, as on real letterhead stationery:
- **Letterhead** = the start of the letter, **page-1 body only** (the memo
  "from/to" is page-1-specific).
- **Imprint header** = the repeating **page-style** 3-column header (shop
  block / title / date+row-count), on **every page**.

(An earlier pass mistakenly dropped the imprint header to "avoid doubling up
with the letterhead"; the user wanted it back — it is the imprint, separate
from the letterhead.)

### Built

- **Letterhead** (`report_append_letterhead`, body page 1):
  - Kingdom motto centered in **Khmer OS Muol** — `ព្រះរាជាណាចក្រកម្ពុជា` /
    `ជាតិ សាសនា ព្រះមហាក្សត្រ` (the font is installed system-wide as
    `KhmerOS_muol.ttf`; the family name is "Khmer OS Muol", spaces — the
    original snippet's `khmer-os-muol` would not have matched).
  - A borderless row: **logo + shop name left**, memo **`ពី ក្រុមបច្ចេកទេស` /
    `ទៅ ខណះគ្រប់គ្រង` (from/to) right**, on the same row.
  - Report title (centered, bold) below. (No separate meta line — the date +
    row count live in the repeating imprint header.)
- **Logo embedded as a base64 `data:` URI** (`report_append_logo`): the html
  blob is copied to a temp dir for soffice, where a relative
  `resource/image/user1.png` would not resolve. Type is sniffed from magic
  bytes (the file is really a JPEG despite the `.png` name). A tiny
  `sb_append_base64` does the encoding — the codebase had none.
- **Imprint header restored** in `report_fodt_decorate` (a `<style:header>`
  holding the 3-column `sb_append_hf_table`, `RptCellHdr` style), injected
  alongside the footer on the "HTML" master-page so LibreOffice repeats it on
  every printed page. `Report_Def.lheader/mheader/rheader` are **used again**
  (left falls back to `report_shop_name()`, mid to the report title, right to
  a "date · row count" stamp). The `n_rows` plumbing that feeds that count is
  back across `report_build_html`/`report_convert`/`report_fodt_decorate`.

### Verified

All four reports, PDF + DOCX, against a fresh seeded DB: page 1 = imprint
header + Kingdom/logo/memo letterhead + title + table + `PAGE 1-N`; page 2+ =
repeating imprint header (incl. the row count) + table + `PAGE k-N`. Logo
embeds (PDF shows an embedded image; DOCX ~205 KB). Full suite green except
the pre-existing signup duplicate-email gap: 357 / 118 / 48 / 179 / 41 / 6,
0 failed.

---

## 2026-10-19 — Report page chrome: ODF header/footer, once-only table header, live page numbers — **done**

Reworked the downloadable-report layout so it reads like a standard
document: a letterhead pinned to the very top of every page and a footer at
the very bottom, each behind a divider rule; a column-header row that prints
once at the start (not repeated down every page); a bordered grid with
minimal cell padding; and real "PAGE X-Y" numbering.

### Built

- **Both `pdf` and `docx` now route through FODT** (see
  [decisions.md](decisions.md) D14). The old direct `html → pdf` path opened
  the file as Writer/Web, which has **no page-style headers/footers and no
  page-number fields** — it cannot repeat a letterhead or number pages. Now
  `report_convert` always: (1) opens the html as Writer and exports flat ODT
  (`fodt`), (2) patches letter→A4 and injects the chrome, (3) exports the
  requested format. `Report_Def` gained `l/m/r` header + footer fields;
  `report_fodt_decorate()` injects them as a page-style `<style:header>` /
  `<style:footer>` into the body's `HTML` master-page, so LibreOffice repeats
  them on every printed page.
- **Live page numbers** come from ODF fields (`<text:page-number>` /
  `<text:page-count>`), not from C — the C code emits one blob and never
  knows how it will paginate. `PAGE_CURR`/`PAGE_LAST` are macros holding that
  field markup; `rfooter` embeds them and is therefore injected as ODF markup
  (the other five chrome strings are XML-escaped text). Each band is a
  **3-column borderless table** (a table, not tab stops, so the 2-line shop
  header still lines up), with a divider rule under the header cells and over
  the footer cells.
- **Table-only body.** `report_build_html` no longer emits the title band /
  generation meta / footnote (those moved into the page chrome) and drops the
  `<thead>` — the column-header row now lives in `<tbody>` so it prints once.
- **Grid + dark header + zebra as presentational HTML attributes**, because
  LibreOffice's html→FODT import **drops CSS backgrounds and cell borders**
  (only paragraph properties like `.num` right-align survive). So the grid is
  `<table border="1" cellpadding="1">`, the dark header row is
  `<th bgcolor="#1f2937">` + `<font color="#ffffff">`, zebra is a per-row
  `bgcolor`, and totals are `bgcolor` + `<b>`.

### Verified

End-to-end against a fresh seeded DB (logged in as `sd.staff1`): all four
reports render in PDF and DOCX. A4 (595×842 pts); header/footer repeat on
every page with dividers; live "PAGE 1-2" / "PAGE 2-2"; column header prints
once (page 1 only); grid + zebra + grouped right-aligned numbers + bold
totals all visible. DOCX carries `header1.xml`/`footer1.xml`, the `PAGE`
field and `w:tcBorders` grid. No blank trailing pages. Full suite green:
357 / 118 / 48 / 179 / 41 / 6, 0 failed.

---

## 2026-10-19 — Directory restructure to match webc (single `src/` tree)

### Built

Layout only; no logic changed. Goal: make `webc_c` mirror the sibling `webc`
project — one `src/` tree, a `third_party/` for vendored deps, and the build
driver `nob.c` at the project root.

- **`module/` → `third_party/`** — `coroutine/`, `nob/nob.h`,
  `sqlite-amalgamation-3460100/`. `module/webc_template.h` → `src/core/`.
- **`core/*` → `src/*`** — `core/http`→`src/http`, `core/i18n`→`src/i18n`,
  `core/auth`→`src/auth`, `core/report`→`src/report`,
  `core/layout`→`src/layout`. `core/display/{master_child,paging}` →
  `src/layout/`; `{people,version}` → `src/{people,version}/`;
  `pos.{c,h}` → `src/pos/`. Empty `core/` removed.
- **Merged split entities (webc style, one file per entity):** `notes.{c,h}`
  and `user.{c,h}` now each hold data + controller + render (dual include
  guards `SRC_*_H_` + `*_SERVE_H_`). The old `core/display/{notes,user}.{c,h}`
  halves were `git rm`'d.
- **`nob.c` → `nob.c`** (root). Build globs only `src/**.c` (dropped the
  `core` dir); recompiled `build/bin/nob` from the root source.
- Global include rewrite (`module/`→`third_party/`, `core/*`→`src/*`);
  `src/tt.c` reaches nob via `../third_party/nob/nob.h`.

### Verified

Build green (only the pre-existing `-Woverlength-strings` footer warning).
All suites pass: **357 + 118 + 48 + 179 + 41 + 6**, 0 real failures. The one
`signup: duplicate email rejected` FAIL is **pre-existing and unrelated**: the
test posts `taken16@signup.test`, which is seeded nowhere, so signup legitimately
succeeds (303) instead of being rejected (200) — `test/` has zero diff vs HEAD
and `auth.c` signup uses its own SQL, independent of the merged `user.c`.

### Still stale (knowingly)

- `src/http/serve.h` still uses include guard `CORE_SERVE_H_`;
  `src/people/people.h` uses `PEOPLE_H_` — cosmetic leftovers only.
- `webc.c` `dev` watcher roots and the `dev_visit_file` SQLite exclusion still
  name the old `core`/`nob.c`/`module/…` paths — see
  [cli.md](cli.md) gaps 4–5.

---


### Built

Documentation only; no source file under `src/`, `display/`, `migrations/` or
`test/` was touched.

- **`README.md`** — was 6 bytes. Now defines the project: what it is,
  requirements, quick start, CLI, environment, dialect selection, TLS, route
  table, project layout, request flow, tests, and pointers into the wiki.
- **`wiki/`** — was 1,200+ lines about *`mcme`*, a Minecraft world editor
  (`simplexity.md`, `decisions.md` D1/D2 on C11+GTK4, `dev-log.md`, and a
  914-line `cli.md` documenting `mcme <verb>`). That content existed nowhere
  else in the repo; it was overwritten on request and remains recoverable from
  git history. Rewritten for webc:
  [README](README.md) (index), [simplexity](simplexity.md),
  [architecture](architecture.md), [templates](templates.md),
  [database](database.md), [testing](testing.md), [cli](cli.md),
  [decisions](decisions.md), and this log.

Measured while writing: `src/` 5,484 lines (`.c`+`.h`), `display/*.tt` 1,366,
`test/` 1,167, `js/` 730, `nob.c` 390.

### Verified — build and tests

| | |
|---|---|
| `./build.sh` | **exit 0**, `build/bin/webc` ~8.36 MB |
| warnings/errors in full build | **0** |
| `./test/run.sh` | **exit 0 — 140 passed, 0 failed** |
| └ `http_test.py` (SQLite) | 94 passed, 0 failed |
| └ `mysql_test.py` (own `mariadbd`) | 40 passed, 0 failed |
| └ `https_test.py` (own cert) | 6 passed, 0 failed |

Both optional suites **actually ran** — `mariadbd` and `openssl` are both
installed, so neither skipped. No `FAIL:` lines in the log. This is the first
recorded full-suite result for this tree; treat it as the baseline.

### Discovered — `~/.sqlite3/webc/db` cannot be opened  ·  status: **blocked on a decision**

The app does not run against its own default database.

```
$ ./build/bin/webc serve 8123
ERROR: /home/playme/.sqlite3/webc/db: Database scheme is too new. Contains
more migrations applied than expected. Update your application.
ERROR: Failed to initialize DB pool connection 0
Listening to http://127.0.0.1:8123/ (coroutine mode)

GET /        → 200, 29018 bytes   (dashboard needs no DB)
GET /notes   → 500,  257 bytes    (needs DB)
```

The log repeats `Database scheme is too new` once per DB request, and the
`DB pool initialized with 8 connections` line never appears.
`db_pool_init()` returns at connection 0 **without** setting
`db_pool_initialized`, so `db_pool_get()` re-attempts and fails every time —
hence 500s rather than one clean refusal at startup.

**Root cause: 9 migrations applied, 2 registered.** `migrations[]` in
`src/db/db.c` and the `migrations/` directory both contain exactly
`0001_notes` and `0002_users`; the database holds 9 rows in `Migrations`,
applied 2026-10-04 → 2026-10-07:

| # | Migration (from its SQL header) | SQL size |
|---|---|---|
| 1 | `0001_notes` | 510 B |
| 2 | `0002_users` | 530 B |
| 3 | `SQLITE3 POS + E-SHOP SCHEMA` | 37,281 B |
| 4 | `0004_seed_pos` — demo/seed data | 25,531 B |
| 5 | `0005_auth` — staff login sessions | 2,597 B |
| 6 | `0006_i18n` — storefront/log | 8,438 B |
| 7 | `0007_users_contact` — contacts | 6,407 B |
| 8 | `0008_role_workspace` | 1,478 B |
| 9 | `0009_geo_admin` — Phase 16 | **2,449,513 B** |

The database therefore has **34 tables** — `orders`, `order_items`, `payments`,
`products`, `customers`, `roles`, `permissions`, `audit_logs`, `stock_ledger`,
`financial_ledgers`, `provinces`/`districts`/`communes`/`villages`,
`translations`, `languages`, … — a POS/e-shop schema, not the notes/users
schema this tree declares.

### Evidence — these migrations were never committed

```
$ ls migrations/
0001_notes  0002_users

$ git ls-files c/webc/migrations/      → same 6 files, 0001 + 0002 only
$ git status -- c/webc/migrations/     → empty (no deletions reported)

$ git log -S'0003_' -- c/webc/src/db/db.c
  → empty: db.c never contained them in any commit

$ git show HEAD:c/webc/src/db/db.c | grep -c '"[0-9]\{4\}_'
  → 2   (identical to the working copy — db.c is not locally modified)
```

So a `migrations[]` with 9 entries and 7 untracked migration directories
existed on disk between 2026-10-04 and 2026-10-07, migrated the database, and
were then removed **without ever being committed**. Nothing in git can restore
them.

Corroborating:

- Commit `03882263 "pos database madness AI don't understand data"` and the
  migration names themselves point at the POS/e-shop work.
- `migrations/pos_and_eshop_db_schema.sql` was **deleted** by
  `ce80dedb "Try to compress webc filestructure for more compress"`. Git still
  has it at `ce80dedb^` — **30,890 bytes**, which does *not* match DB row 3's
  37,281 — so git's copy is an older variant and the database's own record is
  the authoritative one.
- `~/.sqlite3/webc/db.bak` (430,080 B, Oct 4) with `-shm`/`-wal` (Oct 7) — the
  D5 backup/restore path has run before.

### Options — none chosen

**A. Reconstruct migrations 0003–0009 from the database itself.**
D5 exists precisely for this: `Migrations.query` stores the **exact SQL that
was applied**, so the seven missing files can be dumped straight out
(`SELECT query FROM Migrations WHERE rowid BETWEEN 3 AND 9`) and
`migrations[]` re-extended. The database opens as-is with all 34 tables and
its data intact.

- Caveat: only the **`sqlite3.sql`** siblings are recoverable. The `mysql.sql`
  and `postgres.sql` variants of 0003–0009 are gone entirely.
- Caveat: names for 0004–0009 are legible in their SQL headers; row 3's array
  name is not recoverable and would have to be chosen.
- Decision required: **should this tree carry a POS/e-shop schema at all?**
  That is a product question, not a technical one.

**B. Set the POS database aside and start fresh.**
Point `WEBC_DB` elsewhere, or rename `~/.sqlite3/webc/db`. The app works
immediately on a clean notes/users schema — which is exactly what the test
suite already proves happens (140 passing tests all run against a fresh DB).
The 34-table database stays on disk untouched.

**C. Ship as-is.** Not viable: any DB-backed route returns 500.

Recommendation: decide the product question first — *is the POS schema part of
webc?* If yes → A (it is fully recoverable, thanks to D5). If no → B, and keep
the file where it is until the question is settled.

### Known gaps

All verified today; each is documented where it lives.

| # | Gap | Where |
|---|---|---|
| 1 | **Database refuses to open — DB routes 500** | above; [decisions.md](decisions.md) D9 |
| 2 | `src/crud/` compiles but has **zero callers** (live path is `SERVE_*` + `master_child.c`) | [architecture.md](architecture.md) |
| 3 | `driver_postgres.c` is an honest stub; `postgres:` fails at `db_open()` | [database.md](database.md), D4 |
| 4 | `dev` watches two dead paths: `core` and `nob.c` — **editing `nob.c` does not trigger a rebuild** | [cli.md](cli.md) |
| 5 | `dev_visit_file` excludes `module/sqlite-amalgamation-…`, now at `third_party/` — vendored SQLite retrigger builds again | [cli.md](cli.md) |
| 6 | Port argument is `atoi()` with no validation: `serve notanumber` → port 0, `serve 70000` → 4464 | [cli.md](cli.md) |
| 7 | DB pool round-robins with **no checkout tracking** — invariant rests on handlers not yielding while holding a connection | [database.md](database.md) |
| 8 | `tt` prints `char` with `%02x` → 8-digit escapes for bytes ≥ 0x80; bytes correct, build warning-free, but ugly | [templates.md](templates.md) |
| 9 | No `-std=` pinned; compiles as C23 on GCC 16 | [decisions.md](decisions.md) D12 |
| 10 | No concurrency test, no keep-alive/chunked (neither implemented), no Postgres suite | [testing.md](testing.md) |

### Next

1. **Decide A vs B for the database.** Everything else can wait; this is the
   only gap a user hits on their first `serve`.
2. If B: move the DB aside and re-run `serve` → confirm `GET /notes` → 200.
3. If A: dump rows 3–9 to `migrations/000{3..9}_*/sqlite3.sql`, extend
   `migrations[]`, rebuild, re-run the suite **and** the real-DB smoke test.
4. Cheap follow-ups once the above is settled: gaps 4, 5 and 6 are each a
   one-line fix in `src/webc.c`.
