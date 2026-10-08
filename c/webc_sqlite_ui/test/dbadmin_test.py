#!/usr/bin/env python3
"""DB Admin suite - Phase 1: open/switch arbitrary SQLite files.

Spawned by test/run.sh against a throwaway HOME. Exercises /db (overview +
open/new/close panel), /db/browse (directory listing), recents, validation
errors and the header active-file pill. Ends with the app DB active again.
"""
import json
import os
import re
import sqlite3
import sys
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from testlib import Client, Checker  # noqa: E402

HOST = sys.argv[1]
PORT = int(sys.argv[2])
TMP = sys.argv[3]

APP_DB = os.path.join(TMP, "nested", "deep", "db")
FIXTURE = os.path.join(TMP, "fixture.db")
FIX_SPACE = os.path.join(TMP, "we ird&db.sqlite")  # URI-encoding edge case
PLAIN = os.path.join(TMP, "notes.txt")
NEW_DB = os.path.join(TMP, "fresh", "new.db")
RECENTS = os.path.join(TMP, ".sqlite3", "webc_sqlite_ui", "recents")

c = Client(HOST, PORT)
t = Checker()

FORM = {"Content-Type": "application/x-www-form-urlencoded"}


def post_open(path, action="open", readonly=False):
    fields = {"path": path, "action": action}
    if readonly:
        fields["readonly"] = "on"
    return c.req_full(
        "POST", "/db/open", urllib.parse.urlencode(fields).encode(), FORM
    )


def redirect_of(resp):
    status, headers, _, _ = resp
    return f"{status} {headers.get('Location', '')}"


