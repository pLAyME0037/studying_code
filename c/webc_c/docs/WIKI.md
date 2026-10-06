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

## POS phase status (2026-10-04)
- Phase 0 (baseline + wiki) DONE 6bda49f3. Phase 1 (0003_pos_eshop: 3
  dialects, users swap with dup dedup, dict/language seeds, FK-off
  migration txn in db.c, golden pin) DONE 9249a993.
- Phase 2 DONE: MD_Cell + md_col_slot() slot map in master_child.h (SELECT
  expands parts; every values/disp index goes through the map), cells.c
  stack/avatar renderers, composite branches in display/create/edit/add-row
  templates, showcase pages /pos/locations + /pos/users (+8 route_new
  lines), test/pos_test.py (37 checks). Suite: 178 PASS.
- serve.c fix shipped with Phase 2: request-body NUL byte no longer counted
  into sb_to_sv(sc.body) -- it used to leak into the LAST urlencoded form
  value (Svay Pak -> 'Svay Pak\0'). C-string users still see items[count].
- Phase 3 DONE: style pass on shared components (master_child,
  md_child_rows/add_row/tab, pagination, sidebar): no `rounded` left in the
  data area (logo keeps rounded-full), inputs px-0.5 py-1.5 / buttons p-1 /
  cells pl-3 py-1, hierarchy via bg steps (header gray-100/slate-100, rows
  white -> hover gray-100, child panel gray-50, edit row amber-50, dark
  900/800/700) + hairline dividers (border-b/divide-y), no m*/p* nesting
  (field mb-3 dropped for grid gap, edit form padding single-sourced in the
  td). Suite: 178 PASS.
- Phase 4 DONE: MD_MasterConfig.soft_delete (loader WHERE live/trash,
  ?deleted=1 trash view: red tint + banner + restore buttons, restore
  redirects back to ?deleted=1) + MD_ChildTab.soft_delete (child loader),
  SERVE_SOFT_DELETE macro (module SQL: stamp/clear deleted_at, cascade
  triggers fire -- verified user_roles via trg_soft_del_users/restore);
  MD_MasterConfig.read_only gates create/edit/delete/add-child UI and the
  module registers only the GET route -- showcase /pos/dictionaries.
  Suite: 204 PASS (pos_test now 63).
- Phase 5a DONE: CATALOG modules in src/pos/ (categories.c, products.c,
  variants.c, stocks.c + pos_util.h pos_sv/pos_num bind helpers).
  /pos/products = 5 cols (name, codes cell {sku,barcode}, prices cell
  {base,cost,tax}, category FK, type FK) + children product_variants +
  inventory_stocks; /pos/categories = self-FK parent select + products
  child tab (md_products_child_columns). Child forms carry fk + redirect
  in the query string (opt fields: body first, query fallback, absent ->
  SQL NULL); UPDATE = COALESCE(NULLIF(?,''), col) everywhere so a child
  edit form cannot blank columns it does not carry (cost: cleared
  nullable fields keep their stored value). Child trash now complete:
  loader honors ?deleted=1 per child tab, child rows get tint + restore
  button, add-child gated in trash view. Nullable FK selects: required
  gated on !nullable, "None" option in create/edit/add-row selects.
  Cascade verified in tests (product delete/restore stamps+restores
  variants & stocks via trg_soft_del_products/trg_restore_products).
  Standalone child restore = route-only when the table has no master
  page (variants); FK option lists still include deleted rows (engine
  limitation). Suite: 256 PASS (pos_test 115).
- Phase 5b DONE: INVENTORY — /pos/stocks master (5 cols: product/org/
  variant FKs, qty, {min,max} range cell) + stock_ledger child tab, both
  in src/pos/stocks.c (view) and src/pos/ledger.c (ledger handlers);
  stock_id via query opt. update_pos_stock now also updates product_id
  via COALESCE (master edit may move stock; child edit keeps it). Fixed
  ledger create binder field-order bug (opt stock_id = fields[4], not
  fields[0]). Suite: 281 PASS (pos_test 140).
