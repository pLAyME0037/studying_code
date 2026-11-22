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
- **`use cache`** on read-heavy, low-churn data (nav items, translations table, product
  catalog reads) with `cacheLife`; `revalidatePath`/`refresh()` after Server Action writes.
- **Sync DB** (node:sqlite) = no async overhead in RSC; module-scope singleton connection.
- **next/image** for product/gallery images; Tailwind 4 (already wired via turbopack).
- **Server Actions** replace webc_pos's POST routes (`/cart/add`, `/checkout`, CRUD…),
  auth-checked inside every action (Next 16 rule).

## Next 16 facts honored (from node_modules/next/dist/docs — AGENTS.md mandate)

- middleware → `proxy.ts` (not used phase 1; auth via layout/action checks).
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
4. **dashboard + notes + users**: KPI dashboard, notes CRUD, users CRUD (owner-gated).
5. **perf pass**: `use cache`/`cacheLife` where legal, Suspense boundaries, next/image,
   bundle check (`next build` output), lint clean.

## Verification

- `npm run build` zero errors (typecheck + prerender insights).
- `npm run dev` smoke: pages render, cart math matches webc_pos (same seeds → same
  numbers), login → dashboard works, km/en switch works.
- Style: eyeball diff vs webc_pos on :8080 (same tokens/classes).

## Out of scope (phase 1)

POS CRUD engine, reports, geo cascade pages, `/version`, `/people`, `/lang` full page,
mysql/postgres drivers (sqlite only), HTTPS/fork/process model (Next owns the server).
