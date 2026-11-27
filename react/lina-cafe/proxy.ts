import { NextResponse } from "next/server";
import type { NextRequest } from "next/server";
import { db } from "@/app/lib/db";
import { SID_COOKIE } from "@/app/lib/auth";
import { errorPage, idValid } from "@/app/lib/crud";

// Expose the request path/query to Server Components as request headers
// (the language form's `back` return path needs "the URI exactly as
// served", same as webc_pos's sc->uri + query_string). Next 16: this file
// replaces middleware.ts (renamed proxy).
//
// Slice 4: the staff gate moves here (webc_pos auth_gate runs before
// routing, so the redirect is a real pre-shell 303 — not a streamed one):
//   * guarded = /pos, /dashboard, /reports (exact or under);
//   * anon    -> 303 /login?next=<webc uri_encode(path+query)>;
//   * live non-ADMIN -> 303 / (a CUSTOMER session never enters staff areas).
// Proxy defaults to the Node.js runtime (Next 16), so node:sqlite works.
// It also owns the per-page cookie write (an RSC render can't set cookies):
// a valid ?per_page= on a paged list page stores webc_per_page for 365d,
// exactly like webc_pos page_info_parse_session.
// Finally it decides the dynamic detail pages' 404s: Next streams a
// request-time notFound() only after the shell, fixing the status at 200
// (Next docs → Status codes: check the slug in Proxy for a real 404), while
// webc_pos serves its 404 before any body. One indexed PK lookup per
// matching GET/HEAD URL.

// webc_pos auth_guarded_uri: exact match or one segment deeper.
function guardedUri(pathname: string): boolean {
  return (
    pathname === "/pos" ||
    pathname.startsWith("/pos/") ||
    pathname === "/dashboard" ||
    pathname.startsWith("/dashboard/") ||
    pathname === "/reports" ||
    pathname.startsWith("/reports/")
  );
}

// webc_pos auth.c perm_rules — one required permission per guarded route
// prefix (page level: the whole /pos/<area> including its child CRUD
// POSTs). /dashboard is deliberately absent (every signed-in role reaches
// it) and /pos itself has no route.
const PERM_RULES: ReadonlyArray<readonly [string, string]> = [
  ["/pos/products", "SD.PRODUCT.MGMT"],
  ["/pos/categories", "SD.PRODUCT.MGMT"],
  ["/pos/orders", "SD.SELL"],
  ["/pos/sale", "SD.SELL"],
  ["/pos/shifts", "SD.SELL"],
  ["/pos/customers", "SD.SELL"],
  ["/pos/finance", "SD.FINANCE"],
  ["/pos/users", "SD.USER.MGMT"],
  ["/pos/staff", "SD.USER.MGMT"],
  ["/pos/org", "SD.USER.MGMT"],
  ["/pos/roles", "SD.USER.MGMT"],
  ["/pos/permissions", "SD.USER.MGMT"],
  ["/pos/stocks", "SD.STOCK.ADJUST"],
  ["/pos/provinces", "SD.SETTINGS"],
  ["/pos/districts", "SD.SETTINGS"],
  ["/pos/communes", "SD.SETTINGS"],
  ["/pos/villages", "SD.SETTINGS"],
  ["/pos/dictionaries", "SD.SETTINGS"],
  ["/pos/i18n", "SD.SETTINGS"],
  ["/pos/config", "SD.SETTINGS"],
  ["/pos/alerts", "SD.AUDIT"],
  ["/pos/audit", "SD.AUDIT"],
  ["/reports", "SD.REPORT.VIEW"],
];

// webc_pos uri_required_perm: exact prefix or one segment deeper.
function uriRequiredPerm(pathname: string): string | null {
  for (const [prefix, perm] of PERM_RULES) {
    if (
      pathname.startsWith(prefix) &&
      (pathname.length === prefix.length || pathname[prefix.length] === "/")
    ) {
      return perm;
    }
  }
  return null;
}

// The signed-in session's first live role + its permission codes
// (webc_pos auth_current_user + auth_has_perm needle match). Sync db
// calls: proxy runs outside the RSC render where cookies()/cache() don't.
function sessionRolePerms(
  sid: string,
): { role: string; perms: Set<string> } | null {
  const row = db
    .prepare(
      "SELECT COALESCE(r.role_name, '') AS role_name, " +
        "COALESCE(r.id, '') AS role_id " +
        "FROM user_sessions s " +
        "JOIN users u ON u.id = s.user_id " +
        "LEFT JOIN user_roles ur ON ur.user_id = u.id AND ur.deleted_at IS NULL " +
        "LEFT JOIN roles r ON r.id = ur.role_id AND r.deleted_at IS NULL " +
        "WHERE s.id = ? " +
        "AND s.expires_at > strftime('%Y-%m-%dT%H:%M:%fZ', 'now') " +
        "AND u.deleted_at IS NULL LIMIT 1;",
    )
    .get(sid) as { role_name: string; role_id: string } | undefined;
  if (!row) return null;
  const perms = new Set<string>();
  if (row.role_id) {
    const codes = db
      .prepare(
        "SELECT p.perm_code AS c FROM role_permissions rp " +
          "JOIN permissions p ON p.id = rp.permission_id " +
          "WHERE rp.role_id = ? " +
          "AND rp.deleted_at IS NULL AND p.deleted_at IS NULL;",
      )
      .all(row.role_id) as Array<{ c: string }>;
    for (const r of codes) if (r.c) perms.add(r.c);
  }
  return { role: row.role_name, perms };
}