- Phase 5c DONE: SALES — /pos/orders (6 cols: order#, org/staff/
  customer/status FKs + 4-part amounts cell; children order_items,
  payments, deliveries), /pos/shifts (org/staff/status + 3-part cash cell
  + notes), /pos/finance (read_only, GET only). Files: orders.c (view +
  handlers + child column shapes), order_items.c, payments.c,
  deliveries.c, shifts.c, finance.c; 23 routes. Engine: MD_Column gains
  `fk_where` (raw WHERE fragment scoping option queries -- dict selects
  now show only their category; same additive precedent as `cell`),
  restore button gated on !read_only (no dead button on read_only+trash).
  CHECK-default fallbacks in binders: blank status -> COMPLETED / pending /
  OPEN. Fixed create_order_item opt-field index bug (unit_cost got the
  quantity; assert now pins price/qty/total + defaults). Suite: 325 PASS
  (pos_test 184).
- Phase 5d DONE: PARTY — /pos/customers (tier FK CUSTOMER_TYPE, points,
  Since; children users + customer_interactions with {kind,payload} event
  cell), /pos/staff (code + {first,last} name cell + user/org/location
  FKs; child cash_shifts via md_shifts_child_columns), /pos/org
  (code/name + ORG_TYPE FK + self-FK parent; children users + staff),
  /pos/users gains Roles + Staff child tabs. Files: customers.c,
  staff.c (owns all 3 staff shapes: master, under-users, under-org),
  org.c, customer_interactions.c, user_roles.c; pos.c users handlers
  extended (phone/customer_id/org_unit_id opts, body-first, query
  fallback); shifts.c staff_id moved to opt (shared master+child
  handler). Test helper child_thead(): anchor on
  `</thead><tbody id="mc-tbody-{table}-` (data-tab lands on the wrong
  header -- tab buttons precede panel contents; a non-greedy regex
  backtracks across </thead> and swallows earlier headers). Suite: 370
  PASS (pos_test 229).
- Phase 5e DONE: ACCESS — /pos/roles (code/name + org FK + description;
  children role_permissions + Members), /pos/permissions (master-only,
  code/name/module). Files: roles.c, permissions.c, role_permissions.c.
  New wrinkle: the same user_roles link is created from TWO views whose
  body/query split is mirrored (under users: role in body, user in
  query; under roles: user in body, role in query) and
  SERVE_EXTRACT_FIELDS is body-only (missing key = 400) -> the roles
  side gets its own handler pair + route prefix /pos/role_members (empty
  fields[] is a hard error under -pedantic). Roles have NO soft-delete
  cascade trigger: links stay live while the role hides (test pins
  this). Suite: 396 PASS (pos_test 255).
- Phase 5f DONE: CONFIG — /pos/i18n (languages: code/name + 0/1 flag
  numerics; child translations via query, UNIQUE(language_id, key)),
  /pos/config (system_configs master: key/value/json/encrypted + since).
  /pos/locations + /pos/dictionaries were already live from the
  showcase phases. Files: i18n.c, translations.c, config.c. Suite: 420
  PASS (pos_test 279).
- Phase 5g DONE: MONITOR — /pos/alerts (user/order FKs + type + payload
  + {seen,sent} flags cell), /pos/audit (read_only: user/table/record/
  action + {old,new} diff cell + when; GET only). Files: alerts.c,
  audit.c. Test pins read_only+trash gating (restore button stays out
  when ?deleted=1). ALL Phase 5 module pages are now live (5a-5g: 21
  routes groups / ~17 pages). Suite: 439 PASS (pos_test 298).
