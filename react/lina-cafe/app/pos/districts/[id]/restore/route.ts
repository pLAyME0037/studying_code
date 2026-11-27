import {
  MD_DISTRICTS,
  mdGet404,
  mdRestoreHandler,
  mdWriteTarget,
} from "@/app/lib/master-child";

// POST /pos/districts/:id/restore — webc's restore half of
// SERVE_SOFT_DELETE(pos_districts, …).
export const POST = mdRestoreHandler(mdWriteTarget(MD_DISTRICTS));
export const GET = mdGet404;
