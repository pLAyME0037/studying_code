import {
  MD_DISTRICTS,
  mdDeleteHandler,
  mdGet404,
  mdWriteTarget,
} from "@/app/lib/master-child";

// POST /pos/districts/:id/delete — webc SERVE_SOFT_DELETE(pos_districts, …).
export const POST = mdDeleteHandler(mdWriteTarget(MD_DISTRICTS));
export const GET = mdGet404;
