import { getI18n } from "@/app/lib/i18n";
import { currentPath } from "@/app/lib/request";
import { LangForm } from "./lang-form";

// display/layout/header.h.tt (top bar half) — page title + theme buttons +
// language form. Theme buttons carry data-set-theme; the root layout's
// bootstrap handles the click (replacing the template's inline onclick,
// which React can't render). Async: the language form reads the languages
// table; streams behind AdminChrome's <Suspense>.
export async function AdminHeader({ title }: { title: string }) {
  const { tr } = await getI18n();
  const back = await currentPath();
  const themeBtn =
    "px-2 py-1 rounded text-sm text-subtext0 hover:bg-surface0 hover:text-text";
  return (
    <header className="shrink-0 sticky top-0 z-30 bg-mantle shadow-sm border-b border-surface0">
      <div className="flex items-center justify-between px-4 py-1">
        <div className="flex items-center gap-2">
          <button
            data-sidebar-toggle
            className="p-2 -ml-2 rounded-lg text-subtext0 hover:bg-surface0 focus:outline-none focus:ring-2 focus:ring-blue"
          >
            <svg
              className="w-6 h-6"
              fill="none"
              stroke="currentColor"
              viewBox="0 0 24 24"
            >
              <path
                strokeLinecap="round"
                strokeLinejoin="round"
                strokeWidth="2"
                d="M4 6h16M4 12h16M4 18h16"
              />
            </svg>
          </button>

          <div className="hidden md:flex items-center gap-2">
            <span className="font-bold text-xl text-text tracking-tight">
              POS Admin
            </span>
          </div>

          <div className="h-6 w-px bg-surface1 mx-2 hidden md:block" />

          <h1 className="text-lg font-semibold text-text truncate">{title}</h1>
        </div>

        <div className="flex items-center space-x-3">
          <div className="flex items-center gap-1">
            <button
              data-set-theme="light"
              className={themeBtn}
            >
              Light
            </button>
            <button
              data-set-theme="dark"
              className={themeBtn}
            >
              Dark
            </button>
            <button
              data-set-theme="device"
              className={themeBtn}
            >
              System
            </button>
          </div>
          <div className="flex items-center gap-1">
            <LangForm back={back} />
          </div>
        </div>
      </div>
    </header>
  );
}
