import {
  MD_DISTRICTS,
  mdGet404,
  mdUpdateHandler,
  mdWriteTarget,
} from "@/app/lib/master-child";

// POST /pos/districts/:id/update — webc SERVE_UPDATE(pos_districts, …).
export const POST = mdUpdateHandler(mdWriteTarget(MD_DISTRICTS));
export const GET = mdGet404;
