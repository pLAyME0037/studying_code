# PLAN: full working POS/e-shop site on webc_c

Decisions (locked 2026-10-04):
- Scope: EVERYTHING in migrations/pos_and_eshop_db_schema.sql (~26 tables, all get pages)
- users conflict: migrate in place inside 0003
- Reports: LibreOffice headless (HTML/.fodt -> pdf|docx)
- Soft-delete/restore: build into master_child engine now
- Auth/login + customer storefront: deferred

Companion: webc_state_wiki.md (architecture state reference)

-----------------------------------------------------------------------
PHASE 0 — Baseline + wiki
-----------------------------------------------------------------------
- Build + run full suite on current tree (user had uncommitted deletions of
  core/display/table.c/h + display/table.h.tt and route.c edits). Expect
  140 PASS, no -Woverlength-strings warning anymore.
- If green, commit current state first (clean starting point).
- Land wiki (move webc_state_wiki.md into repo as docs/WIKI.md).
Accept: suite green before any POS work.

-----------------------------------------------------------------------
PHASE 1 — Migration 0003_pos_eshop
-----------------------------------------------------------------------
- migrations/0003_pos_eshop/sqlite3.sql:
  * schema minus PRAGMA lines (they already run in open_webc_db()),
    keep triggers + views + CHECKs (whole-transaction is fine).
  * ORDER: all CREATE TABLE/INDEX/VIEW/TRIGGER ->
    INSERT seed of dictionary rows with literal ids matching FK defaults
    ('CUSTOMER', 'TEIR_1', status/order/payment codes...) ->
    users swap:
      CREATE TABLE users_pos (POS shape + profile_pic kept,
        phone TEXT NULL UNIQUE, password_hash TEXT NOT NULL DEFAULT '');
      INSERT INTO users_pos SELECT id,name,username,email,profile_pic,
        ... AS phone(NULL), ... AS password_hash('') FROM users;
      DROP TABLE users;                 -- notes FK target removed...
      ALTER TABLE users_pos RENAME TO users;  -- ...name reused again,
                                               -- notes refs stay valid
  * deviations documented: phone nullable, password_hash default '',
    profile_pic kept, role_permissions/user_roles surrogate id TEXT PK
    + UNIQUE(role_id,permission_id) (engine needs single id column).
- migrations/0003_pos_eshop/mysql.sql + postgres.sql: portable subset in
  existing simplified style (tables + indexes + views; skip sqlite-only
  trigger bodies where dialect differs, port where cheap). Must apply
  cleanly — mysql suite runs every migration.
- Register in src/db/db.c migrations[] (3 path entries).
- test/golden/0003_pos_eshop.sqlite3.sql (byte copy) + add to the golden
  list in test/http_test.py.
- Adjust the one raw demo INSERT in http_test.py if it now violates NOT NULL
  (expected: no change needed thanks to defaults; verify).
Accept: fresh DB + upgraded real-shape DB both end with POS schema, demo
rows preserved (incl. profile_pic), /people notes FK intact, golden check
passes, mysql suite applies 0003.

-----------------------------------------------------------------------
PHASE 2 — Multi-field columns (src/helper/cells.c)
-----------------------------------------------------------------------
- master_child.h: MD_Column gains `cell`:
    typedef struct { const char **parts; size_t part_count;
                     const char *style; } MD_Cell;   /* style: "stack"|"avatar" */
  parts = field names of THIS table (same-table grouping only; FK labels
  stay a separate fk_label mechanism).
- Engine (master_child.c):
  * SELECT list expands composite columns into their part names.
  * Slot map built once per columns-array: col -> {first_slot, nslots}
    (flat columns = identity -> demo behavior unchanged).
  * md_fk_display + every values/disp index switches to slot map.
  * Display td: composite -> src/helper/cells.c renderer (escaped inline
    HTML); normal td unchanged.
  * Edit row td, add-row, master create form, md_form_cols_load:
    composite renders N labeled inputs inside the ONE td (td count and
    colspan math unchanged — composite counts as 1 column everywhere).
