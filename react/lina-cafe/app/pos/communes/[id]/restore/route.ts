import {
  MD_COMMUNES,
  mdGet404,
  mdRestoreHandler,
  mdWriteTarget,
} from "@/app/lib/master-child";

// POST /pos/communes/:id/restore — webc's restore half of
// SERVE_SOFT_DELETE(pos_communes, …).
export const POST = mdRestoreHandler(mdWriteTarget(MD_COMMUNES));
export const GET = mdGet404;