- Sidebar grouped nav DONE: data-driven in display/component/sidebar.h.tt
  (Nav_Item {group,path,label,icon} array in the template prologue, one
  `({ for ... })` loop; non-NULL group renders the color-step header:
  slate-100/800 bg + slate text, no borders/margins). 9 groups in plan
  order (Dashboard, Catalog, Sales, Party, Inventory, Access, Config,
  Monitoring, Demo) / 24 entries; single-path SVG icons (loop emits one
  <path>); active check keeps the "/" special case (NAV_ACTIVE is a
  prefix match, so "/" would match everything). Reports group joins when
  its pages land. http_test +4 checks (header count=9, nav-link=25,
  pos hrefs, single indigo active). Suite: 443 PASS (http_test 99).
- Next: Phase 6-8 reports spike (HTML vs FODT), Khmer A4 PDF/DOC
  (docs/POS_PLAN.md).

## Phase 8 — Reports (Khmer A4 PDF/DOC) DONE (suite 456 PASS)
- Spike PICKED HTML (throwaway in /tmp): html -> soffice honors
  @page A4 (595.304x841.89), embeds NotoSansKhmer, ~1.4s, table
  fidelity ok. Hand-rolled FODT first attempt fell back to Letter
  (master-page plumbing) -> stayed with HTML as the source format.
- core/report/report.{h,c} (new): registry {id, khmer_title, sql,
  khmer headers, footer} with 4 MVP reports -- sales_summary (view
  v_pos_sales_delivery_report), daily_orders (dictionaries join),
  low_stock (inventory+products), product_ranking (SUM group-by);
  builder emits UTF-8 Khmer HTML (shop header from system_configs
  pos.shop_name, fallback "ហាង POS", download date + row count meta);
  extra result columns fall back to index labels, NULL cells -> "".
- Converter quirks (recorded so nobody re-dbugs them):
  * every call wraps soffice in `timeout 25` (no hang -> 502 + log);
  * pdf: ONE step, html opens as Writer/Web -> A4 for free;
  * docx: Writer/Web has NO docx export filter -> html --infilter
    "HTML (StarWriter)" --convert-to fodt (plain text), patch
    fo:page-width 8.5in->21cm / 11in->29.7cm (Writer ignores @page),
    fodt --convert-to docx. Both outputs verified A4 (pdfinfo +
    word/document.xml pgSz 11906x16838 twips);
  * shared persistent profile /tmp/webc_lo_profile (warm starts, no
    per-request litter), per-request mkdtemp workdir removed on both
    paths, WEBC_SOFFICE env overrides the binary;
  * format-string gotcha: "-env:UserInstallation=%s%s--convert-to"
    swallowed --convert-to when the infilter arg was NULL (missing
    space) -> 502 on everything; space added between the args.
- Routes: GET /reports (ROUTE_EXACT index, plain links table) +
  GET /reports/ (ROUTE_PREFIX) parsing /reports/<id>.<ext>; id
  charset-guarded [A-Za-z0-9_-] (traversal -> 404), unknown id/ext ->
  404, soffice fail -> 502 (new reason phrase + new
  http_render_response_attachment helper in serve.c with
  Content-Disposition: attachment).
- Sidebar: Reports group added (10 groups / 25 entries); http_test
  counts updated (header=10, nav-link=26, +href="/reports").
- pos_test +13 checks (index Khmer/links/sidebar, pdf %PDF+attachment
  +font-size, docx PK+attachment, 404s). Suite: 456 PASS
  (http_test 99 / pos_test 311 / mysql 40 / https 6).
- Next: Phase 9 seed data (migrations/0004_seed_pos), then Phase 10
  verification (docs/POS_PLAN.md).

## Phase 9+10 — Seed data + verification DONE (suite 462 PASS)
- migrations/0004_seed_pos/sqlite3.sql (idempotent INSERT OR IGNORE,
  relative timestamps): 5 Khmer locations, 1 org unit, 5 categories,
  50 products (10 nouns x 5 adjectives) + 50 variants + 50 stocks
  (first six <= min -> low_stock report rows), 8 customers, 8 users
  (3 staff + 5 customer-linked -> customer_name in the sales view),
  3 staff, 3 roles / 8 permissions / 11+3 links, 2 cash shifts,
  42 orders across last 14 days (subtotal = SUM(line items), inserted
  items-first since FKs are off during migrations), 84 lines, 42
  payments (every 4th half-paid, every 7th PENDING), 14 deliveries,
  42 credits w/ running-balance window + 6 expenses, 4 alerts, 4
  audit rows. mysql.sql/postgres.sql = documented comment-only skips
  (fragment_has_stmt() skips comment-only fragments - verified).