- src/helper/cells.c/.h:
  * cell_stack(): flex-col lines, color-ranked (province strongest ->
    village faintest) — color theory, no radius/padding nesting.
  * cell_avatar(): grid [32px img][name bold / username muted].
  * both build via String_Builder + sb_append_html_escaped, return cstr.
- Showcase configs: POS locations page (4-part stack cell), POS users
  page (avatar cell) — separate md column arrays from demo so 140 tests
  don't move.
Accept: one <td> renders 4 fields / avatar grid; CRUD still saves each
part; colspan + pagination slicing still correct; demo pages unchanged.

-----------------------------------------------------------------------
PHASE 3 — Style pass (color theory, compact)
-----------------------------------------------------------------------
- Shared components (master_child.h.tt, md_child_rows/add_row/tab .tt,
  pagination.h.tt, MD form markup):
  * remove `rounded` from data-area controls;
  * keep inputs px-0.5 py-1.5, buttons p-1, cells pl-3/py-1 class;
  * hierarchy ONLY via bg steps: table header bg-slate/gray-100, parent
    row white vs child panel gray-50 vs hover gray-100 (dark: 900/800/700),
    chips/labels by text color intensity;
  * no m*/p* used to create nesting; hairline borders/dividers allowed.
- Optional same pass on sidebar/header/dashboard (small, do it).
Accept: visual hierarchy readable without spacing tricks; suite green.

-----------------------------------------------------------------------
PHASE 4 — Engine: soft_delete + read_only
-----------------------------------------------------------------------
- MD_MasterConfig.soft_delete: loader adds "deleted_at IS NULL" (master +
  child); delete route (module handlers reuse SERVE_DELETE variant) ->
  UPDATE deleted_at; restore button + route -> clears; ?deleted=1 shows
  deleted rows with distinct bg tint; child rows follow same rule.
- MD_MasterConfig.read_only: hides add-row/edit/delete UI (templates gate
  on flag); write routes simply not registered by the module.
- Tests: delete -> gone from list; restore -> back; read_only page has no
  mutation affordances.
Accept: cascade triggers fire on soft-delete (verify ledger/order rows).

-----------------------------------------------------------------------
PHASE 5-7 — Modules (each = src/pos/<entity>.c + column arrays + route_new
line + sidebar link; reuse SERVE_CRUD glue — no generic layer)
-----------------------------------------------------------------------
Page map (~17 MD pages + reports + dashboard):
  5a CATALOG   /pos/products  -> children: product_variants, inventory_stocks
            /pos/categories -> child: products (parent_id self-FK select)
  5b INVENTORY /pos/stocks    -> child: stock_ledger
  5c SALES     /pos/orders    -> children: order_items, payments, deliveries
            /pos/shifts   (cash_shifts, master-only)
            /pos/finance  (financial_ledgers, master-only, read_only)
  5d PARTY     /pos/customers -> children: users(customer_id),
                                 customer_interactions
            /pos/users    -> children: user_roles, staff,
                                 + avatar composite cell
            /pos/staff    -> child: cash_shifts
            /pos/org      -> children: users, staff (org_unit_id)
  5e ACCESS    /pos/roles     -> children: role_permissions, user_roles
            /pos/permissions (master-only)
  5f CONFIG    /pos/locations (stack composite showcase, master-only)
            /pos/dictionaries (master-only)
            /pos/i18n     = languages -> translations child
            /pos/config   (system_configs, master-only)
  5g MONITOR   /pos/alerts    (system_alerts, master-only)
            /pos/audit    (audit_logs, read_only)
  Sidebar: grouped nav (Dashboard, Catalog, Sales, Party, Inventory,
  Access, Config, Monitoring, Reports); flat labels, color-step group
  headers.
Per module: columns array (incl. composites), FK selects, sums where
meaningful (totals), handlers via SERVE_* macros, loader order.
Accept per module: list 200, create/edit/soft-delete/restore via MD form,
child tabs load, pagination window works with volume.

