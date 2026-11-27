import { MasterChildPage } from "@/app/ui/master-child";
import { MD_DICTIONARIES } from "@/app/lib/master-child";

// GET /pos/dictionaries — webc_pos serve_pos_dict (pos.c): create_only
// (add form + /create; webc registers no update/delete/restore — the
// port's route.ts exposes POST create only).
type SearchParams = Promise<Record<string, string | string[] | undefined>>;

export async function generateMetadata() {
  return { title: "Dictionaries" };
}

export default async function DictionariesPage({
  searchParams,
}: {
  searchParams: SearchParams;
}) {
  return (
    <MasterChildPage config={MD_DICTIONARIES} searchParams={searchParams} />
  );
}
