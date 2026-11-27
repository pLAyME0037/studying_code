import {
  MD_DICTIONARIES,
  mdCreateHandler,
  mdGet404,
  mdWriteTarget,
} from "@/app/lib/master-child";

// POST /pos/dictionaries/create — webc's create-only dict route
// (SERVE_CREATE(pos_dict, pos_dict, …)); webc registers no
// update/delete/restore here, and GET answers 404.
export const POST = mdCreateHandler(mdWriteTarget(MD_DICTIONARIES));
export const GET = mdGet404;
