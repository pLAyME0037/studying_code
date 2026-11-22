import type { NextRequest } from "next/server";
import { db } from "@/app/lib/db";
import {
  dbRun,
  errorPage,
  formText,
  idValid,
  isUniqueMsg,
  redirect302,
  uniquePage,
} from "@/app/lib/crud";

// POST /notes/<id>/update — webc_pos SERVE_UPDATE(notes): invalid id ->
// 404, missing title/body key or empty title -> 400, one txn, 302 back.
type Params = { params: Promise<{ id: string }> };

export async function POST(request: NextRequest, { params }: Params): Promise<Response> {
  const { id } = await params;
  if (!idValid(id)) return errorPage(404, "404 Not Found");

  const form = await request.formData();
  const title = formText(form, "title");
  const body = formText(form, "body");
  if (title === null || body === null || title.length === 0) {
    return errorPage(400, "400 Bad Request");
  }

  const msg = dbRun(() => {
    db.prepare(
      "UPDATE Notes SET title = ?, body = ? WHERE id = ?;",
    ).run(title, body, id);
  });
  if (msg !== null) {
    if (isUniqueMsg(msg)) return uniquePage("note", msg);
    return errorPage(500, "500 Internal Server Error");
  }
  return redirect302(request, "/notes");
}

export async function GET(): Promise<Response> {
  return errorPage(404, "404 Not Found");
}