// webc_pos perm_gate's 403 body: minimal shell (the port's other error
// pages skip render_page_shell's chrome too) + the auth.c inner div
// verbatim. Status 403, Cache-Control: no-store.
function forbidden(role: string, need: string): Response {
  const esc = (s: string) =>
    s
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  const inner =
    '<div class="min-h-screen flex items-center justify-center bg-base px-4 sm:px-6 lg:px-8 py-6">' +
    '<div class="w-full bg-mantle border border-surface0">' +
    '<div class="bg-red text-onbase px-3 py-2 flex items-center justify-between">' +
    '<span class="text-sm font-semibold">403 · Access restricted</span>' +
    '<span class="text-xs opacity-80">POS Admin</span></div>' +
    '<div class="px-3 py-3 flex flex-col gap-2">' +
    '<p class="text-sm text-text">Your role does not include this area.</p>' +
    '<p class="text-xs text-subtext0">Role: <span class="text-text font-medium">' +
    esc(role) +
    "</span> · Required: <code class=\"text-peach\">" +
    esc(need) +
    "</code></p>" +
    '<div class="flex gap-2 pt-1">' +
    '<a href="/dashboard" class="bg-blue text-onbase hover:brightness-90 px-2 py-1.5 text-xs">Back to workspace</a>' +
    '<a href="/" class="border border-surface0 text-subtext0 hover:bg-surface0 px-2 py-1.5 text-xs">Storefront</a>' +
    "</div></div></div></div>";
  const html =
    '<!DOCTYPE html><html lang="km"><head><meta charset="utf-8"/>' +
    "<title>403 · Access restricted</title></head><body>" +
    inner +
    "</body></html>";
  return new Response(html, {
    status: 403,
    headers: {
      "content-type": "text/html",
      "cache-control": "no-store",
    },
  });
}

// webc_pos uri_encode(): percent-encode keeping only unreserved chars +
// '/' (byte-wise over UTF-8, hex uppercase) so "path?query" round-trips
// through the login form's ?next= field.
function uriEncode(src: string): string {
  const hex = "0123456789ABCDEF";
  let out = "";
  for (const b of new TextEncoder().encode(src)) {
    const keep =
      (b >= 0x41 && b <= 0x5a) ||
      (b >= 0x61 && b <= 0x7a) ||
      (b >= 0x30 && b <= 0x39) ||
      b === 0x2d || // -
      b === 0x5f || // _
      b === 0x2e || // .
      b === 0x7e || // ~
      b === 0x2f; // /
    out += keep ? String.fromCharCode(b) : "%" + hex[b >> 4] + hex[b & 15];
  }
  return out;
}

// webc_pos session_user_id + auth_current_user type check: a live session
// of a live user; NULL/empty user_type passes (webc only bounces a
// *known* non-ADMIN type).
function sessionUserType(sid: string): string | null | undefined {
  const row = db
    .prepare(
      "SELECT u.user_type_dict_id AS utype FROM user_sessions s " +
        "JOIN users u ON u.id = s.user_id " +
        "WHERE s.id = ? " +
        "AND s.expires_at > strftime('%Y-%m-%dT%H:%M:%fZ', 'now') " +
        "AND u.deleted_at IS NULL LIMIT 1;",
    )
    .get(sid) as { utype: string | null } | undefined;
  if (!row) return undefined; // no live session -> anon
  return row.utype;
}

// Pages paged by webc_pos page_info_parse_session (SERVE_READ lists +
// master_child). Only these read/keep the webc_per_page cookie.
function pagedList(pathname: string): boolean {
  return (
    pathname === "/notes" ||
    pathname === "/users" ||
    pathname === "/pos" ||
    pathname.startsWith("/pos/")
  );
}

// webc_pos page_size_from_sv: digits only; empty/zero/non-numeric -> null
// (no cookie write); huge -> clamped to PAGE_MAX_PER_PAGE.
function parsePageSize(v: string): number | null {
  if (v.length === 0) return null;
  for (const ch of v) {
    if (ch < "0" || ch > "9") return null;
  }
  if (v.length >= 21) return 500;
  const n = Number(v);
  if (!Number.isFinite(n) || n < 1) return null;
  return n > 500 ? 500 : n;
}

