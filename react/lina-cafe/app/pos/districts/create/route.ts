import {
  MD_DISTRICTS,
  mdCreateHandler,
  mdGet404,
  mdWriteTarget,
} from "@/app/lib/master-child";

// POST /pos/districts/create — webc SERVE_CREATE(pos_districts, …).
export const POST = mdCreateHandler(mdWriteTarget(MD_DISTRICTS));
export const GET = mdGet404;
