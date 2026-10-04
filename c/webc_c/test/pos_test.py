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


def child_thead(html, table):
    """<th> count of `table`'s child-tab header.

    Tab buttons carry data-tab too and all precede the panel contents, so
    anchoring on data-tab alone lands on the wrong thead. Anchor on the
    tbody id instead (the thead sits directly before it), then take the
    nearest preceding <thead> -- a plain non-greedy regex would backtrack
    across </thead> and swallow earlier headers.
    """
    anchor = re.search(r'</thead>\s*<tbody id="mc-tbody-%s-' % table, html)
    if not anchor:
        return -1
    start = html.rfind("<thead", 0, anchor.start())
    if start == -1:
        return -1
    content = html.find(">", start) + 1   # skip "<thead ..."; it holds "<th" too
    end = html.find("</thead>", start)
    return html[content:end].count("<th")


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

    # ---- Phase 5a: CATALOG (/pos/categories, /pos/products) --------------
    st, body, _ = c.get("/pos/categories")
    html = body.decode()
    t.chk("categories page", st, 200)
    t.chk("categories th = code+name+parent+created+actions", th_count(html), 5)
    t.chk("categories empty state", "No Categories yet." in html, True)

    st, _, _ = c.post_urlencoded(
        "/pos/categories/create?redirect=/pos/categories",
        {"cat_code": "POS-CAT-ROOT", "name": "Root Category", "parent_id": ""})
    t.chk("category create", st, 302)
    cid = db_row("SELECT id FROM categories WHERE cat_code = ?",
                 ("POS-CAT-ROOT",))[0]
    t.chk("parent None -> NULL",
          db_row("SELECT parent_id FROM categories WHERE id = ?", (cid,)),
          (None,))

    st, _, _ = c.post_urlencoded(
        "/pos/categories/create?redirect=/pos/categories",
        {"cat_code": "POS-CAT-KID", "name": "Kid Category", "parent_id": cid})
    t.chk("child category create", st, 302)
    kid = db_row("SELECT id FROM categories WHERE cat_code = ?",
                 ("POS-CAT-KID",))[0]
    t.chk("self-FK parent_id stored",
          db_row("SELECT parent_id FROM categories WHERE id = ?", (kid,)),
          (cid,))

    st, body, _ = c.get("/pos/categories")
    html = body.decode()
    row, edit = row_and_edit(html, kid)
    t.chk("kid category row found", bool(row), True)
    t.chk("categories child tab (products)", 'data-tab="products"' in html, True)
    t.chk("parent cell shows FK label (not raw id)",
          ("Root Category" in row and cid not in row), True)
    t.chk("edit row preselects parent",
          bool(re.search(r'<option value="%s"\s+selected' % re.escape(cid),
                         edit)), True)

    st, _, _ = c.post_urlencoded(
        f"/pos/categories/{kid}/update?redirect=/pos/categories",
        {"cat_code": "POS-CAT-KID", "name": "Kid Renamed", "parent_id": cid})
    t.chk("category update", st, 302)
    t.chk("category update saved",
          db_row("SELECT name FROM categories WHERE id = ?", (kid,)),
          ("Kid Renamed",))

    # ---- /pos/products master -------------------------------------------
    st, body, _ = c.get("/pos/products")
    html = body.decode()
    t.chk("products page", st, 200)
    t.chk("products th = product+codes+prices+category+type+actions",
          th_count(html), 6)
    t.chk("products empty state", "No Products yet." in html, True)

    st, _, _ = c.post_urlencoded(
        "/pos/products/create?redirect=/pos/products",
        {"name": "Test Product", "sku": "POS-SKU-1", "barcode": "",
         "base_price": "12.50", "cost_price": "7", "tax_rate": "10",
         "category_id": kid, "product_type_dict_id": ""})
    t.chk("product create", st, 302)
    pid = db_row("SELECT id FROM products WHERE sku = ?",
                 ("POS-SKU-1",))[0]
    t.chk("product prices + category in db",
          db_row("SELECT base_price, cost_price, tax_rate, category_id "
                 "FROM products WHERE id = ?", (pid,)),
          (12.5, 7.0, 10.0, kid))
    t.chk("barcode empty + type None -> NULL",
          (db_row("SELECT barcode FROM products WHERE id = ?", (pid,)),
           db_row("SELECT product_type_dict_id FROM products WHERE id = ?",
                  (pid,))),
          ((None,), (None,)))

    st, body, _ = c.get("/pos/products")
    html = body.decode()
    row, edit = row_and_edit(html, pid)
    t.chk("product row found", bool(row), True)
    t.chk("products child tabs",
          ('data-tab="product_variants"' in html
           and 'data-tab="inventory_stocks"' in html), True)
    t.chk("product row = 5 columns + actions", row.count("<td"), 6)
    t.chk("category cell shows FK label",
          ("Kid Renamed" in row and kid not in row), True)
    t.chk("prices cell = 3 stacked parts",
          len(re.findall(r'<span class="text-xs', row.split("</td>")[2])), 3)
    ei = edit_inputs(edit)
    t.chk("edit prefills prices",
          [float(ei.get(k) or 0) for k in
           ("base_price", "cost_price", "tax_rate")],
          [12.5, 7.0, 10.0])

    st, _, _ = c.post_urlencoded(
        f"/pos/products/{pid}/update?redirect=/pos/products",
        {"name": "Test Product XL", "sku": "POS-SKU-1", "barcode": "4444",
         "base_price": "15", "cost_price": "8", "tax_rate": "10",
         "category_id": kid, "product_type_dict_id": ""})
    t.chk("product update", st, 302)
    t.chk("product update saved",
          db_row("SELECT name, barcode, base_price FROM products WHERE id = ?",
                 (pid,)),
          ("Test Product XL", "4444", 15.0))
    t.chk("empty type stays NULL (COALESCE keeps)",
          db_row("SELECT product_type_dict_id FROM products WHERE id = ?",
                 (pid,)),
          (None,))

    dict_id = db_row("SELECT id FROM dictionaries LIMIT 1")[0]
    st, _, _ = c.post_urlencoded(
        f"/pos/products/{pid}/update?redirect=/pos/products",
        {"name": "Test Product XL", "sku": "POS-SKU-1", "barcode": "4444",
         "base_price": "15", "cost_price": "8", "tax_rate": "10",
         "category_id": kid, "product_type_dict_id": dict_id})
    t.chk("nullable FK set via edit",
          db_row("SELECT product_type_dict_id FROM products WHERE id = ?",
                 (pid,)),
          (dict_id,))

    # ---- child tabs: variants + stock ------------------------------------
    st, _, _ = c.post_urlencoded(
        f"/pos/variants/create?product_id={pid}&redirect=/pos/products",
        {"variant_name": "Large", "sku": "POS-SKU-1-L",
         "price_delta": "2.5", "attributes": '{"size":"L"}'})
    t.chk("variant create (child, fk via query)", st, 302)
    vid = db_row("SELECT id FROM product_variants WHERE sku = ?",
                 ("POS-SKU-1-L",))[0]
    t.chk("variant saved with product_id + delta",
          db_row("SELECT product_id, price_delta FROM product_variants "
                 "WHERE id = ?", (vid,)),
          (pid, 2.5))

    st, body, _ = c.get("/pos/products")
    html = body.decode()
    t.chk("variant row in child tab", "Large" in html, True)
    vm = re.search(r'data-tab="product_variants".*?<thead[^>]*>(.*?)</thead>',
                   html, re.S)
    t.chk("variants child thead = 4 cols + actions",
          vm.group(1).count("<th") if vm else -1, 5)

    db_exec("INSERT OR IGNORE INTO org_units (id, ou_code, ou_name) "
            "VALUES (?, ?, ?)", ("pos-ou-1", "POS-OU-1", "Main Depot"))
    st, _, _ = c.post_urlencoded(
        f"/pos/stocks/create?product_id={pid}&redirect=/pos/products",
        {"org_unit_id": "pos-ou-1", "variant_id": vid, "quantity": "5"})
    t.chk("stock create (child, thresholds default 0)", st, 302)
    sid = db_row("SELECT id FROM inventory_stocks WHERE product_id = ?",
                 (pid,))[0]
    t.chk("stock row db (quantity + defaulted thresholds)",
          db_row("SELECT quantity, min_threshold, max_threshold "
                 "FROM inventory_stocks WHERE id = ?", (sid,)),
          (5.0, 0.0, 0.0))
    st, body, _ = c.get("/pos/products")
    t.chk("stock row in child tab (FK label)",
          "Main Depot" in body.decode(), True)

    # standalone child soft-delete hides the row; the restore route brings
    # it back (trash UI for children follows the parent ?deleted=1 view).
    st, _, _ = c.post_urlencoded(
        f"/pos/variants/{vid}/delete?redirect=/pos/products", {})
    t.chk("variant soft delete", st, 302)
    t.chk("variant deleted_at stamped",
          db_row("SELECT deleted_at FROM product_variants WHERE id = ?",
                 (vid,))[0] is not None, True)
    st, body, _ = c.get("/pos/products")
    t.chk("variant hidden from live child tab",
          f'data-row-id="{vid}"' not in body.decode(), True)
    st, hdrs, _, _ = c.req_full(
        "POST", f"/pos/variants/{vid}/restore?redirect=/pos/products")
    t.chk("variant restore route", st, 302)
    t.chk("variant restored (deleted_at NULL)",
          db_row("SELECT deleted_at FROM product_variants WHERE id = ?",
                 (vid,)),
          (None,))

    # ---- pagination window with volume -----------------------------------
    for i in range(25):
        db_exec("INSERT INTO products (name, sku, category_id, base_price) "
                "VALUES (?, ?, ?, ?)",
                (f"Bulk {i}", f"BULK-{i}", kid, 1.0))
    st, body, _ = c.get("/pos/products?page=2")
    html = body.decode()
    t.chk("products ?page=2", st, 200)
    t.chk("page 2 window = 6 of 26 rows", html.count('<tr class="hover:'), 6)
    t.chk("products pager present",
          'data-pg-container="mc-tbody"' in html, True)

    st, body, _ = c.get("/pos/categories")
    html = body.decode()
    t.chk("category child pager totals 26", "26 record(s)" in html, True)
    t.chk("category child pager container",
          f'data-pg-container="mc-tbody-products-{kid}"' in html, True)

    # ---- cascade: product delete/restore stamps children -----------------
    st, _, _ = c.post_urlencoded(
        f"/pos/products/{pid}/delete?redirect=/pos/products", {})
    t.chk("product soft delete", st, 302)
    t.chk("cascade: variant + stock stamped",
          (db_row("SELECT deleted_at FROM product_variants WHERE id = ?",
                  (vid,))[0] is not None,
           db_row("SELECT deleted_at FROM inventory_stocks WHERE id = ?",
                  (sid,))[0] is not None),
          (True, True))
    st, body, _ = c.get("/pos/products?deleted=1")
    html = body.decode()
    t.chk("trash view shows deleted product",
          "Test Product XL" in html, True)
    t.chk("trash child rows tinted + restorable",
          ('md-row hover:bg-red-100' in html
           and 'data-md-op="restore"' in html), True)
    st, hdrs, _, _ = c.req_full(
        "POST",
        f"/pos/products/{pid}/restore?redirect=/pos/products?deleted=1")
    t.chk("product restore redirects to trash view",
          (st, hdrs.get("Location")), (302, "/pos/products?deleted=1"))
    t.chk("cascade: variant + stock restored",
          (db_row("SELECT deleted_at FROM product_variants WHERE id = ?",
                  (vid,)),
           db_row("SELECT deleted_at FROM inventory_stocks WHERE id = ?",
                  (sid,))),
          ((None,), (None,)))

    # ---- Phase 5b: INVENTORY (/pos/stocks + stock_ledger) ----------------
    st, body, _ = c.get("/pos/stocks")
    html = body.decode()
    t.chk("stocks page", st, 200)
    t.chk("stocks th = product+org+variant+qty+range+actions", th_count(html), 6)
    t.chk("stocks shows 5a stock row",
          f'data-row-id="{sid}"' in html, True)

    # master create: variant None -> NULL (keeps 5a's variant stock row)
    st, _, _ = c.post_urlencoded(
        "/pos/stocks/create?redirect=/pos/stocks",
        {"product_id": pid, "org_unit_id": "pos-ou-1", "variant_id": "",
         "quantity": "9", "min_threshold": "2", "max_threshold": "50"})
    t.chk("stock create (master form)", st, 302)
    sid2 = db_row("SELECT id FROM inventory_stocks "
                  "WHERE product_id = ? AND variant_id IS NULL", (pid,))[0]
    t.chk("stock row db (master body values)",
          db_row("SELECT quantity, min_threshold, max_threshold "
                 "FROM inventory_stocks WHERE id = ?", (sid2,)),
          (9.0, 2.0, 50.0))

    st, body, _ = c.get("/pos/stocks")
    html = body.decode()
    row, edit = row_and_edit(html, sid2)
    t.chk("stock row found", bool(row), True)
    t.chk("stock row FK labels",
          ("Test Product XL" in row and "Main Depot" in row), True)
    t.chk("edit prefills qty", float(edit_inputs(edit).get("quantity") or 0), 9.0)
    t.chk("stock child tab (ledger)", 'data-tab="stock_ledger"' in html, True)

    st, _, _ = c.post_urlencoded(
        f"/pos/stocks/{sid2}/update?redirect=/pos/stocks",
        {"product_id": pid, "org_unit_id": "pos-ou-1", "variant_id": "",
         "quantity": "7", "min_threshold": "2", "max_threshold": "50"})
    t.chk("stock update", st, 302)
    t.chk("stock update saved",
          db_row("SELECT quantity FROM inventory_stocks WHERE id = ?", (sid2,)),
          (7.0,))

    # ledger via child form (stock_id in the query string)
    st, _, _ = c.post_urlencoded(
        f"/pos/ledger/create?stock_id={sid2}&redirect=/pos/stocks",
        {"reference_type": "ADJUST", "reference_id": "",
         "quantity_change": "5", "balance_after": "5"})
    t.chk("ledger create (child, fk via query)", st, 302)
    lid = db_row("SELECT id FROM stock_ledger "
                 "WHERE stock_id = ? AND reference_type = 'ADJUST'",
                 (sid2,))[0]
    t.chk("ledger row db (change/balance, note default)",
          db_row("SELECT quantity_change, balance_after, note "
                 "FROM stock_ledger WHERE id = ?", (lid,)),
          (5.0, 5.0, None))

    st, body, _ = c.get("/pos/stocks")
    html = body.decode()
    t.chk("ledger row in child tab", "ADJUST" in html, True)
    t.chk("ledger child thead = 4 cols + actions",
          child_thead(html, "stock_ledger"), 5)
    t.chk("ledger pager container",
          f'data-pg-container="mc-tbody-stock_ledger-{sid2}"' in html, True)

    # stock soft delete + restore (master-level trash view)
    st, _, _ = c.post_urlencoded(
        f"/pos/stocks/{sid2}/delete?redirect=/pos/stocks", {})
    t.chk("stock soft delete", st, 302)
    t.chk("stock deleted_at stamped",
          db_row("SELECT deleted_at FROM inventory_stocks WHERE id = ?",
                 (sid2,))[0] is not None, True)
    st, body, _ = c.get("/pos/stocks")
    t.chk("stock hidden from live list",
          f'data-row-id="{sid2}"' not in body.decode(), True)
    st, body, _ = c.get("/pos/stocks?deleted=1")
    t.chk("stock in trash view",
          f'data-row-id="{sid2}"' in body.decode(), True)
    st, hdrs, _, _ = c.req_full(
        "POST", f"/pos/stocks/{sid2}/restore?redirect=/pos/stocks?deleted=1")
    t.chk("stock restore redirects to trash view",
          (st, hdrs.get("Location")), (302, "/pos/stocks?deleted=1"))
    t.chk("stock restored",
          db_row("SELECT deleted_at FROM inventory_stocks WHERE id = ?",
                 (sid2,)),
          (None,))

    # ---- stock pagination window with volume -----------------------------
    for _ in range(25):
        db_exec("INSERT INTO inventory_stocks (org_unit_id, product_id, "
                "quantity) VALUES (?, ?, ?)",
                ("pos-ou-1", pid, 1.0))
    st, body, _ = c.get("/pos/stocks?page=2")
    html = body.decode()
    t.chk("stocks ?page=2", st, 200)
    t.chk("stocks page 2 = 7 of 27 rows",
          html.count('<tr class="hover:'), 7)
    t.chk("stocks pager present",
          'data-pg-container="mc-tbody"' in html, True)

    # ---- Phase 5c: SALES (/pos/orders, /pos/shifts, /pos/finance) --------
    # fixtures: a location + a staff member (orders.staff_id NOT NULL)
    db_exec("INSERT INTO locations (province, district, commune, village) "
            "VALUES (?, ?, ?, ?)",
            ("Phnom Penh", "Chamkarmon", "Boeung Keng Kang", "BKK1"))
    loc_id = db_row("SELECT id FROM locations "
                    "ORDER BY created_at DESC LIMIT 1")[0]
    user_id = db_row("SELECT id FROM users LIMIT 1")[0]
    db_exec("INSERT INTO staff (id, user_id, org_unit_id, staff_code, "
            "first_name, last_name, location_id) VALUES (?, ?, ?, ?, ?, ?, ?)",
            ("pos-stf-1", user_id, "pos-ou-1", "POS-STF-1",
             "Sok", "Dara", loc_id))

    st, body, _ = c.get("/pos/orders")
    html = body.decode()
    t.chk("orders page", st, 200)
    t.chk("orders th = order+org+staff+customer+status+amounts+actions",
          th_count(html), 7)
    t.chk("orders empty state", "No Orders yet." in html, True)
    sm = re.search(r'name="order_status_dict_id"[^>]*>(.*?)</select>',
                   html, re.S)
    t.chk("status options scoped to ORDER_STATUS (fk_where)",
          sm.group(1).count("<option") if sm else -1, 7)

    st, _, _ = c.post_urlencoded(
        "/pos/orders/create?redirect=/pos/orders",
        {"order_number": "ORD-POS-1", "org_unit_id": "pos-ou-1",
         "staff_id": "pos-stf-1", "customer_id": "",
         "order_status_dict_id": "PAID", "subtotal": "100",
         "discount_amount": "10", "tax_amount": "9", "total_amount": "99"})
    t.chk("order create", st, 302)
    oid = db_row("SELECT id FROM orders WHERE order_number = ?",
                 ("ORD-POS-1",))[0]
    t.chk("order row db (incl. delivery_fee/shift defaults)",
          db_row("SELECT org_unit_id, staff_id, customer_id, subtotal, "
                 "total_amount, delivery_fee, shift_id FROM orders WHERE id = ?",
                 (oid,)),
          ("pos-ou-1", "pos-stf-1", None, 100.0, 99.0, 0.0, None))

    st, body, _ = c.get("/pos/orders")
    html = body.decode()
    row, edit = row_and_edit(html, oid)
    t.chk("order row found", bool(row), True)
    t.chk("order row FK labels (org/staff)",
          ("Main Depot" in row and "Sok" in row), True)
    status_label = db_row("SELECT label FROM dictionaries WHERE id = 'PAID'")[0]
    t.chk("status cell shows dict label (not raw id)",
          (status_label in row and "PAID" not in row), True)
    t.chk("amounts cell = 4 stacked parts",
          len(re.findall(r'<span class="text-xs',
                         row.split("</td>")[5])), 4)

    st, _, _ = c.post_urlencoded(
        f"/pos/orders/{oid}/update?redirect=/pos/orders",
        {"order_number": "ORD-POS-1", "org_unit_id": "pos-ou-1",
         "staff_id": "pos-stf-1", "customer_id": "",
         "order_status_dict_id": "DELIVERED", "subtotal": "100",
         "discount_amount": "10", "tax_amount": "9", "total_amount": "105"})
    t.chk("order update", st, 302)
    t.chk("order update saved",
          db_row("SELECT order_status_dict_id, total_amount FROM orders "
                 "WHERE id = ?", (oid,)),
          ("DELIVERED", 105.0))

    # ---- order children: items, payments, deliveries ---------------------
    st, _, _ = c.post_urlencoded(
        f"/pos/order_items/create?order_id={oid}&redirect=/pos/orders",
        {"product_id": pid, "variant_id": "", "unit_price": "15",
         "quantity": "2", "total_line": "30"})
    t.chk("order item create (child, fk via query)", st, 302)
    iid = db_row("SELECT id FROM order_items WHERE order_id = ?", (oid,))[0]
    t.chk("order item db (price/qty/total + computed defaults)",
          db_row("SELECT unit_price, unit_cost, quantity, discount_amount, "
                 "tax_amount, total_line FROM order_items WHERE id = ?", (iid,)),
          (15.0, 0.0, 2.0, 0.0, 0.0, 30.0))

    st, _, _ = c.post_urlencoded(
        f"/pos/payments/create?order_id={oid}&redirect=/pos/orders",
        {"amount": "99", "payment_status": "", "transaction_ref": "TX-1",
         "payment_method_dict_id": "CASH"})
    t.chk("payment create (blank status -> COMPLETED)", st, 302)
    pay_id = db_row("SELECT id FROM payments WHERE order_id = ?", (oid,))[0]
    t.chk("payment db",
          db_row("SELECT payment_status, payment_method_dict_id, amount "
                 "FROM payments WHERE id = ?", (pay_id,)),
          ("COMPLETED", "CASH", 99.0))

    st, _, _ = c.post_urlencoded(
        f"/pos/deliveries/create?order_id={oid}&redirect=/pos/orders",
        {"recipient_name": "Chana", "recipient_phone": "012345678",
         "delivery_address": "St 123", "delivery_status": "delivered",
         "delivery_cost": "3", "driver_staff_id": ""})
    t.chk("delivery create (UNIQUE one per order)", st, 302)
    did = db_row("SELECT id FROM deliveries WHERE order_id = ?", (oid,))[0]
    t.chk("delivery db (driver NULL, status, cost)",
          db_row("SELECT driver_staff_id, delivery_status, delivery_cost, "
                 "dispatched_at FROM deliveries WHERE id = ?", (did,)),
          (None, "delivered", 3.0, None))
    st, _, _ = c.post_urlencoded(
        f"/pos/deliveries/create?order_id={oid}&redirect=/pos/orders",
        {"recipient_name": "Dup", "recipient_phone": "099",
         "delivery_address": "St 1", "delivery_status": "pending",
         "delivery_cost": "0", "driver_staff_id": ""})
    t.chk("second delivery for same order rejected (UNIQUE)", st, 500)

    st, body, _ = c.get("/pos/orders")
    html = body.decode()
    t.chk("order child tabs",
          ('data-tab="order_items"' in html
           and 'data-tab="payments"' in html
           and 'data-tab="deliveries"' in html), True)
    t.chk("child rows rendered",
          (f'data-row-id="{iid}"' in html and f'data-row-id="{pay_id}"' in html
           and f'data-row-id="{did}"' in html), True)
    for tab, want in (("order_items", 4), ("payments", 4), ("deliveries", 4)):
        t.chk(f"{tab} child thead = 3 cols + actions",
              child_thead(html, tab), want)

    # ---- cascade: order delete/restore stamps children -------------------
    st, _, _ = c.post_urlencoded(
        f"/pos/orders/{oid}/delete?redirect=/pos/orders", {})
    t.chk("order soft delete", st, 302)
    t.chk("cascade: items/payments/deliveries stamped",
          (db_row("SELECT deleted_at FROM order_items WHERE id = ?",
                  (iid,))[0] is not None,
           db_row("SELECT deleted_at FROM payments WHERE id = ?",
                  (pay_id,))[0] is not None,
           db_row("SELECT deleted_at FROM deliveries WHERE id = ?",
                  (did,))[0] is not None),
          (True, True, True))
    st, body, _ = c.get("/pos/orders?deleted=1")
    html = body.decode()
    t.chk("trash shows order + child rows",
          (f'data-row-id="{oid}"' in html and f'data-row-id="{iid}"' in html),
          True)
    st, hdrs, _, _ = c.req_full(
        "POST", f"/pos/orders/{oid}/restore?redirect=/pos/orders?deleted=1")
    t.chk("order restore redirects to trash view",
          (st, hdrs.get("Location")), (302, "/pos/orders?deleted=1"))
    t.chk("cascade: children restored",
          (db_row("SELECT deleted_at FROM order_items WHERE id = ?", (iid,)),
           db_row("SELECT deleted_at FROM payments WHERE id = ?", (pay_id,)),
           db_row("SELECT deleted_at FROM deliveries WHERE id = ?", (did,))),
          ((None,), (None,), (None,)))

    # ---- /pos/shifts (master-only) ---------------------------------------
    st, body, _ = c.get("/pos/shifts")
    html = body.decode()
    t.chk("shifts page", st, 200)
    t.chk("shifts th = org+staff+status+cash+notes+actions", th_count(html), 6)
    t.chk("shifts empty state", "No Shifts yet." in html, True)

    st, _, _ = c.post_urlencoded(
        "/pos/shifts/create?redirect=/pos/shifts",
        {"org_unit_id": "pos-ou-1", "staff_id": "pos-stf-1", "status": "",
         "opening_cash": "500", "closing_cash": "", "expected_cash": "",
         "notes": "morning"})
    t.chk("shift create (blank status -> OPEN)", st, 302)
    shid = db_row("SELECT id FROM cash_shifts WHERE notes = 'morning'")[0]
    t.chk("shift db (status default, cash, opened_at default)",
          (db_row("SELECT status, opening_cash FROM cash_shifts WHERE id = ?",
                  (shid,)),
           db_row("SELECT opened_at FROM cash_shifts WHERE id = ?",
                  (shid,))[0] is not None),
          (("OPEN", 500.0), True))

    st, body, _ = c.get("/pos/shifts")
    row, edit = row_and_edit(body.decode(), shid)
    t.chk("shift row found", bool(row), True)
    t.chk("shift row contents (org/staff/status)",
          ("Main Depot" in row and "Sok" in row and "OPEN" in row), True)
    t.chk("shift cash cell = 3 parts",
          len(re.findall(r'<span class="text-xs',
                         row.split("</td>")[3])), 3)

    st, _, _ = c.post_urlencoded(
        f"/pos/shifts/{shid}/update?redirect=/pos/shifts",
        {"org_unit_id": "pos-ou-1", "staff_id": "pos-stf-1", "status": "CLOSED",
         "opening_cash": "500", "closing_cash": "480", "expected_cash": "490",
         "notes": "morning"})
    t.chk("shift close update", st, 302)
    t.chk("shift close saved",
          db_row("SELECT status, closing_cash, expected_cash "
                 "FROM cash_shifts WHERE id = ?", (shid,)),
          ("CLOSED", 480.0, 490.0))

    # ---- /pos/finance (read_only) ----------------------------------------
    st, body, _ = c.get("/pos/finance")
    html = body.decode()
    t.chk("finance page (read_only)", st, 200)
    t.chk("finance th = org+type+account+amounts+description+actions",
          th_count(html), 6)
    t.chk("finance empty state", "No Finance yet." in html, True)
    t.chk("finance read_only: no write UI",
          ('data-md-op="create"' not in html
           and "master-edit-btn" not in html
           and 'data-md-op="delete"' not in html
           and 'data-md-op="restore"' not in html), True)
    t.chk("finance: write route not registered",
          c.req("POST", "/pos/finance/create",
                b"a=b", {"Content-Type": "application/x-www-form-urlencoded"})[0],
          404)

    # ---- Phase 5d: PARTY (/pos/customers, /pos/users+, /pos/staff, /pos/org)
    st, body, _ = c.get("/pos/customers")
    html = body.decode()
    t.chk("customers page", st, 200)
    t.chk("customers th = tier+points+since+actions", th_count(html), 4)
    t.chk("customers empty state", "No Customers yet." in html, True)
    cm = re.search(r'name="customer_type_dict_id"[^>]*>(.*?)</select>',
                   html, re.S)
    t.chk("tier options scoped to CUSTOMER_TYPE (fk_where)",
          cm.group(1).count("<option") if cm else -1, 4)

    st, _, _ = c.post_urlencoded(
        "/pos/customers/create?redirect=/pos/customers",
        {"customer_type_dict_id": "TEIR_2", "loyalty_points": "250"})
    t.chk("customer create", st, 302)
    cid = db_row("SELECT id FROM customers WHERE loyalty_points = 250")[0]
    t.chk("customer row db (tier/points, metadata default)",
          db_row("SELECT customer_type_dict_id, loyalty_points, metadata "
                 "FROM customers WHERE id = ?", (cid,)),
          ("TEIR_2", 250, None))

    st, body, _ = c.get("/pos/customers")
    html = body.decode()
    row, edit = row_and_edit(html, cid)
    tier_label = db_row("SELECT label FROM dictionaries WHERE id = 'TEIR_2'")[0]
    t.chk("customer row shows tier label + points",
          (bool(row) and tier_label in row and "250" in row), True)
    t.chk("customer child tabs (users/activity)",
          ('data-tab="users"' in html
           and 'data-tab="customer_interactions"' in html), True)

    # user child under the customer (fk via query)
    st, _, _ = c.post_urlencoded(
        f"/pos/users/create?customer_id={cid}&redirect=/pos/customers",
        {"name": "Chanthou", "username": "chanthou1",
         "email": "chanthou1@example.com", "profile_pic": "",
         "created_at": ""})
    t.chk("user child create under customer", st, 302)
    uid5d = db_row("SELECT id FROM users WHERE username = 'chanthou1'")[0]
    t.chk("user child db (customer_id set, phone/org null)",
          db_row("SELECT customer_id, phone, org_unit_id FROM users "
                 "WHERE id = ?", (uid5d,)),
          (cid, None, None))
    st, body, _ = c.get("/pos/customers")
    html = body.decode()
    t.chk("user child row under customer",
          f'data-row-id="{uid5d}"' in html, True)

    # interaction child under the customer
    st, _, _ = c.post_urlencoded(
        f"/pos/customer_interactions/create?customer_id={cid}"
        "&redirect=/pos/customers",
        {"user_id": uid5d, "interaction_type": "REVIEW",
         "raw_payload": '{"score":5}'})
    t.chk("interaction create (child)", st, 302)
    itc5d = db_row("SELECT id FROM customer_interactions "
                   "WHERE customer_id = ? AND interaction_type = 'REVIEW'",
                   (cid,))[0]
    t.chk("interaction db (user, payload)",
          db_row("SELECT user_id, raw_payload FROM customer_interactions "
                 "WHERE id = ?", (itc5d,)),
          (uid5d, '{"score":5}'))
    st, body, _ = c.get("/pos/customers")
    html = body.decode()
    t.chk("interaction row under customer",
          f'data-row-id="{itc5d}"' in html, True)
    t.chk("interaction child thead = user+event cells + actions",
          child_thead(html, "customer_interactions"), 3)

    # ---- /pos/users: new Roles + Staff child tabs -------------------------
    st, body, _ = c.get("/pos/users")
    html = body.decode()
    t.chk("users page children (Roles + Staff)",
          ('data-tab="user_roles"' in html and 'data-tab="staff"' in html),
          True)

    db_exec("INSERT INTO roles (id, org_unit_id, role_code, role_name) "
            "VALUES (?, ?, ?, ?)",
            ("pos-role-5d", "pos-ou-1", "POS-ROLE-5D", "Cashier 5D"))
    st, _, _ = c.post_urlencoded(
        f"/pos/user_roles/create?user_id={uid5d}&redirect=/pos/users",
        {"role_id": "pos-role-5d", "created_at": ""})
    t.chk("user_role child create (fk via query)", st, 302)
    url5d = db_row("SELECT id FROM user_roles "
                   "WHERE user_id = ? AND role_id = ?",
                   (uid5d, "pos-role-5d"))[0]
    t.chk("user_role link row (live)",
          db_row("SELECT deleted_at FROM user_roles WHERE id = ?", (url5d,)),
          (None,))
    st, body, _ = c.get("/pos/users")
    html = body.decode()
    t.chk("role link row under user",
          (f'data-row-id="{url5d}"' in html and "Cashier 5D" in html), True)
    t.chk("user_roles child thead = role+linked + actions",
          child_thead(html, "user_roles"), 3)

    # staff child under the user (user_id via query; org/location in body)
    st, _, _ = c.post_urlencoded(
        f"/pos/staff/create?user_id={uid5d}&redirect=/pos/users",
        {"staff_code": "POS-STF-5D", "first_name": "Vanny",
         "last_name": "Ly", "org_unit_id": "pos-ou-1",
         "location_id": loc_id, "created_at": ""})
    t.chk("staff child create under user", st, 302)
    stf5d = db_row("SELECT id FROM staff WHERE staff_code = 'POS-STF-5D'")[0]
    t.chk("staff child db (user/org/location, type/hire/phone null)",
          db_row("SELECT user_id, org_unit_id, location_id, "
                 "staff_type_dict_id, hire_date, phone FROM staff "
                 "WHERE id = ?", (stf5d,)),
          (uid5d, "pos-ou-1", loc_id, None, None, None))
    st, body, _ = c.get("/pos/users")
    t.chk("staff child row under user",
          f'data-row-id="{stf5d}"' in body.decode(), True)

    # ---- /pos/staff master + cash_shifts child ----------------------------
    st, body, _ = c.get("/pos/staff")
    html = body.decode()
    t.chk("staff page", st, 200)
    t.chk("staff th = code+name+user+org+location+actions", th_count(html), 6)
    row, edit = row_and_edit(html, stf5d)
    t.chk("staff row found", bool(row), True)
    t.chk("staff row labels (name cell / user / org)",
          ("Vanny" in row and "Chanthou" in row and "Main Depot" in row),
          True)
    t.chk("staff name cell = 2 parts",
          len(re.findall(r'<span class="text-xs', row.split("</td>")[1])), 2)
    t.chk("staff child tab (shifts)", 'data-tab="cash_shifts"' in html, True)

    # shift child under the staff member (staff_id via query)
    st, _, _ = c.post_urlencoded(
        f"/pos/shifts/create?staff_id={stf5d}&redirect=/pos/staff",
        {"org_unit_id": "pos-ou-1", "status": "", "opening_cash": "300",
         "closing_cash": "", "expected_cash": "", "notes": "eve"})
    t.chk("shift child create under staff", st, 302)
    shid5d = db_row("SELECT id FROM cash_shifts WHERE notes = 'eve'")[0]
    t.chk("shift child db (staff from query, status default OPEN)",
          db_row("SELECT staff_id, status, opening_cash FROM cash_shifts "
                 "WHERE id = ?", (shid5d,)),
          (stf5d, "OPEN", 300.0))

    # ---- /pos/org ---------------------------------------------------------
    st, body, _ = c.get("/pos/org")
    html = body.decode()
    t.chk("org page", st, 200)
    t.chk("org th = code+name+type+parent+actions", th_count(html), 5)
    t.chk("org shows fixture root", 'data-row-id="pos-ou-1"' in html, True)
    om = re.search(r'name="ou_type_dict_id"[^>]*>(.*?)</select>', html, re.S)
    t.chk("org type options scoped to ORG_TYPE (fk_where)",
          om.group(1).count("<option") if om else -1, 4)

    st, _, _ = c.post_urlencoded(
        "/pos/org/create?redirect=/pos/org",
        {"ou_code": "POS-OU-2", "ou_name": "Branch East",
         "ou_type_dict_id": "BRANCH", "parent_id": "pos-ou-1"})
    t.chk("org create", st, 302)
    ou2 = db_row("SELECT id FROM org_units WHERE ou_code = 'POS-OU-2'")[0]
    t.chk("org row db (parent/type, metadata default)",
          db_row("SELECT parent_id, ou_type_dict_id, metadata "
                 "FROM org_units WHERE id = ?", (ou2,)),
          ("pos-ou-1", "BRANCH", None))
    st, body, _ = c.get("/pos/org")
    html = body.decode()
    row, edit = row_and_edit(html, ou2)
    org_type_label = db_row("SELECT label FROM dictionaries "
                            "WHERE id = 'BRANCH'")[0]
    t.chk("org row labels (parent name + type label)",
          (bool(row) and "Main Depot" in row and org_type_label in row), True)
    t.chk("org child tabs (users/staff)",
          ('data-tab="users"' in html and 'data-tab="staff"' in html), True)

    # org soft delete + restore (branch has no children -> cascade no-op)
    st, _, _ = c.post_urlencoded(
        f"/pos/org/{ou2}/delete?redirect=/pos/org", {})
    t.chk("org soft delete", st, 302)
    st, body, _ = c.get("/pos/org?deleted=1")
    t.chk("org in trash view", f'data-row-id="{ou2}"' in body.decode(), True)
    st, hdrs, _, _ = c.req_full(
        "POST", f"/pos/org/{ou2}/restore?redirect=/pos/org?deleted=1")
    t.chk("org restore redirects to trash view",
          (st, hdrs.get("Location")), (302, "/pos/org?deleted=1"))

    # customer soft delete + restore (no customers cascade in schema)
    st, _, _ = c.post_urlencoded(
        f"/pos/customers/{cid}/delete?redirect=/pos/customers", {})
    t.chk("customer soft delete", st, 302)
    st, body, _ = c.get("/pos/customers?deleted=1")
    t.chk("customer in trash view",
          f'data-row-id="{cid}"' in body.decode(), True)
    st, hdrs, _, _ = c.req_full(
        "POST", f"/pos/customers/{cid}/restore"
        f"?redirect=/pos/customers?deleted=1")
    t.chk("customer restore redirects to trash view",
          (st, hdrs.get("Location")), (302, "/pos/customers?deleted=1"))

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