// The GET pages that 404 on a bad/unknown id (their own notFound() runs
// too late to set the status): /notes/<id>/edit, /users/<id>/edit,
// /product/<id>, /order/<id>. Query shape mirrors each page's own lookup
// (notes/users: idValid + row; shop: row AND deleted_at IS NULL).
function detailPage404(request: NextRequest): Response | null {
  if (request.method !== "GET" && request.method !== "HEAD") return null;
  const path = request.nextUrl.pathname;

  const edit = /^\/(notes|users)\/([^/]+)\/edit$/.exec(path);
  if (edit) {
    let id = edit[2];
    try {
      id = decodeURIComponent(id);
    } catch {
      return errorPage(404, "404 Not Found");
    }
    if (!idValid(id)) return errorPage(404, "404 Not Found");
    const table = edit[1] === "notes" ? "Notes" : "users";
    const row = db
      .prepare(`SELECT 1 AS one FROM "${table}" WHERE id = ? LIMIT 1;`)
      .get(id);
    if (!row) return errorPage(404, "404 Not Found");
    return null;
  }

  const detail = /^\/(product|order)\/([^/]+)$/.exec(path);
  if (detail) {
    let id = detail[2];
    try {
      id = decodeURIComponent(id);
    } catch {
      return errorPage(404, "404 Not Found");
    }
    if (!idValid(id)) return errorPage(404, "404 Not Found");
    const row =
      detail[1] === "product"
        ? db
            .prepare(
              "SELECT 1 AS one FROM products WHERE id = ? AND deleted_at IS NULL LIMIT 1;",
            )
            .get(id)
        : db
            .prepare(
              "SELECT 1 AS one FROM orders WHERE id = ? AND deleted_at IS NULL LIMIT 1;",
            )
            .get(id);
    if (!row) return errorPage(404, "404 Not Found");
    return null;
  }

  return null;
}

// Slice 6: the lazy child fragment URL (?fragment=1&child=<table>&
// &master=<id>) is port-only (webc renders children eagerly), so it gets
// the same pre-shell 404 treatment as the hand-crafted detail URLs: the
// child must be a registered child of that page and the master id valid.
const MD_CHILDREN: Record<string, readonly string[]> = {
  "/pos/provinces": ["districts", "communes", "villages"],
};

function fragmentChild404(request: NextRequest): Response | null {
  if (request.method !== "GET" && request.method !== "HEAD") return null;
  const sp = request.nextUrl.searchParams;
  if (sp.get("fragment") !== "1") return null;
  const child = sp.get("child");
  if (child === null) return null;
  if (!(MD_CHILDREN[request.nextUrl.pathname] ?? []).includes(child)) {
    return errorPage(404, "404 Not Found");
  }
  const master = sp.get("master");
  if (master === null || !idValid(master)) return errorPage(404, "404 Not Found");
  return null;
}

export function proxy(request: NextRequest): NextResponse | Response {
  const requestHeaders = new Headers(request.headers);
  requestHeaders.set("x-pathname", request.nextUrl.pathname);
  requestHeaders.set("x-search", request.nextUrl.search);

  // Real 404 status before the shell (see header comment).
  const missing = detailPage404(request);
  if (missing) return missing;
  const badChild = fragmentChild404(request);
  if (badChild) return badChild;

  const res = NextResponse.next({ request: { headers: requestHeaders } });

  // Per-page session cookie (webc paging.c: valid ?per_page= wins and is
  // stored clamped, Path=/ HttpOnly SameSite=Lax, one year).
  if (pagedList(request.nextUrl.pathname)) {
    const cand = request.nextUrl.searchParams.get("per_page");
    if (cand !== null) {
      const per = parsePageSize(cand);
      if (per !== null) {
        res.cookies.set("webc_per_page", String(per), {
          path: "/",
          httpOnly: true,
          sameSite: "lax",
          maxAge: 365 * 24 * 60 * 60,
        });
      }
    }
  }

  // Staff gate (webc auth_gate): only guarded URIs, before routing.
  if (guardedUri(request.nextUrl.pathname)) {
    const sid = request.cookies.get(SID_COOKIE)?.value;
    const utype = sid && sid.length <= 64 ? sessionUserType(sid) : undefined;
    if (utype === undefined) {
      const next = uriEncode(
        request.nextUrl.pathname + request.nextUrl.search,
      );
      return NextResponse.redirect(
        new URL(`/login?next=${next}`, request.url),
        303,
      );
    }
    if (utype !== null && utype !== "" && utype !== "ADMIN") {
      return NextResponse.redirect(new URL("/", request.url), 303);
    }

    // Perm gate (webc auth_gate → perm_gate order): the session is live
    // non-CUSTOMER here; a missing role/permission answers the bare 403.
    const need = uriRequiredPerm(request.nextUrl.pathname);
    if (need && sid) {
      const rp = sessionRolePerms(sid);
      if (!rp || !rp.perms.has(need)) return forbidden(rp?.role ?? "—", need);
    }
  }

  return res;
}

export const config = {
  matcher: [
    "/((?!_next/static|_next/image|favicon\\.ico|.*\\.(?:svg|png|jpg|jpeg|gif|webp|ico)$).*)",
  ],
};
