import {
  MD_PROVINCES,
  mdDeleteHandler,
  mdGet404,
  mdWriteTarget,
} from "@/app/lib/master-child";

// POST /pos/provinces/:id/delete — webc SERVE_SOFT_DELETE(pos_provinces, …).
export const POST = mdDeleteHandler(mdWriteTarget(MD_PROVINCES));
export const GET = mdGet404;
