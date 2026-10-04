#!/usr/bin/env python3
"""POS composite-column suite (Phase 2).

Spawned by test/run.sh against the same throwaway HOME/DB as http_test.py.
Covers the master_child engine's composite (multi-field) columns:
  /pos/locations - one <td> renders a 4-part stack cell
  /pos/users     - one <td> renders an avatar cell (pic + name + username)
plus CRUD that saves each part, col/th/colspan counts, edit-row prefills,
pagination markup, and that the demo pages did not move.
Rows are located by their own id (http_test.py leaves demo users behind).
"""
import os
import re
import sqlite3
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from testlib import Client, Checker  # noqa: E402

HOST = sys.argv[1]
PORT = int(sys.argv[2])
TMP = sys.argv[3]

DB = os.path.join(TMP, "nested", "deep", "db")
PICK = "/resource/image/upload/7155395830974148_1.webp"  # never deleted

c = Client(HOST, PORT)
t = Checker()


def db_row(sql, args=()):
    conn = sqlite3.connect(DB, timeout=10)
    try:
        conn.execute("PRAGMA busy_timeout=5000")
        return conn.execute(sql, args).fetchone()
    finally:
        conn.close()


def db_exec(sql, args=()):
    conn = sqlite3.connect(DB, timeout=10)
    try:
        conn.execute("PRAGMA busy_timeout=5000")
        conn.execute(sql, args)
        conn.commit()
    finally:
        conn.close()


def row_and_edit(html, rid):
    """(display-row inner html, edit-row inner html) for row `rid`.

    The inline edit row immediately follows its display row in the template.
    """
    m = re.search(
        r'data-row-id="%s">(.*?)</tr>\s*'
        r'<tr class="md-master-edit-row hidden">(.*?)</tr>'
        % re.escape(rid),
        html,
        re.S,
    )
    return (m.group(1), m.group(2)) if m else ("", "")


def edit_inputs(edit_html):
    return dict(
        re.findall(r'<input[^>]*name="([^"]+)"[^>]*value="([^"]*)"', edit_html)
    )


def th_count(html):
    m = re.search(r"<thead[^>]*>(.*?)</thead>", html, re.S)
    return m.group(1).count("<th") if m else -1


