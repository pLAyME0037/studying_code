import { NextResponse } from "next/server";
import type { NextRequest } from "next/server";

// Expose the request path/query to Server Components as request headers
// (the language form's `back` return path needs "the URI exactly as
// served", same as webc_pos's sc->uri + query_string). Next 16: this file
// replaces middleware.ts (renamed proxy).
export function proxy(request: NextRequest) {
  const requestHeaders = new Headers(request.headers);
  requestHeaders.set("x-pathname", request.nextUrl.pathname);
  requestHeaders.set("x-search", request.nextUrl.search);
  return NextResponse.next({ request: { headers: requestHeaders } });
}

export const config = {
  matcher: [
    "/((?!_next/static|_next/image|favicon\\.ico|.*\\.(?:svg|png|jpg|jpeg|gif|webp|ico)$).*)",
  ],
};
