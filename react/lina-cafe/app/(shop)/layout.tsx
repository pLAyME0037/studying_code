import { Suspense } from "react";
import type { ReactNode } from "react";
import { ShopHeader } from "@/app/ui/shop-header";
import { ShopFooter } from "@/app/ui/shop-footer";

// Shop/auth page shell (webc_pos render_page_shell + chrome_start/end):
// static structure prerenders as the PPR shell; header/footer stream in
// behind Suspense (cookies/translations), children get their own boundary
// so pages may read cookies/headers at the top level.
export default function ShopLayout({ children }: { children: ReactNode }) {
  return (
    <div className="min-h-screen flex flex-col bg-base">
      <Suspense
        fallback={
          <header className="bg-mantle border-b border-surface0">
            <div className="max-w-7xl mx-auto px-3 py-2" />
          </header>
        }
      >
        <ShopHeader />
      </Suspense>
      <main className="max-w-7xl mx-auto w-full px-3 py-3 flex flex-col gap-3">
        <Suspense fallback={null}>{children}</Suspense>
      </main>
      <Suspense
        fallback={
          <footer className="bg-mantle border-t border-surface0 mt-auto">
            <div className="max-w-7xl mx-auto px-3 py-2 text-sm" />
          </footer>
        }
      >
        <ShopFooter />
      </Suspense>
    </div>
  );
}
