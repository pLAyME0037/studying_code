import { MasterChildPage } from "@/app/ui/master-child";
import { MD_GEO } from "@/app/lib/master-child";

// GET /pos/geo — webc_pos serve_pos_geo_workspace (geo.c): the
// v_geo_hierarchy read-only workspace (soft-deleted rows behind
// ?deleted=1), no children, no writes.
type SearchParams = Promise<Record<string, string | string[] | undefined>>;

export async function generateMetadata() {
  return { title: "Geo Locations" };
}

export default async function GeoPage({
  searchParams,
}: {
  searchParams: SearchParams;
}) {
  return <MasterChildPage config={MD_GEO} searchParams={searchParams} />;
}
