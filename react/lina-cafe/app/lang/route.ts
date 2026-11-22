import { NextResponse, type NextRequest } from "next/server";
import { cookies } from "next/headers";
import {
  LANG_COOKIE,
  LANG_COOKIE_MAX_AGE,
  langCodeOk,
} from "@/app/lib/i18n";
import { queryOne } from "@/app/lib/db";

// GET /lang?code=<c>&back=<path> — webc_pos serve_lang_set: same-site
// return path only (mirrors the ?next= open-redirect guard), cookie only
// for a well-formed code naming an active language, then 303 back.
export async function GET(request: NextRequest): Promise<Response> {
  const code = request.nextUrl.searchParams.get("code") ?? "";
  const back = request.nextUrl.searchParams.get("back") ?? "";

  let to = "/";
  if (
    back.length > 1 &&
    back.startsWith("/") &&
    !back.startsWith("//") &&
    !back.startsWith("\\")
  ) {
    to = back;
  }

  if (langCodeOk(code)) {
    const row = queryOne<{ one: number }>(
      "SELECT 1 AS one FROM languages " +
        "WHERE code = ? AND is_active = 1 AND deleted_at IS NULL LIMIT 1;",
      code,
    );
    if (row) {
      const jar = await cookies();
      jar.set(LANG_COOKIE, code, {
        path: "/",
        sameSite: "lax",
        maxAge: LANG_COOKIE_MAX_AGE,
      });
    }
  }

  return NextResponse.redirect(new URL(to, request.url), 303);
}
