#!/usr/bin/env python3
"""Black-box HTTP suite for webc.

Spawned by test/run.sh against a throwaway HOME (never touches the real DB).
Uploads land in ./resource/image/upload/ (project cwd); created paths are
recorded in $TMP/uploads.txt so run.sh can clean them up.
"""
import os
import socket
import sys
import time
import urllib.parse

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


def split_head_get(head, cut, timeout=3.0):
    """Send request head as two writes split at `cut`, return raw response.

    Pins read_until_double_crlf(): a \\r\\n\\r\\n straddling two reads must still
    be detected (regression: scan frontier never rewound -> connection hung
    forever, browser spinner until refresh).
    """
    s = socket.create_connection((HOST, PORT), timeout=timeout)
    try:
        s.settimeout(timeout)
        s.sendall(head[:cut])
        time.sleep(0.05)  # force the server's read() to return after chunk 1
        s.sendall(head[cut:])
        data = b""
        while b"\r\n\r\n" not in data:
            chunk = s.recv(65536)
            if not chunk:
                break
            data += chunk
        return data
    finally:
        s.close()


def main():
    with open(PNG_PATH, "rb") as f:
        png = f.read()

    # ---- baseline pages ------------------------------------------------
    t.chk("home", c.get("/")[0], 200)
    st, _, ct = c.get("/resource/image/user1.png")
    t.chk("bundle resource", f"{st} {ct}", "200 image/png")
    t.chk("users page", c.get("/users")[0], 200)
    t.chk("notes page", c.get("/notes")[0], 200)

    # ---- head terminator straddling two reads must not hang ------------
    head = b"GET / HTTP/1.1\r\nHost: hang\r\nConnection: close\r\n\r\n"
    term = head.rindex(b"\r\n\r\n")
    for k in (1, 2, 3):
        resp = split_head_get(head, term + k)
        t.chk(f"split head at term+{k}", resp.startswith(b"HTTP/1.1"), "True")

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

    # ---- /people master-detail (users master, notes child) --------------
    def urlenc(fields):
        return urllib.parse.urlencode(fields).encode()

    def post_form(path, fields):
        return c.req_full(
            "POST", path, urlenc(fields),
            {"Content-Type": "application/x-www-form-urlencoded"})

    st, body, _ = c.get("/people")
    t.chk("people page", st, 200)
    html = body.decode()
    t.chk("people sidebar link", 'href="/people"' in html, "True")
    t.chk("people master create form",
          'action="/users/create?redirect=/people"' in html, "True")
    t.chk("people lists legacy user", "Legacy Renamed" in html, "True")

    # master create via the MD form: no profile_pic key -> NULL pic
    st, hdr, _, _ = post_form(
        "/users/create?redirect=/people",
        {"name": "MD Person", "username": "md_person",
         "email": "md@p.com"})
    t.chk("md user create", f"{st} {hdr.get('Location')}", "302 /people")
    t.chk("md user pic NULL",
          db_query(DB, "SELECT profile_pic FROM users "
                       "WHERE username='md_person'"),
          "None")

    # child create: the hidden form action carries the FK in the query
    st, hdr, _, _ = post_form(
        "/notes/create?user_id=7&redirect=/people",
        {"title": "md_child_note", "body": "via people"})
    t.chk("md note create", f"{st} {hdr.get('Location')}", "302 /people")
    t.chk("md note linked to user",
          db_query(DB, "SELECT user_id FROM notes WHERE title='md_child_note'"),
          "7")

    st, body, _ = c.get("/people")
    html = body.decode()
    t.chk("md child row shown", "md_child_note" in html, "True")
    t.chk("md fk badge shown", "user_id: 7" in html, "True")

    # reveal contract: swap root, per-row ids (locate exact new row after
    # reload/swap; row order is random uuid), explicit form scopes
    t.chk("mc swap root id", 'id="mc-root"' in html, "True")
    t.chk("md rows carry ids", html.count("data-row-id=") >= 2, "True")
    t.chk("md form scopes",
          'data-md-kind="master"' in html and 'data-md-kind="child"' in html,
          "True")

    # redirect fallback (no param) + open-redirect guard
    st, hdr, _, _ = post_form("/notes/create",
                              {"title": "fallback_note", "body": "x"})
    t.chk("redirect fallback", f"{st} {hdr.get('Location')}", "302 /notes")
    t.chk("fallback note not linked",
          db_query(DB, "SELECT user_id FROM notes "
                       "WHERE title='fallback_note'"),
          "None")
    st, hdr, _, _ = post_form("/notes/create?redirect=//evil.example",
                              {"title": "evil_note", "body": "x"})
    t.chk("open redirect blocked", hdr.get("Location"), "/notes")

    # delete with ?redirect= comes back to /people
    mid = db_query(DB, "SELECT id FROM notes WHERE title='md_child_note'")
    st, hdr, _, _ = c.req_full("POST", f"/notes/{mid}/delete?redirect=/people")
    t.chk("md delete back to people",
          f"{st} {hdr.get('Location')}", "302 /people")
    t.chk("md delete removed row",
          db_query(DB, "SELECT count(*) FROM notes WHERE id=?", (mid,)), 0)

    ok = t.summary()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
