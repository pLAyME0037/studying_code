# lina-cafe — webc_pos → Next.js port plan

Goal: port webc_pos's logic and style to this Next.js 16 app for learning + performance
comparison. webc_pos is READ-ONLY (never written to). Source of truth for logic/style:
`~/github/webc_pos` (C server, 180 routes, 35 `.tt` templates, catppuccin Tailwind).

## Decisions (user-confirmed, 2026-11-20)

| | |
|---|---|
| scope phase 1 | storefront (`/`, `/product/[id]`, `/cart`, `/checkout`, `/order/[id]`) + auth (`/login`, `/signup`, `/logout`, `/accounts`) + `/dashboard` + `/notes` + `/users` + shop chrome/layout/sidebar |
| phase 2 (later) | POS master_child CRUD engine (~25 entities, one config-driven component) |
| phase 3 (later) | reports (LibreOffice/FODT — special), geo extras |
| data layer | real SQLite via **node:sqlite** (built into Node 26, sync, zero deps); webc_pos's `sqlite3.sql` migrations 0001-0012 + seeds copied into `app/lib/db/migrations/`, applied in order on first open |
| auth + i18n | both ported: session cookie auth (webc_sid / user_sessions) via Server Actions + cookies(); `tr(key, km)` chain (active-lang row → km default → literal) with `webc_lang` cookie |
| style | 1:1: catppuccin tokens (css/input.css) → Tailwind 4 `@theme`, class strings copied verbatim from `.tt` |
| perf intent | showcase Next.js: RSC (server components default), minimal client JS, Suspense streaming, `use cache` for static-ish data, next/image, Server Actions, PPR (cacheComponents already on) |

## Structure (user-specified)

```
app/
  ui/        # components — ported .tt markup (header, sidebar, footer, chrome, cells, forms…)
  lib/       # logic — db, migrations, auth, i18n, shop/cart/checkout queries, server actions
  <routes>   # app router pages (URLs identical to webc_pos)
public/      # static assets (js/ ported to tiny client components where needed)
```

## Perf strategy (the comparison point)

- **Server Components by default**: all pages render on server, near-zero client JS
  (only theme/sidebar toggle + product gallery + geo cascade need `'use client'`).
- **PPR / streaming**: `cacheComponents` + `partialPrefetching` already enabled. Chrome
  (sidebar/header shell) = static shell; cookie/session/data-dependent parts stream behind
  `<Suspense>` (idiomatic Next 16: `cookies()` in layout must be inside a boundary).
- **`use cache`** on read-heavy, low-churn data (translations/languages rows,
  `shopName()`) with `cacheLife`; `revalidatePath`/`refresh()` after Server Action
  writes. Catalog/session data stays request-fresh (parity with webc's per-request
  reads; checkout mutates stock).
- **Sync DB** (node:sqlite) = no async overhead in RSC; module-scope singleton connection.
- **`<img>` kept over next/image**: webc templates are the source of truth (verbatim
  class strings + JS `src` swap on the gallery); images are tiny (1×1 seeds, 512px
  uploads). See slice 5.
- **Server Actions** replace webc_pos's POST routes (`/cart/add`, `/checkout`, CRUD…),
  auth-checked inside every action (Next 16 rule).

## Next 16 facts honored (from node_modules/next/dist/docs — AGENTS.md mandate)

- middleware → `proxy.ts` (renamed from middleware in Next 16): session gate for
  `/pos|/dashboard|/reports`, pre-shell 404 for dynamic detail pages, `webc_per_page`
  write, `x-pathname`/`x-search` request headers.
- `params`/`searchParams` are Promises; `LayoutProps<'/'>`/`PageProps<'/'>` typed helpers.
- Server Functions: `'use server'` in `app/lib/actions.ts`; `refresh()` from `next/cache`.
- Cache Components model: runtime access (`cookies()`) needs `<Suspense>` or
  `use cache: private`; random/time needs `connection()` or cache.

## Slices (each verified with `npm run build` before moving on)

1. **foundation**: globals.css (catppuccin tokens from css/input.css), db layer +
   migration runner + copied sql, i18n `tr()`, session auth lib, root layout + shop
   chrome (header/sidebar/footer), theme+sidebar client toggles.
2. **storefront** ✅: `/` index (chips/filters/pagination), `/product/[id]` (gallery,
   reviews/likes), `/cart` (server-computed money), `/checkout`, `/order/[id]`
   (build green + HTTP smoke: add/update/remove, checkout txn order/stock/ledger/
   delivery rows, error `?e=` re-renders, pager/chips/search).
3. **auth pages** ✅ (build green + HTTP smoke: login ADMIN→/dashboard,
   CUSTOMER→/, err_fill/err_bad, signup fill/short/phone/taken + claim flow,
   accounts dedupe/current badge, switch, remove, logout GET+POST deletes
   switch_group + clears cookie). Errors ride `?e=` + server-action redirect;
   `/logout` = route handler; `/switch` = `switchAccount` action.
   Known deviation: anon `/accounts` redirects streamed → 200 + meta-refresh
   (same PPR tradeoff as empty-cart `/checkout`).
