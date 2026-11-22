import type { NextRequest } from "next/server";
import { db } from "@/app/lib/db";
import {
  dbRun,
  errorPage,
  formText,
  isUniqueMsg,
  redirect302,
  uniquePage,
} from "@/app/lib/crud";

// POST /notes/create — webc_pos SERVE_CREATE(notes): required title+body
// keys (missing key or empty title -> 400), one txn, 302 back to /notes.
// The optional user_id rides body first, then the query string
// (SERVE_EXTRACT_OPT_FIELDS) — the plain /notes form posts neither.
export async function POST(request: NextRequest): Promise<Response> {
  const form = await request.formData();
  const title = formText(form, "title");
  const body = formText(form, "body");
  if (title === null || body === null || title.length === 0) {
    return errorPage(400, "400 Bad Request");
  }

  let userId = formText(form, "user_id");
  if (userId === null || userId.length === 0) {
    userId = request.nextUrl.searchParams.get("user_id") ?? "";
  }

  const msg = dbRun(() => {
    db.prepare(
      "INSERT INTO Notes (user_id, title, body) VALUES (?, ?, ?);",
    ).run(userId.length > 0 ? userId : null, title, body);
  });
  if (msg !== null) {
    if (isUniqueMsg(msg)) return uniquePage("note", msg);
    return errorPage(500, "500 Internal Server Error");
  }
  return redirect302(request, "/notes");
}

// webc routes /notes/create as POST only — a GET falls through to 404.
export async function GET(): Promise<Response> {
  return errorPage(404, "404 Not Found");
}
