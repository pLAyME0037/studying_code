#!/usr/bin/env python3
"""Black-box HTTP suite for webc.

Spawned by test/run.sh against a throwaway HOME (never touches the real DB).
Uploads land in ./resource/image/upload/ (project cwd); created paths are
recorded in $TMP/uploads.txt so run.sh can clean them up.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from testlib import Client, Checker, db_query, db_exec  # noqa: E402

HOST = sys.argv[1]
PORT = int(sys.argv[2])
TMP = sys.argv[3]

DB = os.path.join(TMP, "nested", "deep", "db")
UPLOADS_STATE = os.path.join(TMP, "uploads.txt")
PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PNG_PATH = os.path.join(PROJECT, "resource", "image", "user1.png")

c = Client(HOST, PORT)
t = Checker()


def record_upload(profile_pic):
    """Remember upload path for run.sh cleanup; returns disk path."""
    with open(UPLOADS_STATE, "a") as f:
        f.write(profile_pic + "\n")
    return os.path.join(PROJECT, profile_pic.lstrip("/"))


def main():
    with open(PNG_PATH, "rb") as f:
        png = f.read()

    # ---- baseline pages ------------------------------------------------
    t.chk("home", c.get("/")[0], 200)
    st, _, ct = c.get("/resource/image/user1.png")
    t.chk("bundle resource", f"{st} {ct}", "200 image/png")
    t.chk("users page", c.get("/users")[0], 200)
    t.chk("notes page", c.get("/notes")[0], 200)

    # ---- migration files pin byte-exact history for existing DBs ----------
    for mig in ("0001_notes", "0002_users"):
        got = open(os.path.join(PROJECT, "migrations", mig, "sqlite3.sql"),
                   "rb").read()
        want = open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                 "golden", f"{mig}.sqlite3.sql"), "rb").read()
        t.chk(f"golden migration {mig}", got, want)

    # ---- seed: legacy numeric id row ----------------------------------
    db_exec(
        DB,
        "INSERT INTO users (id, name, username, email, profile_pic) "
        "VALUES ('7', 'Legacy', 'legacy7', 'l7@t.com', NULL)",
    )

    # ---- create: no file -> empty pic ---------------------------------
    st, _, _ = c.post_multipart(
        "/users/create",
        {"name": "No Pic", "username": "nopic_test", "email": "n@t.com",
         "profile_pic": ""},
    )
    t.chk("create no file", st, 302)
    t.chk(
        "no file -> empty pic",
        db_query(DB, "SELECT quote(profile_pic) FROM users "
                     "WHERE username='nopic_test'"),
        "''",
    )

    # ---- create: browser-style empty file part (filename="") ----------
    st, _, _ = c.post_multipart(
        "/users/create",
        {"name": "Raw Empty", "username": "raw_empty", "email": "r@t.com"},
        files={"profile_pic": ("", b"", "application/octet-stream")},
    )
    t.chk("create empty file part", st, 302)
    t.chk(
        "empty file part -> NULL pic",
        db_query(DB, "SELECT profile_pic FROM users WHERE username='raw_empty'"),
        "None",
    )

    # ---- create: real file upload -------------------------------------
    st, _, _ = c.post_multipart(
        "/users/create",
        {"name": "UUID Life", "username": "uuid_life", "email": "u@t.com"},
        files={"profile_pic": ("user1.png", png, "image/png")},
    )
    t.chk("create w/file", st, 302)
    uid = db_query(DB, "SELECT id FROM users WHERE username='uuid_life'")
    pic = db_query(DB, "SELECT profile_pic FROM users WHERE id=?", (uid,))
    t.chk("pic stored as upload url",
          str(pic).startswith("/resource/image/upload/") and
          pic.endswith(".webp"), "True")

    disk_path = record_upload(pic)
    t.chk("upload file on disk", os.path.exists(disk_path), "True")
    st, data, ct = c.get(pic)
    t.chk("avatar served", f"{st} {ct}", "200 image/webp")
    t.chk("avatar is webp", data[:4] == b"RIFF" and data[8:12] == b"WEBP",
          "True")

    # ---- edit / list render ids ---------------------------------------
    st, body, _ = c.get(f"/users/{uid}/edit")
    t.chk("edit page (uuid)", st, 200)
    t.chk("edit page shows id", body.decode().count(uid), 2)
    st, body, _ = c.get("/users")
    t.chk("list shows uuid id", body.decode().count(uid), 3)
    t.chk("list has ui-avatars fallback", body.decode().count("ui-avatars") >= 1,
          "True")

    # ---- update: with file replaces, without keeps --------------------
    st, _, _ = c.post_multipart(
        f"/users/{uid}/update",
        {"name": "UUID Life", "username": "uuid_life", "email": "u@t.com"},
        files={"profile_pic": ("user1.png", png, "image/png")},
    )
    t.chk("update w/file", st, 302)
    pic2 = db_query(DB, "SELECT profile_pic FROM users WHERE id=?", (uid,))
    record_upload(pic2)
    t.chk("update replaced pic",
          f"{pic2 != pic and '/upload/' in pic2}", "True")
    st, _, _ = c.post_multipart(
        f"/users/{uid}/update",
        {"name": "UUID Renamed", "username": "uuid_life", "email": "u@t.com",
         "profile_pic": ""},
    )
    t.chk("update w/o file", st, 302)
    t.chk("update kept pic",
          db_query(DB, "SELECT profile_pic FROM users WHERE id=?", (uid,)),
          pic2)
    t.chk("update renamed",
          db_query(DB, "SELECT name FROM users WHERE id=?", (uid,)),
          "UUID Renamed")

    # ---- legacy numeric id --------------------------------------------
    t.chk("edit page (legacy 7)", c.get("/users/7/edit")[0], 200)
    st, _, _ = c.post_multipart(
        "/users/7/update",
        {"name": "Legacy Renamed", "username": "legacy7", "email": "l7@t.com",
         "profile_pic": ""},
    )
    t.chk("update legacy", st, 302)
    t.chk("legacy kept NULL pic",
          db_query(DB, "SELECT profile_pic FROM users WHERE id='7'"),
          "None")

    # ---- delete --------------------------------------------------------
    t.chk("delete uuid", c.req("POST", f"/users/{uid}/delete")[0], 302)
    t.chk("row gone", db_query(DB, "SELECT count(*) FROM users WHERE id=?",
                               (uid,)), 0)

    # ---- validation ----------------------------------------------------
    st, _, _ = c.post_multipart(
        "/users/create",
        {"name": "NoUser", "email": "x@x.com", "profile_pic": ""},
    )
    t.chk("missing field", st, 400)
    st, _, _ = c.post_multipart(
        "/users/create",
        {"name": "Fake", "username": "fake_test", "email": "f@t.com"},
        files={"profile_pic": ("fake.png", b"plain text not an image",
                               "image/png")},
    )
    t.chk("non-image bytes", st, 400)

    big3 = b"\x89PNG\r\n\x1a\n" + b"\0" * (3 * 1024 * 1024)
    st, _, _ = c.post_multipart(
        "/users/create",
        {"name": "Big", "username": "big_test", "email": "b@t.com"},
        files={"profile_pic": ("big.png", big3, "image/png")},
    )
    t.chk("3MB image cap", st, 413)

    huge = b"\x89PNG\r\n\x1a\n" + b"\0" * (9 * 1024 * 1024)
    st, _, _ = c.post_multipart(
        "/users/create",
        {"name": "Huge", "username": "huge_test", "email": "h@t.com"},
        files={"profile_pic": ("huge.png", huge, "image/png")},
    )
    t.chk("9MB body cap", st, 413)

    t.chk("traversal blocked",
          c.get("/resource/../../src/webc.c")[0], 404)
    t.chk("garbage id", c.get("/users/no-such-id/edit")[0], 404)

    # ---- notes: urlencoded form CRUD ----------------------------------
    st, _, _ = c.post_urlencoded("/notes/create",
                                 {"title": "nt", "body": "nb"})
    t.chk("notes urlencoded create", st, 302)
    nid = db_query(DB, "SELECT id FROM notes WHERE title='nt'")
    st, body, _ = c.get(f"/notes/{nid}/edit")
    t.chk("note edit (uuid)", st, 200)
    t.chk("note edit shows id", body.decode().count(nid), 2)
    st, _, _ = c.post_urlencoded(f"/notes/{nid}/update",
                                 {"title": "nt2", "body": "nb2"})
    t.chk("note update", st, 302)
    t.chk("note updated",
          db_query(DB, "SELECT title FROM notes WHERE id=?", (nid,)), "nt2")

    # ---- notes JSON API ------------------------------------------------
    st, data, _ = c.get("/api/notes")
    import json

    notes = json.loads(data)
    t.chk("api GET", st, 200)
    t.chk("api ids are strings",
          all(isinstance(n["id"], str) for n in notes), "True")

    st, _, _ = c.post_json("/api/notes", {"title": "api_t", "body": "api_b"})
    t.chk("api POST", st, 200)
    api_id = db_query(DB, "SELECT id FROM notes WHERE title='api_t'")
    t.chk("api POST row", api_id is not None, "True")

    st, _, _ = c.req("PUT", "/api/notes",
                     json.dumps({"id": api_id, "title": "api_t3",
                                 "body": "api_b3"}).encode(),
                     {"Content-Type": "application/json"})
    t.chk("api PUT", st, 200)
    t.chk("api PUT updated",
          db_query(DB, "SELECT title FROM notes WHERE id=?", (api_id,)),
          "api_t3")

    st, _, _ = c.req("DELETE", "/api/notes",
                     json.dumps({"id": api_id}).encode(),
                     {"Content-Type": "application/json"})
    t.chk("api DELETE", st, 200)
    t.chk("api DELETE gone",
          db_query(DB, "SELECT count(*) FROM notes WHERE id=?", (api_id,)), 0)

    st, _, _ = c.req("PATCH", "/api/notes",
                     json.dumps({"id": "x"}).encode(),
                     {"Content-Type": "application/json"})
    t.chk("api PATCH -> 405", st, 405)

    # ---- delete leftover note row --------------------------------------
    t.chk("note delete route",
          c.req("POST", f"/notes/{nid}/delete")[0], 302)
    t.chk("note row gone",
          db_query(DB, "SELECT count(*) FROM notes WHERE id=?", (nid,)), 0)

    ok = t.summary()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
