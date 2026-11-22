import type { NextRequest } from "next/server";
import { db } from "@/app/lib/db";
import { dbRun, errorPage, idValid, redirect302 } from "@/app/lib/crud";

// POST /users/<id>/delete — webc_pos SERVE_DELETE(users): invalid id -> 404,
// one txn, 302 back to /users.
type Params = { params: Promise<{ id: string }> };

export async function POST(request: NextRequest, { params }: Params): Promise<Response> {
  const { id } = await params;
  if (!idValid(id)) return errorPage(404, "404 Not Found");

  const msg = dbRun(() => {
    db.prepare("DELETE FROM Users WHERE id = ?;").run(id);
  });
  if (msg !== null) return errorPage(500, "500 Internal Server Error");
  return redirect302(request, "/users");
}

// webc routes /users/<id>/delete as POST only — a GET falls through to 404.
export async function GET(): Promise<Response> {
  return errorPage(404, "404 Not Found");
}