-----------------------------------------------------------------------
PHASE 8 — Reports (Khmer, A4)
-----------------------------------------------------------------------
- Spike (throwaway, decides format): HTML->soffice pdf vs .fodt->soffice pdf;
  check A4 page size honored, Khmer shapes correctly (fonts present),
  latency, table fidelity. Pick ONE and record in wiki.
- core/report/ (new): report registry {id, khmer_title, sql, columns,
  footer}; builder emits markup (UTF-8 Khmer headers, compact tables,
  shop header, date range); converter: popen("soffice --headless
  --convert-to pdf|docx --outdir <tmp> ...") with timeout + error path.
- Routes: /reports (index page, plain links + format buttons),
  /reports/<id>.pdf, /reports/<id>.docx with Content-Disposition:
  attachment; Content-Type application/pdf / vnd.openxmlformats.
- MVP reports: sales summary (v_pos_sales_delivery_report), daily order
  list, low-stock list, product sales ranking.
Accept: downloads return %PDF magic / PK zip magic, Khmer bytes present in
source markup, no server hang if soffice fails (502 + log).

-----------------------------------------------------------------------
PHASE 9 — Seed data
-----------------------------------------------------------------------
- migrations/0004_seed_pos/sqlite3.sql (idempotent INSERT OR IGNORE):
  Khmer locations (province/district/commune/village), org unit, staff,
  roles+permissions, categories, ~50 products + variants + stocks (volume
  for pagination), customers/users, orders across last 14 days with
  items/payments/deliveries, financial ledger rows, cash shift, sample
  alerts/audit rows.
- mysql.sql: skip seed (POS feature tests are sqlite-only) — document.
- Demo tests unaffected (they count rows on demo pages: keep demo tables
  untouched — seeds only touch POS tables).
Accept: /pos pages populated; reports have realistic Khmer numbers.

-----------------------------------------------------------------------
PHASE 10 — Verification
-----------------------------------------------------------------------
- new test/pos_test.py (sqlite suite):
  * all ~26 tables exist after migrations;
  * spot-check every route returns 200;
  * product create/edit/soft-delete/restore round-trip;
  * composite cell markers (4-in-1 td, avatar grid) present;
  * read_only page: no add/edit/delete controls;
  * reports: %PDF and PK magic + attachment header;
  * pagination: seeded products > 40 -> page 2 disjoint rows, page=999 clamp;
  * users swap preserved demo rows + profile_pic + notes FK (/people 200).
- Full suite: 140 existing + new checks green; mysql suite still applies
  all migrations; https suite untouched.
- Manual smoke via webc dev: walk every page, JS swap, pagination flips,
  report download opens (A4, Khmer).
- Update wiki/README-notes (wiki only; README is user's).
Accept: ./test/run.sh all green; wiki current.

-----------------------------------------------------------------------
PHASE 11 — Storefront at /, staff auth, checkout-only orders
-----------------------------------------------------------------------
Goal: customers order by clicking buy (guest checkout), staff-only pages
gated by session login. Orders are never freely inputable by guests.
- migrations/0005_auth (sqlite3/mysql/postgres): user_sessions table +
  indexes, admin hash UPDATE (sd.staff1 / posadmin1, salt webc2026),
  sqlite-only order-date fix (sd-ord-040/041/042 were +12..14h future and
  outranked fresh rows in created_at DESC -> pinned to now-3/-2/-1 hour).
- core/auth: EVP sha256 salt$hash verify, sessions in user_sessions
  (dialect expiry), auth_gate(sc) at top of route_request (303
  /login?next=... for /pos /dashboard /reports prefixes), GET/POST /login
  (housekeeping DELETE expired, C-minted sid, webc_sid HttpOnly 7d
  SameSite=Lax cookie), /logout (DELETE + clear -> 303 /). next= is
  sanitized to a same-site path (//evil.com -> /dashboard).
- core/http: set_cookie + http_set_cookie + http_req_header +
  http_cookie_find plumbing (Set-Cookie on redirect/attachment, cleared in
  sc_reset); webc_uuid() (/dev/urandom + time/counter fallback); form_text().
- src/shop: render_page_shell storefront chrome (own color steps, no admin
  sidebar), / index (cat chips + ?q= + ?page= 12/page + pager), /product/<id>,
  cookie cart webc_cart (id:qty,... HttpOnly, DB-validated, junk dropped,
  dupes merged, cap 999/line, 40 lines) at /cart[/add|/buynow|/update|/remove],
  /checkout GET+POST (server computes all money, posted amounts ignored;
  txn validates live stock, greedily decrements + negative stock_ledger
  rows, org/staff = ORDER BY id LIMIT 1, PENDING, delivery row carries
  contact, customers+users pair created/reused by phone, cart cleared ->
  303 /order/<uuid> public confirmation).
- sidebar: Dashboard href / -> /dashboard, logout action /notes -> /logout
  (nav counts 10/26 unchanged); route.c adds shop/auth routes, auth_gate()
  call; server byte-identical without JS (plain forms + cookie cart).
- Tests: testlib Client cookie jar + login_as; pos_test auth section first
  (guest 303, bad login marker, staff login 200, /dashboard 200) + table
  check 29->30 (+user_sessions); new test/shop_test.py last (storefront,
  filters, pager disjoint, detail 404, cart add/update/remove/buynow,
  guest checkout happy path + forged amounts ignored + stock/ledger/
  delivery/customer assertions, over-stock + empty-cart rejects, guest
  lockdown incl. forged POST /pos/orders/create changing nothing, login/
  logout incl. old-sid replay, evil next guard); mysql history 4->5.
Accept: ./test/run.sh all green (558 PASS); guest can browse+buy while
/pos /dashboard /reports redirect guests to /login.

-----------------------------------------------------------------------
PHASE 12 — Catppuccin light/dark theme + DB-backed language dropdown
-----------------------------------------------------------------------
Goal: one theme switch flips storefront + admin shell (Latte light /
Mocha dark), and a top-bar <select> picks the UI language from the
languages/translations tables (admin labels, reports stay Khmer).
- css/input.css: raw --ctp-* token values on :root (Latte) and .dark
  (Mocha) + @theme inline { --color-*: var(--ctp-*) } so utilities read
  the variables directly and ONE .dark class flips every color. Token
  set: base/mantle/crust/text/subtext0/overlay0/surface0/surface1/onbase
  + accents blue/mauve/green/red/peach/yellow/teal/lavender. onbase =
  text on a saturated accent (Latte accent dark w/ light text, Mocha
  accent light w/ dark text). Old .dark raw overrides for sidebar/nav/
  scrollbar deleted (tokens self-flip); active nav row = one nav-active
  class + unlayered .nav-link.nav-active rule (color-mix blue 16%).
  Never write text-base (font-size utility wins) - use text-onbase.
- js/themeSwitcher.js reused unchanged (localStorage, default light,
  device follows OS): storefront top bar gets 3 data-set-theme buttons
  (onclick themeController.setTheme), admin header.h.tt keeps its 3.
  render_page_shell now emits the script in <head> too (admin full-doc
  header already had it); no JS -> light stays.
- core/i18n/i18n.{h,c} (new): file-static request state bound by
  i18n_begin(sc) at the top of route_request (atomic, no yields - same
  pattern as user_data()). Lazy resolve on first tr()/lang access so
  static assets never touch the DB: cookie webc_lang validated against
  active languages (ORDER BY is_default DESC -> langs[0] = default),
  else default, else "" (mysql skips 0006 -> C literals, 200 kept).
  Row loads: SELECT trans_key,trans_value LIMIT 512 for active + default
  (skipped when same). tr(key, fallback) chain: active -> default -> C
  literal; reads only, NEVER writes on GET. i18n_html_lang() for
  <html lang> ("km" fallback); i18n_lang_form_html(back) renders the
  hidden-back <select onchange="this.form.submit()"> + <noscript>
  button (empty when <2 active langs); serve_lang_set validates code
  charset + membership, sets webc_lang (1y, Path=/, SameSite=Lax) and
  303s to back only when same-site (/ prefix, not //), else /.
- route.c: i18n_begin(sc) before auth_gate() (login translates too) +
  GET /lang ROUTE_EXACT; serve.c render_page_shell: <html lang> +
  themeSwitcher.js; core/layout/header.c: HTML_LANG/LANG_FORM macros so
  header.h.tt emits lang + top-bar dropdown (back = current_path);
  storefront chrome builds back from uri+query. Reports stay Khmer,
  admin labels stay English (they only translate if keys are added at
  /pos/i18n).
- Restyle = colors only, structure untouched: src/shop/*.c
  (slate/indigo/white/emerald -> tokens, accent buttons bg-blue
  text-onbase hover:brightness-90, cart header bg-text bar, chips
  bg-blue active / bg-mantle idle), core/auth login page tokens, sidebar/
  header .tt shell -> tokens (deep per-cell admin accents untouched).
- migrations/0006_i18n/sqlite3.sql: INSERT OR IGNORE, 70 keys x 2 langs
  (km byte-identical to the C fallbacks, en = UI English) covering
  shop./cart./checkout./order./auth./status.* keys; mysql+postgres files
  comment-only skip (no semicolons in comments); registered in db.c
  (history 5->6). New keys/languages stay at /pos/i18n CRUD.
- Tests: shop_test Phase 12 section (lang="km" default, theme script +
  3 buttons x2 controllers, lang form + 2 options + selected, Khmer
  placeholder, output.css pins :root/.dark/var()/nav-active, 140 seeded
  rows floor, invalid code -> no cookie, //evil back -> /, code=en ->
  cookie + English UI + lang="en" persisting on reload, admin doc via
  header macros); http_test + pos_test sidebar assertions updated to
  bg-surface0/60 nav-label + nav-active (counts 10/26/1 unchanged);
  mysql Migrations 5->6.
Accept: ./test/run.sh all green (587 PASS); / flips theme via top-bar
buttons, /lang?code=en switches storefront+admin UI to English for a
year, server bytes identical without JS (light theme, km, forms work).

-----------------------------------------------------------------------
PHASE 13 — Data truth + CRUD overhaul (shipped)
-----------------------------------------------------------------------
User complaints fixed: staff-as-child was wrong (master!), /pos/users said
"Staff", timestamps unreadable, avatar ugly/uncolored, missing FK info,
phone/email rules unclear, "trash data" on /pos routes, sidebar identity.
Answers locked: phone a must + email required only on deliberate account
creation; users+staff are MASTER types (FKs shown on the master, editable
as children of their targets); research-driven enterprise dashboards next.

- migrations/0007_users_contact: users.phone TEXT NOT NULL UNIQUE (0005
  shape restored), email nullable (0007 differs: backfill phone = username
  WHERE phone IS NULL). sqlite3.sql must drop trg_soft_del_org_units +
  trg_restore_org_units + v_active_users + v_pos_sales_delivery_report
  BEFORE the users_pos swap (ALTER ... RENAME re-validates every trigger/
  view ref) and recreate all four verbatim after the 3 users triggers.
  Verified on a copy of the real DB. Registered in db.c (history 6->7);
  mysql pin 6->7.
- /pos/users rebuilt as the User master page, title "Users": 9 columns =
  avatar cell (parts profile_pic,name,username,status + part_choices
  ACTIVE/INACTIVE/SUSPENDED) + phone + email + user_type FK (USER_TYPE)
  + org FK + location FK + customer FK (label COALESCE name->id) +
  activity cell (computed: Joined=created_at, Edited=updated_at, red
  DELETED when soft-deleted, md_date_human humanize) + actions.
  Handler usr_fields = {phone,name,username,email,profile_pic} phone-first
  (values[0] empty -> 400), opt = {status,customer_id,org_unit_id,
  location_id,user_type_dict_id} with SQL-level NULLIF/COALESCE defaults
  ('ACTIVE'/'CUSTOMER'); UPDATE keeps-stored COALESCE except email may
  clear. All users forms now carry a required phone input (0007).
- Staff is master: child tabs removed from /pos/users AND /pos/org,
  md_staff_child_* arrays deleted; /pos/staff keeps its cash_shifts child
  and gained phone + staff_type (Role, STAFF_TYPE) + hire_date columns
  (handlers already took them as opt fields). Staff created with
  user_id/org/location via query still works (contract test proves it).
- SERVE_EXTRACT_OPT_FIELDS (module/webc_template.h): body first, then
  query string only when the query HAS the key, then as-extracted. A
  present-but-empty body value (blanked FK input on child add-rows) no
  longer shadows the query, and an empty input keeps "" (non-NULL
  driver data) instead of degrading to NULL.
- Demo stack (/users + /people): phone added to md_users_columns, the
  create/edit templates, User struct, read/create/update SQL (count 5,
  update COALESCE-keeps phone); checkout guest insert email -> NULL.
- Engine: MD_Cell.part_choices (select in forms/display from a per-part
  choice list); computed columns skipped in create/edit forms;
  !nullable && type != DATE -> required attr; DATE inputs render
  md_date_input() prefills; md_date_human() post-pass for flat dates;
  cells.c avatar renderer (40px rounded-full, status ring blue/peach/
  red, initials-circle placeholder) + activity renderer.
- Auth/sidebar: Auth_User + auth_current_user() lazy request-scoped
  resolve (rebound each auth_gate); header.c SIDEBAR_AVATAR_RAW/
  SIDEBAR_NAME/SIDEBAR_SUB feed the card (name, role else email else
  workspace label, status-ringed avatar).
- alerts flags cell got part_choices {0,1}: free-text is_seen/is_sent
  inputs could poison the CHECK (0,1) columns - the contract test caught
  it, the form now offers the two legal values.
- test/crud_contract_test.py (runs last in run.sh): sidebar = page
  inventory; per page scrape data-md-op=create form, fill browser-style
  (bare required selects take first non-empty option, optional selects
  submit None, dates empty -> DB defaults, typed fakes for text/numbers,
  page-unique marker in a plain text field only), POST expect 302 + row
  persisted; walk the pager for the row's update form, submit exactly as
  rendered expect 302; delete form expect 302; live count back to 0.
  13 pages full round-trip, customers/stocks create-only (no text field
  to mark), finance/dictionaries/audit read-only skips.
- Pins updated: pos_test users th 4->9 (+status/activity/avatar-ring/
  phone-prefill edits), staff th 6->9 (+phone/hire inputs, STAFF_TYPE
  select), Roles-only + org users-only children, staff-not-a-user-child,
  sidebar identity (Khmer signed-in name on a non-users page),
  http/mysql posts + seeds carry phone, demo master row 5->6.
Accept: ./test/run.sh green (749 PASS); /pos/users shows who the person is
(status-ringed avatar, contact, type/org/location/customer FK labels,
human Joined/Edited/DELETED) and every sidebar page's own create+edit+
delete forms round-trip for real (302) - no page can trap its own UI.

-----------------------------------------------------------------------
## Phase 14 — Enterprise role workspaces (RBAC + role dashboards) DONE (suite 804 PASS)
-----------------------------------------------------------------------
Goal: "research a wonderful flow for staff + admin staff, dashboard as an
enterprise environment" - one identity, three surfaces. Manager sees the
business, cashier runs a till, driver runs deliveries, and every /pos
page enforces the grant.
- Migration 0008_role_workspace (sqlite real; mysql/postgres comment-only
  skip, suite Migrations 7->8): permissions +SD.FINANCE +SD.AUDIT, both
  granted to the manager role; staff1-style password hash seeded onto
  sd-user-2/3 so the other two demo staff can sign in.
- Auth: Auth_User + role_code/staff_id/perms (",CODE," join from
  role_permissions via user_roles; roles LEFT JOIN for the code);
  auth_has_perm() = strstr on the joined string.
- perm_gate(sc) runs after auth_gate in route.c: 19 prefix rules map
  every /pos page + /reports to its required grant (PRODUCT.MGMT, SELL,
  FINANCE, USER.MGMT, STOCK.ADJUST, SETTINGS, AUDIT, REPORT.VIEW) ->
  styled 403 (render_page_shell, "403 - Access restricted", names the
  role + the missing code). /dashboard is deliberately NOT perm-gated:
  it dispatches on role_code instead. Guests still 303 (auth_gate first).
- Sidebar: nav_perms[25] positional array mirroring nav_items[],
  _Static_assert-locked. Anonymous renders the FULL nav (http_test anon
  pins hold); signed-in gets per-row filtering; group headers hide when
  every row under them is filtered. "Main Form" label -> "Overview".
- Dashboard: src/dashboard/ loader + one template dispatching on role:
  - SD-MANAGER: 6 KPIs (Net sales 7d +/- delta, Transactions 7d, Avg
    basket, Sales today, Low stock, Open shifts), priority attention
    line (low stock > stale shift > pending orders > clear) with
    drill-down href, 7-day CSS bars over a 14-day rollup, recent orders.
  - SD-CASHIER: 4 KPIs (My sales / My avg / Team today / Out of stock),
    My shift card (open since / no open shift), my recent orders.
  - SD-DRIVER: 3 run KPIs (Pending / In transit / Delivered) + assigned
    deliveries queue (11 seeded, pending first, capped 12) + pending-run
    attention. else generic welcome (zero queries).
  - header row: workspace name + Khmer role chip + per-kind quick actions.
- header.h.tt brand "Clinic" -> "POS Admin".
- Tests: role_test.py (new; after shop_test, before contract): cashier =
  Counter workspace + nav 5 groups/11 links + SELL/report pages 200 vs
  finance/users/products/stocks/alerts 403 (body names SD.FINANCE);
  driver = Delivery workspace + nav 2 groups/7 links + orders/shifts/
  users/reports 403; logouts 303. pos_test +8 manager dashboard pins
  (workspace, KPI labels, attention line, 7+ bar heights, recent rows,
  quick actions, Khmer chip).
Accept: ./test/run.sh green (804 PASS); cashier lands on Counter (my till,
my shift, my orders) with manager-only nav/pages closed by 403, driver on
Delivery (assigned queue), manager on Manager workspace (KPIs, attention,
7-day bars) - one /dashboard URL, three rooms.

-----------------------------------------------------------------------
RISKS / NOTES
-----------------------------------------------------------------------
- sqlite users DROP+RENAME inside migration tx: notes FK points at table
  name "users" — drop-before-rename keeps name valid at tx end; FK checks
  are deferred within tx (foreign_keys=OFF during migrations per db.c).
- Existing real DB has demo users rows w/ UNIQUE emails — copy is 1:1,
  no dup risk; phone NULLs distinct under UNIQUE.
- Dictionary literal ids: any code assuming uuid ids must not parse users'
  user_type_dict_id — spot check dashboard counts.
- soffice latency (1-3 s/report): convert on request with timeout; if
  flaky, cache converted file keyed by params.
- mysql port of 0003: BIG translation (CHECKs, triggers, uuid default) —
  keep it simplified like 0002 (tables+indexes+views), suite only needs
  clean apply + demo pages.
- Column-count/td-count invariants: composite = 1 column in every loop —
  guard with tests (colspan check).
- Style pass touches shared templates -> re-run pagination JS contract
  tests (data-pg-* attrs must survive restyle).
