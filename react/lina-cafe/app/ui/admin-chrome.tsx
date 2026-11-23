import { Suspense } from "react";
import type { ReactNode } from "react";
import { AdminHeader } from "./admin-header";
import { AdminFooter } from "./admin-footer";
import { Sidebar } from "./sidebar";
import { SidebarInit } from "./sidebar-init";

// display/layout/header.h + footer.h — the full POS chrome host that
// webc_pos's render_page_header(title, path) + render_page_footer wrap
// page content in. Sync wrapper: every runtime-dependent piece (session,
// translations, path) streams behind its own <Suspense>, so the chrome
// frame is part of the static shell.
export function AdminChrome({
  title,
  children,
}: {
  title: string;
  children: ReactNode;
}) {
  return (
    <div className="h-full bg-base text-text transition-colors duration-300 flex flex-col">
      <div className="flex flex-col h-screen">
        <Suspense
          fallback={
            <header className="shrink-0 sticky top-0 z-30 bg-mantle shadow-sm border-b border-surface0">
              <div className="flex items-center justify-between px-4 py-1" />
            </header>
          }
        >
          <AdminHeader title={title} />
        </Suspense>

        <div className="flex-1 flex overflow-hidden relative min-h-0">
          <Suspense
            fallback={
              <aside
                id="sidebar"
                className="shrink-0 border-r border-surface0 bg-mantle"
              />
            }
          >
            <Sidebar />
          </Suspense>

          <div className="flex-1 flex flex-col min-w-0">
            <main className="flex-1 overflow-y-auto flex flex-col min-w-0 bg-base">
              <div className="flex-1 py-6 px-4 sm:px-6 lg:px-8">
                {children}
              </div>
            </main>
            <AdminFooter />
          </div>
        </div>
      </div>
      <SidebarInit />
      {/* webc loads these at the end of <body> (after the DOM exists);
          defer keeps that timing without blocking the parse. */}
      <script src="/js/sidebarSwitcher.js" defer />
      <script src="/js/PaginationSwitcher.js" defer />
      <script src="/js/ImageUpload.js" defer />
    </div>
  );
}
