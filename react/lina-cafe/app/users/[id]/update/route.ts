import type { NextRequest } from "next/server";
import { db } from "@/app/lib/db";
import {
  dbRun,
  errorPage,
  formText,
  idValid,
  isUniqueMsg,
  redirect302,
  uniquePage,
} from "@/app/lib/crud";
import { saveUploadedImage } from "@/app/lib/upload";

// POST /users/<id>/update — webc_pos SERVE_UPDATE(users): invalid id -> 404,
// five required keys missing or empty first_name -> 400, an optional new
// profile_pic file goes through save_uploaded_image first (413/400/500),
// then the COALESCE(NULLIF(...)) UPDATE keeps phone/picture when the form
// left them blank, 409 on a unique clash, 302 back.
type Params = { params: Promise<{ id: string }> };

export async function POST(request: NextRequest, { params }: Params): Promise<Response> {
  const { id } = await params;
  if (!idValid(id)) return errorPage(404, "404 Not Found");

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
    // Empty phone / picture bind "" and COALESCE(NULLIF('')) keeps the
    // stored value — editing name/email never wipes them (webc comment
    // on update_user).
    db.prepare(
      "UPDATE Users SET first_name = ?, last_name = ?, " +
        "username = ?, email = ?, " +
        "phone = COALESCE(NULLIF(?, ''), phone), " +
        "profile_pic = COALESCE(NULLIF(?, ''), profile_pic) " +
        "WHERE id = ?;",
    ).run(firstName, lastName, username, email, phone, picUrl, id);
  });
  if (msg !== null) {
    if (isUniqueMsg(msg)) return uniquePage("user", msg);
    return errorPage(500, "500 Internal Server Error");
  }
  return redirect302(request, "/users");
}

// webc routes /users/<id>/update as POST only — a GET falls through to 404.
export async function GET(): Promise<Response> {
  return errorPage(404, "404 Not Found");
}
