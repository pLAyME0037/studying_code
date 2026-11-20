import { headers } from "next/headers";

// The URI exactly as served (path + query) — the `back`/`next` return
// paths webc_pos reads off sc->uri/query_string. Populated by proxy.ts.
export async function currentPath(): Promise<string> {
  const h = await headers();
  const path = h.get("x-pathname") ?? "/";
  const search = h.get("x-search") ?? "";
  return path + search;
}