4. **dashboard + notes + users** ✅ (build green + HTTP smoke): manager dashboard
   (SD-MANAGER 6 KPIs, 7-day bars, attention line, recent orders; anon → pre-shell
   303 `/login?next=`, live non-ADMIN → 303 `/`), notes + users CRUD as **route
   handlers** on webc's exact paths/statuses (302, 400 missing-key/empty-first,
   409 unique "Unknown", 413 too-large, GET create/update/delete → 404, plain
   `<form action>` no $ACTION_ID), image upload → ffmpeg webp served from
   `/resource/image/upload/<name>` (fake → 400, >2MB → 413), `/api/notes`
   GET/POST/PUT/DELETE, `?fragment=1` → `#pg-frag` + nav only, `?per_page=`
   cookie write in proxy (read/clamped in page: 10/99999→500/abc→20), edit
   pages unguarded like webc. **Proxy owns pre-shell 404** for `/notes|users/<id>/edit`
   + `/product|order/<id>` (Next docs: page-level `notFound()` streams after the
   shell → status 200; webc serves real 404). Notes/users get their own
   Suspense layouts (cacheComponents prerender rule). node:sqlite gotcha fixed:
   unaliased SELECT expressions label with expression text — every computed
   column needs `AS` (webc reads positionally); dashboard day-window math uses
   ms (`DAY_MS`), webc's `now` is seconds.
   Known deviation: route-handler error pages skip webc's full shell chrome
   (status + heading match, `Cache-Control: no-store`).
5. **perf pass** ✅ (build green + lint 0 errors + smoke re-pass):
   - **`use cache` + `cacheLife('hours')`** on the two phase-1 config readers:
     i18n `loadLangs`/`loadRows(langId)` (args = cache key) and `shopName()` —
     webc re-reads these tables every request; phase 1 has no CRUD UI for them,
     so phase-2 language/settings editors must `revalidatePath`/`revalidateTag`
     or drop the directives. Catalog/dashboard/notes/users lists stay request-
     fresh (checkout decrements stock; stale numbers would break parity).
   - **node:sqlite lesson**: rows are **null-prototyped** — `"use cache"` rejects
     them ("Classes or null prototypes are not supported", `environmentName:
     Cache`). Copy into object literals inside the cached fn before returning
     (and `getI18n` re-wraps `langs`/`rows` plain before handing them out).
   - **lint clean** (0 errors): internal `<a href="/">` → `<Link>` (prefetch +
     rule), the 3 admin-chrome `<script>` → `defer` (webc loads them at end of
     body = same timing, parse no longer blocked), unused vars dropped. The 9
     `no-img-element` warnings stay on purpose (below).
   - **`next/image` NOT adopted**: webc markup is `<img>` with verbatim class
     strings, the product gallery swaps `src` from `data-full` JS, and the
     images are 1×1 seed PNGs / 512px upload webp — optimization adds risk
     without measurable win. Deviation kept + documented (lint warnings).
   - **storefront images fixed**: webc's `resource/image/*.png` copied to
     `public/resource/image/` (product pics 404'd before this slice).
   - **numbers** (PORT=3100, warm): TTFB `/` 4.5ms, `/product/[id]` 8.5ms,
     `/dashboard` 6ms, `/notes` 20ms, `/login` 6ms; static assets 600K JS
     (36K CSS), home page 85KB. webc_pos :8080 was down — head-to-head
     comparison pending (needs user OK to start webc_pos server).
   - **`connection()` in `dashboardLoad`** (fix, 2026-11-25): dev's
     blocking-prerender insight flagged `Date.now()` — the dashboard is
     request-time data, so `await connection()` opts the subtree out of
     prerendering (build was always green; the insight only fires in dev's
     on-demand shell prerender). Also: function-action forms dropped their
     own `method="POST"`/`encType` (React provides both — dev console warning
     otherwise); rendered HTML unchanged.

## Verification

- `npm run build` zero errors (typecheck + prerender insights).
- `npm run dev` smoke: pages render, cart math matches webc_pos (same seeds → same
  numbers), login → dashboard works, km/en switch works.
- Style: eyeball diff vs webc_pos on :8080 (same tokens/classes).

### Head-to-head vs webc_pos (2026-11-25, both live: :8080 vs :3100)

- **Cart**: `POST /cart/add product_id=sd-prod-001 qty=3` → both 303 `/cart`,
  line 52.00 / total 156.00 / subtotal 156.00 — identical.
- **KPI parity**: lina's dashboard SQL run against **both** DBs reproduces each
  server's rendered HTML exactly (net/avg/today/low/open/pct — e.g. webc DB:
  `6529.50 $ / 2176.50 $ / 0.00 $ / 6 / 1 / -86.7%` in both SQL and page).
  Same math, **different data**: the DBs diverged — webc's live DB
  (`~/sqlite3/webc_c/db`) absorbed the test-suite runs (42 orders / 97 853.00),
  lina's snapshot has 43 / 98 009.00 (the extra 156.00 = `WEB-261121-5c850f84`).
- **TTFB** (warm, ×3): `/` webc 0.9ms vs lina 4.5ms; `/product/[id]` webc
  1.3–5.3ms vs lina 3.9–8.4ms. HTML size `/`: webc 18KB vs lina 85KB (Next
  inlines the RSC flight payload — the same strings appear twice, e.g. each KPI
  label once in DOM + once in payload).
- Login parity: both 303 `/dashboard` (webc `303 Unknown` statusText = its
  normal redirect; lina `303 See Other`).

## Out of scope (phase 1)

POS CRUD engine, reports, geo cascade pages, `/version`, `/people`, `/lang` full page,
mysql/postgres drivers (sqlite only), HTTPS/fork/process model (Next owns the server).
