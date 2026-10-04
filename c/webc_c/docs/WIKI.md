# webc_c — State Wiki (reference)

> Why: project state too large to keep in conversation. Keep this current.
> Repo: /mnt/disk2/mythings/study_my_code/c/webc_c (git repo root is the parent
> c/; sibling webc/ is an older copy — ignore it).

## What this is
C + SQLite/MySQL/Postgres-stub web CRUD app (demo evolving into a POS/e-shop
admin). Custom build (nob), custom template compiler (tt), no framework,
vanilla JS only.

## Build / test
- Build: ./build/bin/nob -> build/bin/webc. New .c files under src/, core/,
  module/ are auto-globbed — no build edits needed (only new js/css/images
  need a build/nob.c resources[] entry).
- Run: webc serve [port], webc dev [port] (auto rebuild+reload); env WEBC_DB,
  WEBC_TLS_CERT/WEBC_TLS_KEY (both or nothing).
- Tests: ./test/run.sh (builds; throwaway HOME+WEBC_DB under /tmp; never
  touches real DB). SKIP_BUILD=1 to reuse build. Suites: http_test.py (sqlite),
  mysql_test.py (own mariadbd), https_test.py. Baseline before POS work:
  **140 passing (94 + 40 + 6)**.
- Real DB ~/.sqlite3/webc/db — NEVER write to it from tests/tools.

## Directory map
- core/http/ — serve.c (hang fix), route.c (route_initialize(): one route_new
  line per page), utils.h (form_find), serve.h (sb_append_html_escaped).
- core/display/ — page renderers. master_child.c/.h = MD engine
  (serve_master_child), people.c = /people composition example,
  paging.c/.h = Page_Info pagination core, user.c/notes.c = demo pages.
