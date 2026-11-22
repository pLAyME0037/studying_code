import type { NextRequest } from "next/server";
import { db, queryAll } from "@/app/lib/db";
import { dbRun, errorPage, okJson } from "@/app/lib/crud";

// /api/notes — webc_pos serve_notes_api (src/notes/notes.c): GET dumps the
// whole list as JSON (id/title/created_at/body, NULL -> ""), POST/PUT/DELETE
// write with the same 400/500 contract, anything else 405. Unguarded, like
// webc (auth_gate only watches /pos, /dashboard, /reports).

type NoteJson = {
  id: string;
  title: string | null;
  created_at: string | null;
  body: string | null;
};

function jsonStr(obj: Record<string, unknown>, key: string): string | null {
  const v = obj[key];
  return typeof v === "string" ? v : null;
}

async function readJsonBody(request: NextRequest): Promise<Record<string, unknown> | null> {
  try {
    const body: unknown = await request.json();
    if (body !== null && typeof body === "object" && !Array.isArray(body)) {
      return body as Record<string, unknown>;
    }
  } catch {
    // fall through — webc's json_find_string just fails to find keys
  }
  return null;
}

export async function GET(): Promise<Response> {
  // serve_notes_json: read_notes(NULL slice) — the unwindowed query, order
  // decided by read_notes' ORDER BY created_at DESC.
  const rows = queryAll<NoteJson>(
    "SELECT id, user_id, title, datetime(created_at, 'localtime') AS created_at, body " +
      "FROM Notes ORDER BY created_at DESC;",
  );
  const items = rows.map((r) => ({
    id: r.id ?? "",
    title: r.title ?? "",
    created_at: r.created_at ?? "",
    body: r.body ?? "",
  }));
  return new Response(JSON.stringify(items), {
    status: 200,
    headers: {
      "content-type": "application/json",
      "cache-control": "no-store",
    },
  });
}

export async function POST(request: NextRequest): Promise<Response> {
  const body = await readJsonBody(request);
  if (body === null) return errorPage(400, "400 Bad Request");
  const title = jsonStr(body, "title");
  const text = jsonStr(body, "body");
  if (title === null || text === null || title.length === 0) {
    return errorPage(400, "400 Bad Request");
  }

  const msg = dbRun(() => {
    // The JSON API carries no user_id (create_note's optional third value
    // is absent -> SQL NULL), same as webc.
    db.prepare("INSERT INTO Notes (user_id, title, body) VALUES (NULL, ?, ?);").run(
      title,
      text,
    );
  });
  if (msg !== null) return errorPage(500, "500 Internal Server Error");
  return okJson();
}

export async function PUT(request: NextRequest): Promise<Response> {
  const body = await readJsonBody(request);
  if (body === null) return errorPage(400, "400 Bad Request");
  const id = jsonStr(body, "id");
  const title = jsonStr(body, "title");
  const text = jsonStr(body, "body");
  if (id === null || id.length === 0 || id.length >= 128) {
    return errorPage(400, "400 Bad Request");
  }
  if (title === null || text === null || title.length === 0) {
    return errorPage(400, "400 Bad Request");
  }

  const msg = dbRun(() => {
    db.prepare("UPDATE Notes SET title = ?, body = ? WHERE id = ?;").run(
      title,
      text,
      id,
    );
  });
  if (msg !== null) return errorPage(500, "500 Internal Server Error");
  return okJson();
}

export async function DELETE(request: NextRequest): Promise<Response> {
  const body = await readJsonBody(request);
  if (body === null) return errorPage(400, "400 Bad Request");
  const id = jsonStr(body, "id");
  if (id === null || id.length === 0 || id.length >= 128) {
    return errorPage(400, "400 Bad Request");
  }

  const msg = dbRun(() => {
    db.prepare("DELETE FROM Notes WHERE id = ?;").run(id);
  });
  if (msg !== null) return errorPage(500, "500 Internal Server Error");
  return okJson();
}

// webc serve_error(sc, 405) for any other method (Next auto-answers the
// unexported ones with 405 + Allow; the exported shape matches webc's
// explicit fallthrough for GET/POST/PUT/DELETE only).