- ALL seed created_at sit in the PAST (-300..-1 days): demo pages sort
  newest-first, so test/demo rows must keep page 1 of per_page=20.
- Master order flipped id DESC -> created_at DESC in
  master_child.c (the "%s DESC single line"): seed ids 'sd-*' sort
  above random uuids lexicographically and pushed freshly created rows
  off page 1 -> row_and_edit lookups failed. Every master table has
  created_at (views are never master lists) - verified via PRAGMA.
- Test updates for populated tables: 7 empty-state checks -> seed-row
  presence checks; products/stocks page-2 windows 6->20 / 7->20
  (76/77 rows); two ambiguous db_row lookups fixed (staff fixture:
  users LIMIT 1 -> first non-staff user, staff.user_id is UNIQUE;
  alert: alert_type='POPUP' -> raw_payload filter, seed had a POPUP);
  mysql_test history rows 3->4.
- Phase 10 additions: page1/page2 row-id sets disjoint, page=999
  clamps (200, <= per_page rows), seeded products >= 50, sales report
  view >= 40 rows, low-stock rows >= 1, exact 29-table existence
  check (28 migrations + Migrations). Manual browser smoke (webc dev
  walk, A4 PDF open) still not possible here - no desktop browser.
- Suite: 462 PASS (http_test 99 / pos_test 317 / mysql 40 / https 6).