- core/layout/ — header.c/footer.c wrappers around layout .tt.
- display/ — .tt templates (build/bin/tt -> build/h_to_html/** hex-escaped C
  OUT("...") literals). ({ C code }) = raw inline C, else literal HTML.
  component/master_child.h.tt, md_child_*.h.tt, pagination.h.tt,
  pager_templates.h.tt, sidebar.h.tt; layout/header|footer|dashboard.h.tt.
- module/webc_template.h — SERVE_READ/EDIT/CREATE/UPDATE/DELETE/CRUD macros
  (per-module glue, NOT a generic CRUD layer).
- src/ — per-entity modules (user/, notes/, dashboard/, crud/ = dead
  leftovers), db/ (sql.h wrapper + drivers, db.c = migrations),
  webc.c (CLI), tt.c (template compiler).
- js/ — ListExpandSwitcher.js (smart master_child: snapshot->fetch->swap->
  reveal), PaginationSwitcher.js (fragment-store paging), sidebarSwitcher,
  themeSwitcher. Bundled via build/nob.c resources[].
- test/ — run.sh, http_test.py, mysql_test.py, https_test.py, testlib.py
  (Client, Checker, db_query = scalar, db_exec), golden/ byte-exact pins.
- migrations/000N_name/{sqlite3,mysql,postgres}.sql — per-dialect, applied in
  ONE transaction => NO PRAGMAs inside files (they run in open_webc_db());
  sqlite files byte-exact for existing DBs (golden-pinned); list = static
  migrations[] in src/db/db.c; golden check list hardcoded in http_test.py
  (~line 80: for mig in ("0001_notes","0002_users")).

## Hard rules (user)
- NO generic CRUD layer; NO sql_each — inline sql_step loops with column++.
- Adding a page/module = own file + one route_new line, zero engine edits.
- Reusable types in master_child.h/.c stay stable (interface directive).
- JS: vanilla only; server byte-identical with JS disabled (no-JS PRG path).
- Style: compact, information-dense, NO modern styling; keep px-0.5 py-1.5
  inputs; do NOT use radius/padding/margin to express nesting — hierarchy via
  BACKGROUND COLOR STEPS (light white/gray-50/gray-100/200; dark
  gray-900/800/700) + hairline borders/dividers.
- README.md belongs to the user — do not touch.

## Pagination contract (shipped, tested)
- SSR first window via SQL LIMIT/OFFSET + count; Page_Info (core/display/
  paging.h); clamps page->[1,total_pages], per_page->[1,100] (default 20),
  page=abc -> 1.
- Nav ALWAYS renders (1 page => Prev/Next disabled spans); attrs
  data-pg-container|key|base|per; page links carry data-pg-n, per-page links
  carry none (always SSR reload).
- Prefetch ?fragment=all (no doctype, has id="pg-store", <template
  data-pg-pager="<container>" data-pg-n="N">); armed only when a real
  data-pg-n link exists. JS slices store by data-pg-unit child groups
  (users/notes=1, master cluster=3, child row=3 incl its <form>), preserves
  tr.md-add-row, purges nested stores after master flip, patchCluster syncs
  holder, window.pgReset/pgArm hooked into mdSwap.
- Row-group units: child row = [md-row tr, edit tr, form] = 3 tbody children;
  master cluster = [display tr, edit tr, panel tr] = 3.

## master_child engine (key facts)
- MD_MasterConfig{table,title,id_column,crud_path,columns,sum,children[]};
  MD_Column{name,label,type,nullable,fk_*,hidden,computed}.
- SELECT list = id_column + each columns[].name; slots = values[0]=id,
  values[i+1] per column (this is the indexing the multi-field-cell phase
  replaces with a slot map).
- Master: SQL window (LIMIT/OFFSET) + count(*); children loaded fully and
  windowed in template. ORDER BY ... DESC comment in core loaders = the
  ordering tweak point (core decides, ordering not a generic layer).
- No deleted_at awareness anywhere yet (soft-delete = planned engine flag).
- No read-only page flag yet (planned).
- Forms: form="..." attr wiring (edit mdf-e-<table>-<id>, add
  mdf-a-<table>-<masterid>); md_fk_display() swaps FK ids -> labels after
  load.

## Smart JS contract (ListExpandSwitcher)
- Submit -> sessionStorage snapshot (md-flash) -> fetch(form.action) ->
  DOMParser -> swap #mc-root -> reopen panels/tabs -> reveal via data-row-id
  diff. mdChildRows scoped tbody[id] > tr[data-row-id].
- mdSwap success: history.replaceState(res.url) + pgReset/pgArm.

## POS full-site milestone (decided 2026-10-04)
- Scope: EVERYTHING in migrations/pos_and_eshop_db_schema.sql (~26 tables, all
  get pages). Auth/login + storefront: DEFERRED.
- users conflict: MIGRATE IN PLACE inside 0003 — create pos-shaped users (+
  keep profile_pic superset; phone NULL-able; password_hash DEFAULT '' for
  demo inserts) -> copy rows -> DROP users -> RENAME users_pos TO users
  (drop-before-rename keeps notes.user_id REFERENCES users(id) intact).
- Dictionary literal-id rows ('CUSTOMER','TEIR_1',...) seeded INSIDE 0003
  BEFORE the users copy (FK defaults reference them).
- Join tables role_permissions/user_roles get surrogate id + UNIQUE pair
  (engine needs single id column) — documented schema deviation.
- Reports: LibreOffice headless (/usr/bin/soffice present, 24 Khmer fonts) —
  C emits HTML or .fodt -> soffice --headless --convert-to pdf|docx -> serve
  with Content-Disposition. Spike decides HTML vs FODT (A4 page control).
- Soft-delete/restore: IN ENGINE NOW (config flag: list filters deleted_at,
  delete sets it, restore clears; triggers cascade; optional show-deleted
  tint).
- Multi-field columns: MD_Column.cell{parts[],style} + slot map + inline HTML
  renderers in src/helper/cells.c (cell_stack = province/district/commune/
  village flex-col color-ranked; cell_avatar = pic/name/username grid); one
  <td> per column kept in display AND edit/create rows.
- Plan file: docs/POS_PLAN.md (phases 0-10)

## Gotchas
- String_View: use designated initializers { .data=..., .count=... }.
- No sb_append_ch — use sb_append_buf(sb, &c, 1).
- tt-generated headers are hex — grep for hex escapes, not literal text.
- Demo tests assume default per_page=20 shows everything (row counts < 20) —
  keep demo seeds small; POS volume seeds live in POS migration/test only.
- Old notes have user_id NULL -> invisible under any master in /people.
- Never delete resource/image/upload/7155395830974148_1.webp.
- User had uncommitted deletions (core/display/table.c/h + display/table.h.tt)
  + route.c edits as of 2026-10-04 — verify build/tests BEFORE new work.
