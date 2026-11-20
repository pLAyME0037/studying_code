import { Suspense } from "react";
import type { ReactNode } from "react";

// Admin route group: pages render their own <AdminChrome title=...> (the
// page title lives in the header, so it can't come from a layout). This
// boundary lets pages read cookies()/headers() at the top level while the
// static shell still prerenders (cacheComponents rule).
export default function AdminLayout({ children }: { children: ReactNode }) {
  return <Suspense fallback={null}>{children}</Suspense>;
}