## Phase 11 — Storefront at / + staff auth + checkout-only orders DONE (suite 558 PASS)
- Route split: `/` is the public shop (src/shop: shop chrome via
  render_page_shell, no admin sidebar), admin home moved to `/dashboard`.
  auth_gate(sc) runs at the top of route_request - prefix match on
  /pos /dashboard /reports redirects guests 303 to /login?next=<path>;
  everything else (shop, demo, product/order confirmation pages) stays
  public. next= is sanitized to a same-site path (//evil.com -> /dashboard).
- Auth: salt$sha256(salt+password) via EVP, hash set by 0005 for demo
  staff sd.staff1 / posadmin1 (salt webc2026). Sessions live in
  user_sessions (0005), cookie webc_sid HttpOnly SameSite=Lax Max-Age 7d;
  POST /login mints a C-side uuid sid (webc_uuid, /dev/urandom fallback),
  /logout DELETEs the row + clears the cookie, expired rows reaped on
  login. Only user_type ADMIN + ACTIVE may log in.
- Cart = server-read cookie webc_cart ("id:qty,...", HttpOnly): loader
  validates against live products, drops junk, merges dupes, caps qty 999
  / 40 lines; add/buynow/update/remove are plain form POSTs (303 back),
  so the whole flow works byte-identical without JS.
- Checkout is the only customer order path: POST /checkout ignores every
  posted amount (subtotal/total/unit_price forged fields are read nowhere),
  recomputes from base_price in one txn - validates qty against live
  stock, greedily decrements stock rows + writes negative stock_ledger
  rows (reference ORDER, balance_after), creates/reuses customers+users
  by phone (staff-owned phone -> customers row only, no user hijack),
  org/staff = ORDER BY id LIMIT 1 fallback, order_number WEB-YYMMDD-8hex,
  status PENDING, delivery row carries the guest contact (no payment row),
  cart cleared, 303 to public /order/<uuid>. Over-stock or empty cart
  rolls back with data-checkout-error, orders count untouched.
- Manual /pos/orders/create kept for staff behind the same guard (guest
  POST bounces to /login and changes nothing - asserted).
- sidebar.h.tt: Dashboard href / -> /dashboard, logout action /notes ->
  /logout (nav counts 10/26 unchanged, no test pinned either value).
- Gotchas hit: (1) 0004's sd-ord-040/041/042 got +12..14h FUTURE
  timestamps (+ hours bug) and outranked fresh orders in created_at DESC -
  0005 now pins them to now-3/-2/-1 hour (sqlite-only, idempotent);
  (2) sql_bind maps String_View{data=NULL} to SQL NULL, so a form value
  bound into `? = ''` tautologies MUST be initialized to sv_from_cstr("")
  or the whole WHERE collapses to NULL (fixed in shop index q/cat);
  (3) `pkill -f "webc serve 8091"` matches the pkill command line itself
  and kills the shell - write the pattern with a bracket (809[1]) and keep
  the plain port out of that command.
- Tests: testlib Client got a cookie jar (Set-Cookie store, Max-Age=0
  delete, Cookie replay) + login_as(); pos_test auth section runs FIRST
  (guest 303 -> next= path, bad-login marker, staff login 200 on
  /pos+/dashboard) and the table check is now 30 (0001-0005, +user_sessions);
  new test/shop_test.py runs LAST (88 checks: storefront/search/cat/page
  disjoint, detail 404s, cart ops, checkout happy path + forged amounts +
  stock/ledger/delivery/customer assertions, over-stock/empty rejects,
  guest lockdown + forged create, login/logout incl. old-sid replay);
  mysql history rows 4->5.
- Suite: 558 PASS (http_test 99 / pos_test 325 / shop_test 88 /
  mysql 40 / https 6).

## Phase 12 — Catppuccin theme + language dropdown DONE (suite 587 PASS)
- Theme = token flip, not variant sprawl: css/input.css defines raw
  `--ctp-*` values on `:root` (Catppuccin Latte, light) and `.dark`
  (Catppuccin Mocha), then `@theme inline { --color-*: var(--ctp-*) }`
  so every Tailwind color utility resolves through the variables - one
  `.dark` class on <html> recolors everything, no `dark:` variants needed
  on colors. Tokens: base/mantle/crust (bg steps), text/subtext0/overlay0
  (ink steps), surface0/surface1 (hairlines), `onbase` (text ON accent
  surfaces: Latte accents are dark->light text, Mocha light->dark text,
  one token reads right on both), accents blue/mauve/green/red/peach/
  yellow/teal/lavender. WARNING: never write `text-base` - Tailwind's
  font-size utility of that name wins; accent-contrast text is `text-onbase`.
  All old `.dark` raw overrides (sidebar/nav/scrollbar/tooltip) deleted:
  the vars flip themselves. Active nav = `.nav-link.nav-active` unlayered
  rule (color-mix blue 16% + blue text) keyed off ONE swapped class.
- Switcher: existing js/themeSwitcher.js (localStorage, default light,
  `device` follows OS) drives both documents - 3 buttons in the storefront
  top bar (data-set-theme + onclick window.themeController.setTheme) and
  the 3 kept in header.h.tt; render_page_shell now ships the script in
  <head> (admin full-doc header already had it). No JS -> light stays.
- i18n runtime (core/i18n/i18n.{h,c}): file-static request state bound by
  `i18n_begin(sc)` at the top of route_request BEFORE auth_gate (route_request
  is atomic - coroutines only yield at socket I/O outside it - same safety
  as user_data()). Lazy: cookie webc_lang (charset-validated) -> membership
  in `SELECT id,code,name FROM languages WHERE is_active=1 AND deleted_at IS
  NULL ORDER BY is_default DESC, code LIMIT 50` (langs[0] = default) ->
  fallback default; then load `trans_key,trans_value ... LIMIT 512` for
  active + default (skipped when equal). `tr(key, fallback)` chain: active
  row -> default row -> C literal; READS ONLY, never writes on GET;
  empty DB value skips to the next level. mysql (no 0006 rows) gets C
  literals + `<html lang="km">` and keeps 200s. Static assets never resolve
  (no DB touch unless a page renders text).
- /lang: GET /lang?code=&back= (ROUTE_EXACT) validates charset + active
  membership, sets `webc_lang=<code>; Path=/; SameSite=Lax; Max-Age=1y`
  and 303s to `back` only when same-site (`/` prefix, not `//`), else `/`;
  invalid code -> plain 303, no cookie. i18n_lang_form_html(back) emits
  hidden back + `<select onchange="this.form.submit()">` (2+ langs only)
  + <noscript> submit button. Storefront back = uri+query, admin back =
  current_path (LANG_FORM macro in core/layout/header.c next to HTML_LANG
  for `<html lang>`).
- Restyle was colors-only (structure untouched): src/shop/{shop,cart,
  checkout}.c + core/auth login page now emit tokens (accent buttons
  bg-blue text-onbase hover:brightness-90, card bg-mantle border-surface0,
  cart header bar bg-text, chips active bg-blue / all-chip bg-text),
  sidebar.h.tt + header.h.tt shell tokenized (deep per-cell admin accents
  and reports intentionally untouched - reports stay Khmer).
- Migration 0006_i18n: sqlite INSERT OR IGNORE of 70 keys x lang_km+
  lang_en (km values extracted from the C fallbacks so DB and literal are
  byte-identical), mysql/postgres = comment-only skip (no semicolons in
  comments; precedent 0004), registered in db.c -> history rows 5->6.
  New keys/languages still via /pos/i18n CRUD.
- Tests: shop_test +29 (Phase 12 section: lang="km", theme script + 3
  buttons x2 controllers, lang form/options/selected, Khmer placeholder,
  output.css pins :root+.dark+var()+nav-active, seeded>=140, invalid
  code no-cookie, //evil back -> /, code=en -> cookie + English UI +
  lang="en" persisting across reload, admin header-macro doc); http_test
  + pos_test sidebar asserts updated to `bg-surface0/60 nav-label` and
  `nav-active` (counts 10/26/1 unchanged); mysql Migrations 5->6.
- Suite: 587 PASS (http_test 99 / pos_test 325 / shop_test 117 /
  mysql 40 / https 6).

## Phase 13 — Data truth + CRUD overhaul DONE (suite 749 PASS)
- Migration 0007_users_contact: users.phone NOT NULL UNIQUE (backfill
  phone = username WHERE phone IS NULL), email nullable. sqlite dialect
  drops trg_soft_del_org_units/trg_restore_org_units +
  v_active_users/v_pos_sales_delivery_report BEFORE the users swap
  (ALTER ... RENAME re-validates every trigger/view ref) and recreates
  them verbatim after the 3 users triggers. db.c history 6->7 (real DB
  picks it up on next server start).
- /pos/users = the User master ("Users"): 9 cols = avatar cell (40px
  rounded-full, status ring ACTIVE blue / INACTIVE peach / SUSPENDED red,
  initials placeholder, parts profile_pic,name,username,status with
  part_choices) + phone + email + user_type(USER_TYPE)/org/location/
  customer FK labels + activity cell (computed: Joined/Edited human,
  red DELETED in trash) + actions. usr_fields = {phone,name,username,
  email,profile_pic} phone-first (values[0] guard), opt FKs COALESCE
  defaults ('ACTIVE'/'CUSTOMER'); UPDATE keeps-stored except email may
  clear. Every users form carries required phone (0007).
- Staff/user are MASTER types: staff child tabs gone from /pos/users +
  /pos/org (Roles-only / users-only children now); /pos/staff master
  gained phone + staff_type(Role, STAFF_TYPE) + hire_date, keeps its
  cash_shifts child. SERVE_EXTRACT_OPT_FIELDS: query overrides only when
  it carries the key; body-found-empty keeps "" (driver non-NULL data).
- Auth sidebar: auth_current_user() (lazy, request-scoped, rebound per
  auth_gate) -> SIDEBAR_AVATAR_RAW/NAME/SUB (name, role|email|workspace
  label, status-ringed avatar/initials). user_data() hello-world gone.
- Engine: MD_Cell.part_choices (selects from per-part choices; alerts
  flags {0,1} - the contract test proved free text could poison CHECK
  columns), computed cols skipped in forms, !nullable && !DATE ->
  required, DATE -> md_date_input() + md_date_human() display pass.
- Demo /users + /people + User struct/SQL + all test seeds/posts carry
  phone; checkout guest insert email -> NULL (0003 email NOT NULL for
  notes still stands - demo only).
- test/crud_contract_test.py runs LAST in run.sh: sidebar hrefs = page
  inventory; per page scrape data-md-op create form -> fill browser-style
  (bare `required` selects take first non-empty option, optional selects
  submit None, dates empty so DB defaults win, typed fakes, marker only
  into a plain text field) -> POST 302 + persisted; pager-walk to the
  row, submit its update form exactly as rendered -> 302; delete form ->
  302; live count 0. 13 full round-trips, customers/stocks create-only
  (no text field), finance/dictionaries/audit read-only skips.
- Suite: 749 PASS (http_test 99 / pos_test 331 / shop_test 117 /
  crud_contract 156 / mysql 40 / https 6).

## Phase 14 — Enterprise role workspaces (suite 804 PASS)
- Migration 0008_role_workspace: permissions SD.FINANCE + SD.AUDIT
  (both granted to the manager role), demo staff2/staff3 passwords
  seeded (same posadmin1 hash as staff1). mysql/postgres = comment-only
  skip (mysql_test Migrations 7->8).
- RBAC: Auth_User.role_code/staff_id/perms (",CODE," join);
  auth_has_perm() = strstr. perm_gate() runs after auth_gate in route.c:
  19 prefix rules -> styled 403 ("403 - Access restricted", names the
  role + missing code). /dashboard is never perm-gated - it dispatches.
  Order matters: guest 303 login first, then 403 for signed-in but
  under-privileged users.
- Sidebar nav_perms[25] (positional, _Static_assert vs nav_items[25]):
  anon = full nav (public-page pins unchanged); signed-in = per-row
  perm filter (NULL = everyone); group headers hide when all their rows
  are filtered out. "Main Form" -> "Overview" label.
- Dashboard: one template, role_code dispatch from src/dashboard/ -
  manager: 6-KPI strip + priority attention (low stock > stale shift >
  pending > clear, drill-down href) + 7-day CSS bars from a 14-day
  rollup + recent orders; cashier: 4 personal KPIs + My shift card +
  my orders; driver: 3 run KPIs + assigned deliveries queue; generic
  welcome otherwise. Workspace name + Khmer role chip + per-kind quick
  actions in the header row. Data tables free'd after render (loader is
  caller-owned).
- header.h.tt brand "Clinic" -> "POS Admin".
- Tests: role_test.py (47 checks, between shop_test and the contract
  suite) + pos_test manager dashboard pins (+8). Suite: 804 PASS
  (http 99 / pos 339 / shop 117 / role 47 / crud_contract 156 /
  mysql 40 / https 6).

## Gotchas
- String_View: use designated initializers { .data=..., .count=... }.
- String_View with data=NULL binds SQL NULL (not '') - initialize
  form-derived views with sv_from_cstr("") before `= ''` tautologies.
- No sb_append_ch — use sb_append_buf(sb, &c, 1).
- tt-generated headers are hex — grep for hex escapes, not literal text.
- Demo tests assume default per_page=20 shows everything (row counts < 20) —
  keep demo seeds small; POS volume seeds live in POS migration/test only.
- Old notes have user_id NULL -> invisible under any master in /people.
- Never delete resource/image/upload/7155395830974148_1.webp.
- User had uncommitted deletions (core/display/table.c/h + display/table.h.tt)
  + route.c edits as of 2026-10-04 — verify build/tests BEFORE new work.
