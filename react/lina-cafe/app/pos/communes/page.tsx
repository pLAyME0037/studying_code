import { MasterChildPage } from "@/app/ui/master-child";
import { MD_COMMUNES } from "@/app/lib/master-child";

// GET /pos/communes — webc_pos serve_pos_communes (geo.c) +
// display/master_child.h.tt. No children (webc parity with districts).
type SearchParams = Promise<Record<string, string | string[] | undefined>>;

export async function generateMetadata() {
  return { title: "Communes" };
}

export default async function CommunesPage({
  searchParams,
}: {
  searchParams: SearchParams;
}) {
  return <MasterChildPage config={MD_COMMUNES} searchParams={searchParams} />;
}