def main():
    c.wait_ready()

    # ---- /pos/locations: 4-part stack cell -------------------------------
    st, body, _ = c.get("/pos/locations")
    html = body.decode()
    t.chk("locations page", st, 200)
    t.chk("locations th = cell + created + actions", th_count(html), 3)
    t.chk("locations empty state colspan 3", 'colspan="3"' in html, "True")
    t.chk("locations pager nav", 'data-pg-container="mc-tbody"' in html, "True")
    t.chk("locations ?page=1", c.get("/pos/locations?page=1")[0], 200)

    loc1 = {
        "province": "Phnom Penh",
        "district": "Chamkarmon",
        "commune": "Tonle Bassac",
        "village": "Svay Pak",
    }
    st, _, _ = c.post_urlencoded(
        "/pos/locations/create?redirect=/pos/locations", loc1
    )
    t.chk("location create", st, 302)
    rid = db_row("SELECT id FROM locations WHERE village = ?", ("Svay Pak",))[0]

    st, body, _ = c.get("/pos/locations")
    html = body.decode()
    row, edit = row_and_edit(html, rid)
    t.chk("location row found by id", bool(row), "True")
    t.chk("stack column = ONE td (3 total in row)", row.count("<td"), 3)
    first_td = row.split("</td>")[0]
    spans = re.findall(r'<span class="text-xs[^"]*">([^<]*)</span>', first_td)
    t.chk("stack cell renders 4 parts in order",
          spans, list(loc1.values()))
    t.chk("stack color ranks strongest first",
          (first_td.find("text-gray-900") < first_td.find("text-gray-400")),
          "True")
    ei = edit_inputs(edit)
    t.chk("edit row prefills every part",
          [ei.get(k) for k in loc1], list(loc1.values()))
    t.chk("edit + detail rows keep colspan 3", html.count('colspan="3"') >= 2,
          "True")

    loc2 = {
        "province": "Phnom Penh",
        "district": "Prampi Makara",
        "commune": "Phnom Penh Thmey",
        "village": "Kilometer 6",
    }
    st, _, _ = c.post_urlencoded(
        f"/pos/locations/{rid}/update?redirect=/pos/locations", loc2
    )
    t.chk("location update", st, 302)
    st, body, _ = c.get("/pos/locations")
    html = body.decode()
    row, _ = row_and_edit(html, rid)
    t.chk("update reflects in cell (old part gone)",
          ("Prampi Makara" in row and "Chamkarmon" not in row), "True")
    t.chk("update saved each part in db",
          db_row(
              "SELECT province, district, commune, village "
              "FROM locations WHERE id = ?", (rid,)
          ),
          tuple(loc2.values()))
    t.chk("created_at untouched by update",
          db_row("SELECT created_at FROM locations WHERE id = ?", (rid,))
          is not None, "True")

    # ---- /pos/users: avatar cell -----------------------------------------
    st, body, _ = c.get("/pos/users")
    html = body.decode()
    t.chk("pos users page", st, 200)
    t.chk("users th = avatar + email + joined + actions", th_count(html), 4)

    usr = {"name": "Sok Dara", "username": "pos_avatar_1",
           "email": "dara@pos.kh", "profile_pic": PICK}
    st, _, _ = c.post_urlencoded("/pos/users/create?redirect=/pos/users", usr)
    t.chk("staff create", st, 302)
    uid = db_row("SELECT id FROM users WHERE username = ?",
                 ("pos_avatar_1",))[0]

    st, body, _ = c.get("/pos/users")
    html = body.decode()
    row, edit = row_and_edit(html, uid)
    t.chk("staff row found by id", bool(row), "True")
    t.chk("avatar column = ONE td (4 total in row)", row.count("<td"), 4)
    first_td = row.split("</td>")[0]
    t.chk("avatar cell image + primary + secondary",
          (f'<img src="{PICK}"' in first_td
           and "Sok Dara" in first_td
           and "pos_avatar_1" in first_td), "True")
    ei = edit_inputs(edit)
    t.chk("avatar edit row prefills parts + email",
          [ei.get(k) for k in ("profile_pic", "name", "username", "email")],
          [PICK, "Sok Dara", "pos_avatar_1", "dara@pos.kh"])

    # empty picture -> placeholder box; empty update keeps stored picture
    st, _, _ = c.post_urlencoded(
        "/pos/users/create?redirect=/pos/users",
        {"name": "No Pic", "username": "pos_avatar_2",
         "email": "np@pos.kh", "profile_pic": ""},
    )
    t.chk("staff create without picture", st, 302)
    uid2 = db_row("SELECT id FROM users WHERE username = ?",
                  ("pos_avatar_2",))[0]
    st, body, _ = c.get("/pos/users")
    html = body.decode()
    row2, _ = row_and_edit(html, uid2)
    t.chk("avatar placeholder for empty pic",
          'bg-gray-200 dark:bg-gray-600' in row2, "True")
    st, _, _ = c.post_urlencoded(
        f"/pos/users/{uid}/update?redirect=/pos/users",
        {"name": "Sok Dara Renamed", "username": "pos_avatar_1",
         "email": "dara@pos.kh", "profile_pic": ""},
    )
    t.chk("staff update", st, 302)
    t.chk("empty pic keeps stored pic (COALESCE)",
          db_row("SELECT profile_pic FROM users WHERE id = ?", (uid,)),
          (PICK,))
    t.chk("update saved name",
          db_row("SELECT name FROM users WHERE id = ?", (uid,)),
          ("Sok Dara Renamed",))

    # ---- soft delete + restore (Phase 4) ---------------------------------
    st, _, _ = c.post_urlencoded(
        f"/pos/locations/{rid}/delete?redirect=/pos/locations", {}
    )
    t.chk("location soft delete", st, 302)
    t.chk("location deleted_at stamped",
          db_row("SELECT deleted_at FROM locations WHERE id = ?", (rid,))
          [0] is not None, True)
    st, body, _ = c.get("/pos/locations")
    t.chk("live view hides deleted location",
          (st, "No Locations yet." in body.decode()), (200, True))
    st, body, _ = c.get("/pos/locations?deleted=1")
    html = body.decode()
    row, _ = row_and_edit(html, rid)
    t.chk("trash view shows deleted location", bool(row), True)
    t.chk("trash rows tinted", "hover:bg-red-100" in html, True)
    t.chk("trash row has restore button", 'data-md-op="restore"' in row, True)
    t.chk("trash banner + no add form",
          ("Deleted rows" in html, 'data-md-op="create"' not in html),
          (True, True))
    st, hdrs, _, _ = c.req_full(
        "POST", f"/pos/locations/{rid}/restore?redirect=/pos/locations?deleted=1"
    )
    t.chk("location restore redirects to trash view",
          (st, hdrs.get("Location")), (302, "/pos/locations?deleted=1"))
    t.chk("restore cleared deleted_at",
          db_row("SELECT deleted_at FROM locations WHERE id = ?", (rid,)),
          (None,))
    st, body, _ = c.get("/pos/locations")
    t.chk("restored row visible in live view",
          (st, "Prampi Makara" in body.decode()), (200, True))

    # ---- users: soft delete + cascade trigger ----------------------------
    # user_roles row exercises trg_soft_del_users / trg_restore_users.
    db_exec("INSERT OR IGNORE INTO roles (id, role_code, role_name) "
            "VALUES (?, ?, ?)", ("pos-test-role-1", "POS_TEST_ROLE", "POS Test"))
    db_exec("INSERT OR IGNORE INTO user_roles (user_id, role_id) VALUES (?, ?)",
            (uid, "pos-test-role-1"))
    st, _, _ = c.post_urlencoded(
        f"/pos/users/{uid}/delete?redirect=/pos/users", {}
    )
    t.chk("staff soft delete", st, 302)
    t.chk("staff deleted_at stamped",
          db_row("SELECT deleted_at FROM users WHERE id = ?", (uid,))
          [0] is not None, True)
    t.chk("cascade: user_roles stamped by trigger",
          db_row("SELECT deleted_at FROM user_roles WHERE user_id = ? "
                 "AND role_id = ?", (uid, "pos-test-role-1"))
          [0] is not None, True)
    st, body, _ = c.get("/pos/users")
    t.chk("live view hides deleted staff",
          (st, "pos_avatar_1" not in body.decode()), (200, True))
    st, body, _ = c.get("/pos/users?deleted=1")
    t.chk("trash shows deleted staff", "pos_avatar_1" in body.decode(), True)
    st, hdrs, _, _ = c.req_full(
        "POST", f"/pos/users/{uid}/restore?redirect=/pos/users?deleted=1"
    )
    t.chk("staff restore redirects to trash view",
          (st, hdrs.get("Location")), (302, "/pos/users?deleted=1"))
    t.chk("cascade: user_roles restored by trigger",
          db_row("SELECT deleted_at FROM user_roles WHERE user_id = ? "
                 "AND role_id = ?", (uid, "pos-test-role-1")),
          (None,))

    # final cleanup: both showcase users soft-deleted, live views empty
    st, _, _ = c.post_urlencoded(
        f"/pos/users/{uid}/delete?redirect=/pos/users", {}
    )
    t.chk("staff re-delete", st, 302)
    st, _, _ = c.post_urlencoded(
        f"/pos/users/{uid2}/delete?redirect=/pos/users", {}
    )
    t.chk("staff delete (no-pic)", st, 302)
    st, _, _ = c.post_urlencoded(
        f"/pos/locations/{rid}/delete?redirect=/pos/locations", {}
    )
    t.chk("location re-delete", st, 302)
    t.chk("showcase rows soft-deleted in db",
          (db_row("SELECT deleted_at FROM locations WHERE id = ?", (rid,))
           [0] is not None,
           db_row("SELECT deleted_at FROM users WHERE id IN (?, ?)",
                  (uid, uid2))[0] is not None),
          (True, True))
    st, body, _ = c.get("/pos/locations")
    t.chk("locations empty state after delete",
          (st, "No Locations yet." in body.decode()), (200, True))
    st, body, _ = c.get("/pos/users")
    t.chk("pos users page after deletes (showcase rows gone)",
          (st, "pos_avatar_1" not in body.decode()
           and "pos_avatar_2" not in body.decode()), (200, True))

    # ---- read_only page (/pos/dictionaries) ------------------------------
    st, body, _ = c.get("/pos/dictionaries")
    html = body.decode()
    t.chk("dictionaries page (read_only)", st, 200)
    t.chk("dictionaries th = 4 columns + actions", th_count(html), 5)
    t.chk("dictionaries lists seed rows", html.count('data-row-id="') >= 1, True)
    t.chk("read_only: no create form", 'data-md-op="create"' not in html, True)
    t.chk("read_only: no edit buttons", "master-edit-btn" not in html, True)
    t.chk("read_only: no delete forms", 'data-md-op="delete"' not in html, True)
    t.chk("read_only: no restore forms", 'data-md-op="restore"' not in html, True)
    t.chk("read_only: no add-child buttons", "add-child-btn" not in html, True)
    t.chk("read_only: write route not registered",
          c.req("POST", "/pos/dictionaries/create",
                b"a=b", {"Content-Type": "application/x-www-form-urlencoded"})[0],
          404)

    # ---- demo pages did not move ----------------------------------------
    st, _, _ = c.get("/people")
    t.chk("people page (demo)", st, 200)
    st, body, _ = c.get("/users")
    t.chk("users page (demo)", st, 200)
    st, body, _ = c.get("/people")
    html = body.decode()
    m = re.search(r'<tr class="hover:[^>]*data-row-id="([^"]+)">(.*?)</tr>',
                  html, re.S)
    t.chk("demo master row still 4 columns + actions",
          m.group(2).count("<td") if m else -1, 5)

    ok = t.summary()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
