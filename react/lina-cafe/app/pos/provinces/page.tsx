import { MasterChildPage } from "@/app/ui/master-child";
import { MD_PROVINCES } from "@/app/lib/master-child";

// GET /pos/provinces — webc_pos serve_pos_provinces (geo.c) +
// display/master_child.h.tt. Master page with the three child tabs
// (districts / communes / villages) — the only page in webc with children.
type SearchParams = Promise<Record<string, string | string[] | undefined>>;

export async function generateMetadata() {
  return { title: "Provinces" };
}

export default async function ProvincesPage({
  searchParams,
}: {
  searchParams: SearchParams;
}) {
  return <MasterChildPage config={MD_PROVINCES} searchParams={searchParams} />;
}
