import { NextResponse } from "next/server";
import type { NextRequest } from "next/server";
import { db } from "@/app/lib/db";
import { SID_COOKIE } from "@/app/lib/auth";

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

export function proxy(request: NextRequest): NextResponse {
  const requestHeaders = new Headers(request.headers);
  requestHeaders.set("x-pathname", request.nextUrl.pathname);
  requestHeaders.set("x-search", request.nextUrl.search);

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
  }

  return res;
}

export const config = {
  matcher: [
    "/((?!_next/static|_next/image|favicon\\.ico|.*\\.(?:svg|png|jpg|jpeg|gif|webp|ico)$).*)",
  ],
};
