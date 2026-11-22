import { NextResponse, type NextRequest } from "next/server";
import { db } from "@/app/lib/db";
import { SID_COOKIE, activeSession } from "@/app/lib/auth";

// GET + POST /logout — webc_pos serve_auth_logout: drop this session AND
// every session of the browser's switch_group (a logout signs the whole
// browser out), clear the cookie, 303 to the storefront.
async function logout(request: NextRequest): Promise<Response> {
  const active = await activeSession();
  if (active) {
    db.prepare(
      "DELETE FROM user_sessions WHERE id = ? OR switch_group = ?;",
    ).run(active.sid, active.group);
  }

  const res = NextResponse.redirect(new URL("/", request.url), 303);
  res.cookies.set(SID_COOKIE, "", {
    path: "/",
    httpOnly: true,
    sameSite: "lax",
    maxAge: 0,
  });
  return res;
}

export async function GET(request: NextRequest): Promise<Response> {
  return logout(request);
}

export async function POST(request: NextRequest): Promise<Response> {
  return logout(request);
}
