import {
  MD_PROVINCES,
  mdGet404,
  mdUpdateHandler,
  mdWriteTarget,
} from "@/app/lib/master-child";

// POST /pos/provinces/:id/update — webc SERVE_UPDATE(pos_provinces, …).
export const POST = mdUpdateHandler(mdWriteTarget(MD_PROVINCES));
export const GET = mdGet404;
