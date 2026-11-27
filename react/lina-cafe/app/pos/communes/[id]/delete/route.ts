import {
  MD_COMMUNES,
  mdDeleteHandler,
  mdGet404,
  mdWriteTarget,
} from "@/app/lib/master-child";

// POST /pos/communes/:id/delete — webc SERVE_SOFT_DELETE(pos_communes, …).
export const POST = mdDeleteHandler(mdWriteTarget(MD_COMMUNES));
export const GET = mdGet404;
