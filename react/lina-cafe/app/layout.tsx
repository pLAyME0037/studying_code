import type { Metadata } from "next";
import type { ReactNode } from "react";
import "./globals.css";

export const metadata: Metadata = {
  title: "Example WebC Site",
  description: "POS · webc_pos port to Next.js",
};

// Runs before first paint (no flash, no extra request): theme controller —
// verbatim js/themeSwitcher.js logic — plus the four head-level behaviors
// webc_pos spreads across scripts/templates:
//   1. theme buttons: RSC markup carries data-set-theme (delegation here
//      replaces the templates' inline onclick, which React can't render);
//   2. sidebar collapse class — inline <script> from layout/header.h.tt;
//   3. <html lang> follows the webc_lang cookie (render_page_shell +
//      render_page_header read it server-side; here it runs client-side
//      so the static shell can stay prerenderable);
//   4. language-form auto-submit — lang_form.h.tt's inline onchange.
const BOOTSTRAP = `
window.themeController={
  get(){let t=localStorage.getItem('theme');if(!t)t=matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light';return t;},
  apply(t){if(t==='device')t=matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light';document.documentElement.classList.toggle('dark',t==='dark');},
  setTheme(t){localStorage.setItem('theme',t);this.apply(t);}
};
window.themeController.apply(window.themeController.get());
document.addEventListener('click',function(e){
  var b=e.target&&e.target.closest&&e.target.closest('[data-set-theme]');
  if(b)window.themeController.setTheme(b.getAttribute('data-set-theme'));
});
if(localStorage.getItem('sidebar_collapsed')==='true')document.documentElement.classList.add('sidebar-collapsed');
var m=document.cookie.match(/(?:^|;\\s*)webc_lang=([^;]+)/);
if(m){try{document.documentElement.lang=decodeURIComponent(m[1]);}catch(_){}}
document.addEventListener('change',function(e){
  var s=e.target;
  if(s&&s.tagName==='SELECT'&&s.form&&s.form.closest('[data-lang-form]'))s.form.submit();
});
`;

export default function RootLayout({ children }: { children: ReactNode }) {
  return (
    <html lang="km" suppressHydrationWarning>
      <head>
        <script dangerouslySetInnerHTML={{ __html: BOOTSTRAP }} />
      </head>
      <body>{children}</body>
    </html>
  );
}
