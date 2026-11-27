import {
  MD_PROVINCES,
  mdCreateHandler,
  mdGet404,
  mdWriteTarget,
} from "@/app/lib/master-child";

// POST /pos/provinces/create — webc SERVE_CREATE(pos_provinces, …).
// GET answers 404 (webc registers the route POST-only).
export const POST = mdCreateHandler(mdWriteTarget(MD_PROVINCES));
export const GET = mdGet404;
