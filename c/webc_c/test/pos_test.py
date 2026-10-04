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
    lm = re.search(r'data-tab="stock_ledger".*?<thead[^>]*>(.*?)</thead>',
                   html, re.S)
    t.chk("ledger child thead = 4 cols + actions",
          lm.group(1).count("<th") if lm else -1, 5)
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
