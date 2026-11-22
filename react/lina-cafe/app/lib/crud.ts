import { NextResponse, type NextRequest } from "next/server";
import { db } from "./db";

// Shared plumbing for the notes/users write routes — port of webc_pos
// src/core/webc_template.h SERVE_* macros + serve_error/http_render_redirect.

// webc_pos serve_error(): HTML page "<h1>400 Bad Request</h1>" with the
// status title (the port skips render_page_shell's chrome; status and
// heading match). Every webc dynamic response is Cache-Control: no-store.
export function errorPage(status: number, title: string): Response {
  return new Response(
    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\"/>" +
      `<title>${title}</title></head><body>` +
      `<div class="p-8"><h1 class="text-3xl font-bold">${title}</h1></div>` +
      "</body></html>",
    {
      status,
      headers: {
        "content-type": "text/html; charset=utf-8",
        "cache-control": "no-store",
      },
    },
  );
}

// webc_pos serve_ok(): the JSON-API ack.
export function okJson(): Response {
  return new Response('{"ok":true}', {
    status: 200,
    headers: {
      "content-type": "application/json",
      "cache-control": "no-store",
    },
  });
}

// webc_pos db_msg_is_unique(): the driver's unique-violation fingerprint.
export function isUniqueMsg(msg: string): boolean {
  return (
    msg.includes("UNIQUE constraint failed") || // sqlite
    msg.includes("Duplicate entry") || // mysql
    msg.includes("duplicate key value") // postgres
  );
}

// webc_pos SERVE_CREATE/UPDATE's 409 branch: plain-text reason (webc's
// reason table has no 409 row, hence statusText "Unknown").
export function uniquePage(singular: string, msg: string): Response {
  return new Response(
    `A ${singular} with the same unique value already exists. (${msg})`,
    {
      status: 409,
      statusText: "Unknown",
      headers: {
        "content-type": "text/plain",
        "cache-control": "no-store",
      },
    },
  );
}

// SERVE_EXTRACT_ONE for text fields: a MISSING key is a 400 (null), a
// present value (empty allowed) comes back as the string.
export function formText(form: FormData, key: string): string | null {
  const v = form.get(key);
  if (v === null) return null;
  return typeof v === "string" ? v : "";
}

// One synchronous transaction around a write (no awaits inside — the
// shared connection must never see an interleaved request mid-txn).
// Returns null on commit, the driver message after rollback.
export function dbRun(fn: () => void): string | null {
  db.exec("BEGIN");
  try {
    fn();
    db.exec("COMMIT");
    return null;
  } catch (e) {
    db.exec("ROLLBACK");
    return e instanceof Error ? e.message : String(e);
  }
}

// webc_pos http_redirect_target(): ?redirect= must be a same-site
// absolute path ("/x" yes, "//x", "/\x" and scheme targets no), else the
// module fallback.
export function redirectTarget(sp: URLSearchParams, fallback: string): string {
  const t = sp.get("redirect");
  if (
    t !== null &&
    t.length > 1 &&
    t.startsWith("/") &&
    !t.startsWith("//") &&
    !t.startsWith("\\")
  ) {
    return t;
  }
  return fallback;
}

// SERVE_*'s final 302 (webc uses the exact code — not a 303).
export function redirect302(request: NextRequest, fallback: string): Response {
  const target = redirectTarget(request.nextUrl.searchParams, fallback);
  return NextResponse.redirect(new URL(target, request.url), 302);
}

// webc route_id_parse() gate for /<id>/update|delete (TEXT pk: empty or
// >=128 chars is invalid; a path segment can never carry '/').
export function idValid(id: string): boolean {
  return id.length > 0 && id.length < 128;
}
