import { MasterChildPage } from "@/app/ui/master-child";
import { MD_DISTRICTS } from "@/app/lib/master-child";

// GET /pos/districts — webc_pos serve_pos_districts (geo.c) +
// display/master_child.h.tt. No children: webc still renders the detail
// panel row per master row, and the thead omits the Actions th (kept).
type SearchParams = Promise<Record<string, string | string[] | undefined>>;

export async function generateMetadata() {
  return { title: "Districts" };
}

export default async function DistrictsPage({
  searchParams,
}: {
  searchParams: SearchParams;
}) {
  return <MasterChildPage config={MD_DISTRICTS} searchParams={searchParams} />;
}
