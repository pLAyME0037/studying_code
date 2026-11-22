import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync, unlinkSync, writeFileSync } from "node:fs";
import path from "node:path";

// Image uploads: port of webc_pos src/http/utils.c save_uploaded_image().
// 2 MB cap -> magic-byte check -> ffmpeg webp (max 512px, q80) -> stored
// under data/image/upload/ and served from /resource/image/upload/<name>
// (the URL shape webc_pos produces; its ./resource/image/upload lives
// outside public/, so the Next port keeps the bytes in data/ and serves
// them through a route handler).
export const UPLOAD_DIR = path.join(process.cwd(), "data", "image", "upload");
export const UPLOAD_URL = "/resource/image/upload";
export const MAX_UPLOAD_IMAGE_SIZE = 2 * 1024 * 1024; // 2 MB

export type UploadResult =
  | { code: "ok"; url: string }
  | { code: "too_large" }
  | { code: "not_image" }
  | { code: "io" };

// webc_pos looks_like_image(): PNG / JPEG / GIF8 / RIFF....WEBP.
function looksLikeImage(b: Uint8Array): boolean {
  if (
    b.length >= 8 &&
    b[0] === 0x89 &&
    b[1] === 0x50 &&
    b[2] === 0x4e &&
    b[3] === 0x47
  ) {
    return true;
  }
  if (b.length >= 3 && b[0] === 0xff && b[1] === 0xd8 && b[2] === 0xff) {
    return true;
  }
  if (
    b.length >= 4 &&
    b[0] === 0x47 &&
    b[1] === 0x49 &&
    b[2] === 0x46 &&
    b[3] === 0x38
  ) {
    return true;
  }
  if (
    b.length >= 12 &&
    b[0] === 0x52 &&
    b[1] === 0x49 &&
    b[2] === 0x46 &&
    b[3] === 0x46 &&
    b[8] === 0x57 &&
    b[9] === 0x45 &&
    b[10] === 0x42 &&
    b[11] === 0x50
  ) {
    return true;
  }
  return false;
}

// webc keeps a process-wide seq counter; the stamp is time ^ (pid << 32).
let seq = 0;

export async function saveUploadedImage(buf: ArrayBuffer): Promise<UploadResult> {
  const data = new Uint8Array(buf);
  if (data.byteLength > MAX_UPLOAD_IMAGE_SIZE) return { code: "too_large" };
  if (!looksLikeImage(data)) return { code: "not_image" };

  try {
    mkdirSync(UPLOAD_DIR, { recursive: true });
  } catch {
    return { code: "io" };
  }

  seq += 1;
  const stamp = (
    BigInt(Math.floor(Date.now() / 1000)) ^
    (BigInt(process.pid) << BigInt(32))
  ).toString();
  const outName = `${stamp}_${seq}.webp`;
  const tmpPath = path.join(UPLOAD_DIR, `.${stamp}_${seq}.tmp`);
  const outPath = path.join(UPLOAD_DIR, outName);

  try {
    writeFileSync(tmpPath, data);
  } catch {
    return { code: "io" };
  }

  // Shrink to at most 512px and recompress as webp (keeps transparency).
  const run = spawnSync("ffmpeg", [
    "-nostdin",
    "-y",
    "-loglevel",
    "error",
    "-i",
    tmpPath,
    "-vf",
    "scale='min(512,iw)':-2",
    "-frames:v",
    "1",
    "-c:v",
    "libwebp",
    "-quality",
    "80",
    "-compression_level",
    "6",
    outPath,
  ]);

  try {
    unlinkSync(tmpPath);
  } catch {
    // already gone
  }

  const ok = run.status === 0 && existsSync(outPath);
  if (!ok) {
    try {
      if (existsSync(outPath)) unlinkSync(outPath);
    } catch {
      // best effort, like webc delete_file
    }
    return { code: "not_image" };
  }
  return { code: "ok", url: `${UPLOAD_URL}/${outName}` };
}
