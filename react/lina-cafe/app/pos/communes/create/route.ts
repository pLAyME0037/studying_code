import {
  MD_COMMUNES,
  mdCreateHandler,
  mdGet404,
  mdWriteTarget,
} from "@/app/lib/master-child";

// POST /pos/communes/create — webc SERVE_CREATE(pos_communes, …).
export const POST = mdCreateHandler(mdWriteTarget(MD_COMMUNES));
export const GET = mdGet404;
