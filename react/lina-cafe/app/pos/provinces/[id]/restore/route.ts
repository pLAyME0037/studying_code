import {
  MD_PROVINCES,
  mdGet404,
  mdRestoreHandler,
  mdWriteTarget,
} from "@/app/lib/master-child";

// POST /pos/provinces/:id/restore — webc's restore half of
// SERVE_SOFT_DELETE(pos_provinces, …).
export const POST = mdRestoreHandler(mdWriteTarget(MD_PROVINCES));
export const GET = mdGet404;
