#!/usr/bin/env python3
"""Black-box HTTP suite for webc.

Spawned by test/run.sh against a throwaway HOME (never touches the real DB).

Baseline after the demo-app removal: pages + static assets + raw-socket
regressions + the empty migration history. DB Admin feature suites land in
test/dbadmin_test.py as their phases ship.
"""
import os
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from testlib import Client, Checker, db_query  # noqa: E402

HOST = sys.argv[1]
PORT = int(sys.argv[2])
TMP = sys.argv[3]

DB = os.path.join(TMP, "nested", "deep", "db")

c = Client(HOST, PORT)
t = Checker()


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
    # ---- baseline pages ------------------------------------------------
    t.chk("home", c.get("/")[0], 200)
    st, body, _ = c.get("/")
    t.chk("home is the version page", b"Version" in body, True)
    t.chk("version page", c.get("/version")[0], 200)
    st, body, _ = c.get("/version")
    t.chk("page branded webc", b"webc" in body, True)
    t.chk("no clinic branding", b"Clinic" in body, False)

    # ---- static assets (bundle + resources) -----------------------------
    st, _, ct = c.get("/resource/image/user1.png")
    t.chk("bundle resource", f"{st} {ct}", "200 image/png")
    st, _, ct = c.get("/css/output.css")
    t.chk("css", f"{st} {ct}", "200 text/css; charset=utf-8")
    st, _, ct = c.get("/css/db.css")
    t.chk("db workspace css", f"{st} {ct}", "200 text/css; charset=utf-8")
    st, _, ct = c.get("/js/themeSwitcher.js")
    t.chk("js", f"{st} {ct}", "200 text/javascript; charset=utf-8")
    t.chk("favicon", c.get("/favicon.ico")[0], 200)

    # ---- head terminator straddling two reads must not hang ------------
    head = b"GET / HTTP/1.1\r\nHost: hang\r\nConnection: close\r\n\r\n"
    term = head.rindex(b"\r\n\r\n")
    for k in (1, 2, 3):
        resp = split_head_get(head, term + k)
        t.chk(f"split head at term+{k}", resp.startswith(b"HTTP/1.1"), "True")

    # ---- demo-app routes are gone --------------------------------------
    for route in ("/notes", "/users", "/people", "/api/notes",
                  "/notes/create", "/users/7/edit"):
        t.chk(f"removed route {route}", c.get(route)[0], 404)

    # ---- 404 for unknown paths, path traversal blocked ------------------
    t.chk("unknown route", c.get("/no-such-page")[0], 404)
    t.chk("traversal blocked",
          c.get("/resource/../../src/webc.c")[0], 404)

    # ---- sidebar carries only real links -------------------------------
    st, body, _ = c.get("/")
    html = body.decode()
    t.chk("sidebar has version link", 'href="/version"' in html, True)
    t.chk("sidebar drops demo links",
          any(x in html for x in ('href="/notes"', 'href="/users"',
                                  'href="/people"')), False)

    # ---- app database: empty migration history -------------------------
    t.chk("db file exists", os.path.exists(DB), True)
    t.chk("bootstrap Migrations table",
          db_query(DB, "SELECT count(*) FROM sqlite_master "
                       "WHERE type='table' AND name='Migrations'"), 1)
    t.chk("migration history empty",
          db_query(DB, "SELECT count(*) FROM Migrations"), 0)
    t.chk("no demo tables",
          db_query(DB, "SELECT count(*) FROM sqlite_master "
                       "WHERE name IN ('notes', 'users')"), 0)

    ok = t.summary()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