def main():
    # ---- fixtures -------------------------------------------------------
    conn = sqlite3.connect(FIXTURE)
    conn.execute("CREATE TABLE people(id INTEGER PRIMARY KEY, name TEXT)")
    conn.execute("INSERT INTO people(name) VALUES ('alice')")
    conn.commit()
    conn.close()
    conn = sqlite3.connect(FIX_SPACE)
    conn.execute("CREATE TABLE items(id INTEGER PRIMARY KEY)")
    conn.commit()
    conn.close()
    with open(PLAIN, "w") as f:
        f.write("just some text, not a database\n" * 4)

    # ---- /db overview on the application database -----------------------
    st, body, _ = c.get("/db")
    html = body.decode()
    t.chk("db page", st, 200)
    t.chk("app db path shown", APP_DB in html, True)
    t.chk("app badge", 'badge-app' in html, True)
    t.chk("bootstrap Migrations listed", "Migrations" in html, True)
    t.chk("open panel present", "Open a database" in html, True)
    t.chk("no active-file pill (app db)",
          'db-pill' in html, False)

    # ---- open a fixture -------------------------------------------------
    t.chk("open fixture", redirect_of(post_open(FIXTURE)),
          "302 /db?ok=opened")
    st, body, _ = c.get("/db")
    html = body.decode()
    t.chk("fixture path active", FIXTURE in html, True)
    t.chk("fixture table listed", "people" in html, True)
    t.chk("row count rendered",
          re.search(r"text-align:right[^>]*>\s*1\s*</td>", html) is not None,
          True)
    t.chk("active pill (header)", 'db-pill' in html,
          True)
    t.chk("pill basename", "fixture.db" in html, True)
    st, body, _ = c.get("/")
    t.chk("pill global in header",
          'db-pill' in body.decode(), True)

    # ---- file transport for the browser engine --------------------------
    st, body, ct = c.get("/db/file")
    t.chk("file transport returns active db bytes",
          (st, body[:16], len(body), ct),
          (200, b"SQLite format 3\x00", os.path.getsize(FIXTURE),
           "application/octet-stream"))

    # ---- save-back guards (P9) ------------------------------------------
    st, headers, db_bytes, _ = c.req_full("GET", "/db/file")
    meta = headers.get("X-DB-Meta", "")
    t.chk("change token on file transport", (st, bool(meta)), (200, True))
    t.chk("no WAL header without sidecar", "X-DB-Wal" in headers, False)
    RAW = {"Content-Type": "application/octet-stream"}

    st, _, resp, _ = c.req_full("POST", "/db/save", db_bytes,
                                dict(RAW, **{"X-DB-Meta": "0-0.0"}))
    info = json.loads(resp)
    t.chk("stale token rejected",
          (st, info.get("ok"), "changed on disk" in info.get("error", "")),
          (409, False, True))

    st, _, resp, _ = c.req_full("POST", "/db/save", b"not a database at all",
                                dict(RAW, **{"X-DB-Meta": meta}))
    info = json.loads(resp)
    t.chk("non-sqlite body rejected",
          (st, info.get("ok"), "not a SQLite" in info.get("error", "")),
          (400, False, True))

    wal = FIXTURE + "-wal"
    with open(wal, "wb") as f:
        f.write(b"\x00" * 8)
    st, wh, _, _ = c.req_full("GET", "/db/file")
    t.chk("WAL sidecar surfaced on file transport", wh.get("X-DB-Wal"), "1")
    st, _, resp, _ = c.req_full("POST", "/db/save", db_bytes,
                                dict(RAW, **{"X-DB-Meta": meta}))
    os.unlink(wal)
    info = json.loads(resp)
    t.chk("wal sidecar blocks save",
          (st, info.get("ok"), "WAL" in info.get("error", "")),
          (409, False, True))

    st, _, resp, _ = c.req_full("POST", "/db/save", db_bytes,
                                dict(RAW, **{"X-DB-Meta": meta}))
    info = json.loads(resp)
    t.chk("save-back accepted", (st, info.get("ok"), info.get("size")),
          (200, True, len(db_bytes)))
    bak = FIXTURE + ".bak"
    t.chk("backup written before replace",
          (os.path.exists(bak), open(bak, "rb").read() == db_bytes),
          (True, True))

    st, _, resp, _ = c.req_full("POST", "/db/save", db_bytes,
                                dict(RAW, **{"X-DB-Meta": meta}))
    t.chk("replayed meta rejected after save", st, 409)

    # ---- read-only ------------------------------------------------------
    t.chk("open read-only", redirect_of(post_open(FIXTURE, readonly=True)),
          "302 /db?ok=opened")
    st, body, _ = c.get("/db")
    t.chk("read-only badge", "badge-ro" in body.decode(), True)

    # ---- paths with spaces / & (URI percent-encoding) -------------------
    t.chk("open path with space+amp", redirect_of(post_open(FIX_SPACE)),
          "302 /db?ok=opened")
    st, body, _ = c.get("/db")
    t.chk("escaped path in page", "we ird&amp;db.sqlite" in body.decode(),
          True)

    # ---- validation errors ----------------------------------------------
    t.chk("relative path rejected",
          "err=" in redirect_of(post_open("relative/x.db")), True)
    t.chk("dotdot path rejected",
          "err=" in redirect_of(post_open("/tmp/../etc/x.db")), True)
    t.chk("missing file rejected",
          "err=" in redirect_of(post_open(os.path.join(TMP, "nope.db"))), True)
    t.chk("non-sqlite file rejected",
          "err=" in redirect_of(post_open(PLAIN)), True)
    t.chk("empty path rejected",
          "err=" in redirect_of(post_open("")), True)
    st, body, _ = c.get("/db?err=" + urllib.parse.quote("boom & <x>"))
    t.chk("error message rendered escaped",
          "boom &amp; &lt;x&gt;" in body.decode(), True)

    # ---- create a new database ------------------------------------------
    t.chk("create new db", redirect_of(post_open(NEW_DB, action="create")),
          "302 /db?ok=created")
    t.chk("new file exists", os.path.exists(NEW_DB), True)
    st, body, _ = c.get("/db")
    t.chk("new db active", NEW_DB in body.decode(), True)
    t.chk("empty db has no tables", "No tables or views yet." in
          body.decode(), True)
    t.chk("create over existing rejected",
          "err=" in redirect_of(post_open(NEW_DB, action="create")), True)

    # ---- directory browser ------------------------------------------------
    st, data, ct = c.get("/db/browse?dir=" + urllib.parse.quote(TMP))
    listing = json.loads(data)
    t.chk("browse status/type", f"{st} {ct.startswith('application/json')}",
          "200 True")
    t.chk("browse dir echoed", listing["dir"], TMP)
    t.chk("browse parent", listing["parent"], os.path.dirname(TMP))
    entries = {e["name"]: e for e in listing["entries"]}
    t.chk("browse finds fixture", entries["fixture.db"]["sqlite"], True)
    t.chk("browse flags non-sqlite", entries["notes.txt"]["sqlite"], False)
    t.chk("browse dirs first", listing["entries"][0]["dir"], True)
    st, data, _ = c.get("/db/browse")
    t.chk("browse default dir", json.loads(data)["dir"] == os.environ["HOME"],
          True)
    st, data, _ = c.get("/db/browse?dir=" + urllib.parse.quote("../etc"))
    t.chk("browse relative rejected", f"{st} {json.loads(data)['error']}",
          "400 path must be absolute and contain no `..`")
    st, data, _ = c.get("/db/browse?dir=/tmp/a/../etc")
    t.chk("browse dotdot rejected", st, 400)
    st, data, _ = c.get("/db/browse?dir=" +
                        urllib.parse.quote(os.path.join(TMP, "notes.txt")))
    t.chk("browse file rejected", st, 400)

    # ---- recents ------------------------------------------------------------
    with open(RECENTS) as f:
        recent_lines = [l.strip() for l in f if l.strip()]
    t.chk("recents most recent first", recent_lines[0], NEW_DB)
    t.chk("recents contains fixture", FIXTURE in recent_lines, True)
    t.chk("recents capped at 10", len(recent_lines) <= 10, True)
    st, body, _ = c.get("/db")
    t.chk("recents rendered on page", FIXTURE in body.decode(), True)

    # ---- method guards + close -------------------------------------------
    t.chk("GET /db/open is 404", c.get("/db/open")[0], 404)
    t.chk("GET /db/close is 404", c.get("/db/close")[0], 404)
    resp = c.req_full(
        "POST", "/db/close", b"", FORM
    )
    t.chk("close", redirect_of(resp), "302 /db?ok=closed")
    st, body, _ = c.get("/db")
    html = body.decode()
    t.chk("back on app db", APP_DB in html, True)
    t.chk("pill gone after close",
          'db-pill' in html, False)
    st, _, _ = c.get("/db/file")
    t.chk("file transport closed with user db", st, 404)
    st, _, _, _ = c.req_full(
        "POST", "/db/save", b"SQLite format 3\x00" + b"\x00" * 48,
        {"Content-Type": "application/octet-stream", "X-DB-Meta": "0-0.0"})
    t.chk("save closed with user db", st, 404)

    # app DB untouched by any of the switching above
    conn = sqlite3.connect(APP_DB)
    t.chk("app migrations intact",
          conn.execute("SELECT count(*) FROM Migrations").fetchone()[0], 0)
    conn.close()

    st, body, _ = c.get("/db")
    t.chk("WASI loader in page",
          (st, b'id="sqlite-wasm-status"' in body,
           b'src="/js/sqliteBrowser.js"' in body,
           b'id="sqlite-db-tree"' in body,
           b'id="sqlite-db-grid"' in body,
           b'id="sqlite-sql-input"' in body,
           b'id="sqlite-sql-run"' in body,
           b'id="sqlite-sql-result"' in body,
           b'id="sqlite-tbl-name"' in body,
           b'id="sqlite-tbl-create"' in body,
           b'id="sqlite-export-db"' in body,
           b'id="sqlite-import-db"' in body,
           b'id="sqlite-import-sql"' in body),
          (200, True, True, True, True, True, True, True, True, True, True, True, True))
    t.chk("save to device button", b'id="sqlite-save-db"' in body, True)
    t.chk("reload from device button", b'id="sqlite-reload-db"' in body, True)
    t.chk("workspace shell (tabs/panes/status)",
          b'id="dbapp"' in body
          and b'id="db-tabs"' in body
          and b'data-tab="browse"' in body
          and b'data-tab="connection"' in body
          and b'data-tab="info"' in body
          and b'id="db-empty"' in body
          and b'id="db-status"' in body, True)
    t.chk("catppuccin stylesheet linked",
          b'href="/css/db.css"' in body, True)
    t.chk("alter + dirty widgets",
          b'id="sqlite-tbl-target"' in body
          and b'id="sqlite-tbl-alter"' in body
          and b'id="sqlite-dirty"' in body, True)
    t.chk("console history select", b'id="sqlite-sql-history"' in body, True)
    t.chk("index + view widgets",
          b'id="sqlite-idx-table"' in body
          and b'id="sqlite-idx-create"' in body
          and b'id="sqlite-view-target"' in body
          and b'id="sqlite-view-save"' in body
          and b'id="sqlite-schema-status"' in body, True)
    t.chk("trigger widgets",
          b'id="sqlite-trigger-sql"' in body
          and b'id="sqlite-trigger-save"' in body, True)
    t.chk("transaction widgets",
          b'id="sqlite-tx-begin"' in body
          and b'id="sqlite-tx-commit"' in body
          and b'id="sqlite-tx-rollback"' in body
          and b'id="sqlite-tx-status"' in body, True)
    t.chk("database info widgets",
          b'id="sqlite-db-info"' in body
          and b'id="sqlite-fk-toggle"' in body, True)
    st, body, ct = c.get("/js/sqliteBrowser.js")
    t.chk("WASI loader served", (st, "WebAssembly.instantiate" in body.decode(),
                                  ct.startswith("text/javascript")),
          (200, True, True))
    wasm = os.path.join(os.path.dirname(os.path.dirname(__file__)),
                        "build", "sqlite.wasm")
    if os.path.exists(wasm):
        st, body, ct = c.get("/wasm/sqlite.wasm")
        t.chk("WASI module served", (st, body[:4], ct),
              (200, b"\x00asm", "application/wasm"))

    ok = t.summary()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
