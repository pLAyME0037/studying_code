// The auth group renders BARE (webc_pos render_page_shell only — no shop or
// admin chrome), with the group content behind a boundary so pages may read
// cookies()/headers() at the top level while the shell stays static.
import { Fragment, Suspense } from "react";

export default function AuthLayout({ children }: { children: React.ReactNode }) {
  return (
    <Fragment>
      <Suspense fallback={null}>{children}</Suspense>
    </Fragment>
  );
}
