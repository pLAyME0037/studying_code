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
