import {
  MD_COMMUNES,
  mdGet404,
  mdUpdateHandler,
  mdWriteTarget,
} from "@/app/lib/master-child";

// POST /pos/communes/:id/update — webc SERVE_UPDATE(pos_communes, …).
export const POST = mdUpdateHandler(mdWriteTarget(MD_COMMUNES));
export const GET = mdGet404;
