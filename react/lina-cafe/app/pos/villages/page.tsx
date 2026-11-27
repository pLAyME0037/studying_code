import { MasterChildPage } from "@/app/ui/master-child";
import { MD_VILLAGES } from "@/app/lib/master-child";

// GET /pos/villages — webc_pos serve_pos_villages (geo.c): read_only,
// GET route only (webc registers no /pos/villages write handlers — POSTs
// answer 404 there, and the port has no route.ts either).
type SearchParams = Promise<Record<string, string | string[] | undefined>>;

export async function generateMetadata() {
  return { title: "Villages" };
}

export default async function VillagesPage({
  searchParams,
}: {
  searchParams: SearchParams;
}) {
  return <MasterChildPage config={MD_VILLAGES} searchParams={searchParams} />;
}
