import { readFile } from "node:fs/promises";
import path from "node:path";
import type { NextRequest } from "next/server";
import { UPLOAD_DIR } from "@/app/lib/upload";
import { errorPage, idValid } from "@/app/lib/crud";

// GET /resource/image/upload/<name> — serves the bytes saveUploadedImage
// stored under data/image/upload/ (webc_pos serves the same folder as
// static files; Next keeps it out of public/, so a route handler stands in).
type Params = { params: Promise<{ name: string }> };

export async function GET(_request: NextRequest, { params }: Params): Promise<Response> {
  const { name } = await params;
  // route_id_parse() shape (non-empty, <128) plus a traversal guard —
  // a path param can never carry '/', but %2e%2e can arrive decoded.
  if (!idValid(name) || name.includes("..") || name.includes("\\") || name.includes("/")) {
    return errorPage(404, "404 Not Found");
  }

  try {
    const buf = await readFile(path.join(UPLOAD_DIR, name));
    return new Response(new Uint8Array(buf), {
      status: 200,
      headers: {
        "content-type": "image/webp",
        "content-length": String(buf.byteLength),
      },
    });
  } catch {
    return errorPage(404, "404 Not Found");
  }
}
