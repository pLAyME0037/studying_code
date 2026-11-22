import type { NextRequest } from "next/server";
import { db } from "@/app/lib/db";
import {
  dbRun,
  errorPage,
  formText,
  isUniqueMsg,
  redirect302,
  uniquePage,
} from "@/app/lib/crud";
import { saveUploadedImage } from "@/app/lib/upload";

// POST /users/create — webc_pos SERVE_CREATE(users): five required text
// keys (missing key -> 400, empty first_name -> 400), profile_pic is the
// optional slot (a real file is magic-checked + ffmpeg'd to webp first),
// INSERT OR REPLACE exactly like webc's sqlite dialect, 302 back.
export async function POST(request: NextRequest): Promise<Response> {
  const form = await request.formData();
  const firstName = formText(form, "first_name");
  const lastName = formText(form, "last_name");
  const username = formText(form, "username");
  const email = formText(form, "email");
  const phone = formText(form, "phone");
  if (
    firstName === null ||
    lastName === null ||
    username === null ||
    email === null ||
    phone === null ||
    firstName.length === 0
  ) {
    return errorPage(400, "400 Bad Request");
  }

  let picUrl = "";
  const pic = form.get("profile_pic");
  if (pic instanceof File && pic.size > 0) {
    const up = await saveUploadedImage(await pic.arrayBuffer());
    if (up.code === "too_large") return errorPage(413, "413 Payload Too Large");
    if (up.code === "not_image") return errorPage(400, "400 Bad Request");
    if (up.code === "io") return errorPage(500, "500 Internal Server Error");
    picUrl = up.url;
  }

  const msg = dbRun(() => {
    db.prepare(
      "INSERT OR REPLACE INTO Users " +
        "(first_name, last_name, username, email, phone, profile_pic) " +
        "VALUES (?, ?, ?, ?, ?, ?);",
    ).run(
      firstName,
      lastName,
      username,
      email,
      phone,
      picUrl.length > 0 ? picUrl : null,
    );
  });
  if (msg !== null) {
    if (isUniqueMsg(msg)) return uniquePage("user", msg);
    return errorPage(500, "500 Internal Server Error");
  }
  return redirect302(request, "/users");
}

// webc routes /users/create as POST only — a GET falls through to 404.
export async function GET(): Promise<Response> {
  return errorPage(404, "404 Not Found");
}
