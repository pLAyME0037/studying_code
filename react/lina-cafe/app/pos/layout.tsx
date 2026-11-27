import { Suspense } from "react";
import type { ReactNode } from "react";

// Same boundary as the (auth)/(admin)/(shop) + notes/users layouts: the
// master_child pages read cookies()/headers()/searchParams at the top
// level, so they sit behind a Suspense edge while the static shell
// prerenders (cacheComponents rule).
export default function PosLayout({ children }: { children: ReactNode }) {
  return <Suspense fallback={null}>{children}</Suspense>;
}
