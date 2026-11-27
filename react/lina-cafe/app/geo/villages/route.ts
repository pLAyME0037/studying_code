import type { NextRequest } from "next/server";
import { queryAll } from "@/app/lib/db";

// GET /geo/villages?commune=<id> — webc_pos serve_geo_villages_options
// (geo.c): bare <option> lines for one commune. A missing or over-long
// commune answers an empty list, not an error (a half-built form just
// shows no villages). The id is bound, never interpolated.

const HTML = "text/html; charset=utf-8";

function html(body: string): Response {
  return new Response(body, {
    status: 200,
    headers: { "content-type": HTML },
  });
}

// webc sb_append_html_escaped: & < > " ' -> entities.
function esc(s: string): string {
  return s
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

export async function GET(request: NextRequest): Promise<Response> {
  const commune = request.nextUrl.searchParams.get("commune") ?? "";
  if (commune.length === 0 || commune.length > 32) return html("");

  const rows = queryAll<{ id: string; name_kh: string | null }>(
    "SELECT id, name_kh FROM villages " +
      "WHERE commune_id = ? AND deleted_at IS NULL " +
      "ORDER BY name_kh ASC;",
    commune,
  );
  let out = "";
  for (const r of rows) {
    out += `<option value="${esc(String(r.id))}">${esc(r.name_kh ?? "")}</option>\n`;
  }
  return html(out);
}
