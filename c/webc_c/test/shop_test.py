#!/usr/bin/env python3
"""Storefront + cart + guest checkout + auth suite (Phase 11).

Spawned by test/run.sh LAST (after pos_test.py) so the users/orders it
creates cannot disturb earlier suites' list and pagination counts.
Covers:
  - public catalog at / (search, category filter, ?page=), product detail
  - cookie cart: add / update / remove / buy-now (webc_cart)
  - POST /checkout is the only customer order path: server-computed money
    (forged form amounts ignored), stock decrement + stock_ledger rows,
    deliveries contact row, customers/users reuse by phone, cart cleared
  - over-stock and empty-cart rejects leave orders untouched
  - lockdown: /pos, /dashboard, /reports 303 -> /login for guests (incl.
    a forged POST /pos/orders/create), session login/logout flow, and the
    ?next= open-redirect guard.
"""
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

DB = os.path.join(TMP, "nested", "deep", "db")

c = Client(HOST, PORT)
t = Checker()

STOCK_SQL = (
    "COALESCE((SELECT SUM(s.quantity) FROM inventory_stocks s "
    "          WHERE s.product_id = p.id AND s.deleted_at IS NULL), 0)"
)
PHONE = "012999001"
URLC = {"Content-Type": "application/x-www-form-urlencoded"}


def db_row(sql, args=()):
    conn = sqlite3.connect(DB, timeout=10)
    try:
        conn.execute("PRAGMA busy_timeout=5000")
        return conn.execute(sql, args).fetchone()
    finally:
        conn.close()


def db_rows(sql, args=()):
    conn = sqlite3.connect(DB, timeout=10)
    try:
        conn.execute("PRAGMA busy_timeout=5000")
        return conn.execute(sql, args).fetchall()
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


def post(path, fields):
    """POST urlencoded through the shared cookie jar -> (status, headers)."""
    st, hdr, _, _ = c.req_full(
        "POST", path,
        body=urllib.parse.urlencode(fields).encode(), headers=URLC)
    return st, hdr


def pick_products():
    """a/b/c/d: ordinary seeded products; e: dedicated over-stock victim."""
    rows = db_rows(
        "SELECT p.id, p.name, p.base_price FROM products p "
        "WHERE p.deleted_at IS NULL AND p.id LIKE 'sd-prod-%' "
        f"AND {STOCK_SQL} >= 4 ORDER BY p.id LIMIT 4"
    )
    a, b, cprod, dprod = rows
    e = db_row(
        "SELECT p.id, p.name, p.base_price FROM products p "
        "WHERE p.deleted_at IS NULL AND p.id NOT IN (?,?,?,?) "
        "AND EXISTS (SELECT 1 FROM inventory_stocks s "
        "            WHERE s.product_id = p.id AND s.deleted_at IS NULL) "
        "ORDER BY p.id LIMIT 1",
        (a[0], b[0], cprod[0], dprod[0]),
    )
    return a, b, cprod, dprod, e


def cart_cookie():
    return c.cookies.get("webc_cart", "")


def main():
    c.wait_ready()
    a, b, cprod, dprod, e = pick_products()

    # ---- public storefront ------------------------------------------------
    st, body, _ = c.get("/")
    html = body.decode()
    t.chk("storefront home", st, 200)
    t.chk("storefront has no admin sidebar", 'class="nav-link' in html, False)
    t.chk("storefront grid present", "grid grid-cols-2" in html, True)
    t.chk("storefront search box", 'name="q"' in html, True)

    # ?q= search finds a specific seeded product
    st, body, _ = c.get("/?q=" + urllib.parse.quote(a[1]))
    t.chk("search page", st, 200)
    t.chk("search finds the product by name", a[1] in body.decode(), True)
    st, body, _ = c.get("/?q=zzz-no-such-product-zzz")
    t.chk("search empty state", "មិនមានផលិតផលទេ" in body.decode(), True)

    # ?cat= filter: every rendered sku belongs to that category
    cat_id = db_row(
        "SELECT category_id FROM products WHERE id = ?", (a[0],))[0]
    cat_skus = {
        r[0] for r in db_rows(
            "SELECT sku FROM products WHERE category_id = ? "
            "AND deleted_at IS NULL", (cat_id,))
    }
    st, body, _ = c.get(f"/?cat={cat_id}")
    html = body.decode()
    t.chk("category filter page", st, 200)
    shown = set(re.findall(r'text-slate-400 truncate">([^<]+) · ', html))
    t.chk("category filter renders rows", len(shown) >= 1, True)
    t.chk("category filter only own skus", shown <= cat_skus, True)

    # ?page= pager: page 1 and 2 windows are disjoint
    st, p1, _ = c.get("/")
    st2, p2, _ = c.get("/?page=2")
    t.chk("page=2 status", st2, 200)
    win1 = set(re.findall(r'href="/product/([^"]+)"', p1.decode()))
    win2 = set(re.findall(r'href="/product/([^"]+)"', p2.decode()))
    t.chk("page 1 has product links", len(win1) >= 1, True)
    t.chk("page1/page2 disjoint", bool(win1) and not (win1 & win2), True)

    # ---- product detail ---------------------------------------------------
    st, body, _ = c.get(f"/product/{a[0]}")
    html = body.decode()
    t.chk("product detail", st, 200)
    t.chk("detail shows name", a[1] in html, True)
    t.chk("detail shows price", f"{a[2]:.2f} $" in html, True)
    t.chk("detail add-to-cart form", 'action="/cart/add"' in html, True)
    t.chk("detail buy-now form", 'action="/cart/buynow"' in html, True)
    t.chk("unknown product id -> 404", c.get("/product/no-such-id")[0], 404)
    t.chk("empty product id -> 404", c.get("/product/")[0], 404)

    # ---- cookie cart ------------------------------------------------------
    st, _ = post("/cart/add", {"product_id": a[0], "qty": "2",
                               "redirect": "/cart"})
    t.chk("cart add a x2 -> redirect", st, 303)
    t.chk("cart cookie set", f"{a[0]}:2" in cart_cookie(), True)

    post("/cart/add", {"product_id": b[0], "qty": "1"})
    t.chk("cart cookie has both lines",
          f"{a[0]}:2,{b[0]}:1" in cart_cookie(), True)

    st, body, _ = c.get("/cart")
    html = body.decode()
    t.chk("cart page", st, 200)
    t.chk("cart rows", html.count("data-cart-row"), 2)
    sub = 2 * a[2] + 1 * b[2]
    t.chk("cart subtotal (DB prices)", f"{sub:.2f} $" in html, True)

    post("/cart/update", {"product_id": a[0], "qty": "5",
                          "redirect": "/cart"})
    t.chk("cart update qty", f"{a[0]}:5" in cart_cookie(), True)
    st, body, _ = c.get("/cart")
    t.chk("updated subtotal",
          f"{5 * a[2] + b[2]:.2f} $" in body.decode(), True)

    post("/cart/remove", {"product_id": b[0]})
    t.chk("cart remove line", cart_cookie(), f"{a[0]}:5")

    post("/cart/update", {"product_id": a[0], "qty": "0"})
    t.chk("qty 0 clears cart cookie", "webc_cart" in c.cookies, False)
    st, body, _ = c.get("/cart")
    t.chk("empty cart state", "រទេះទំនិញទំនេរ" in body.decode(), True)

    # ---- buy now: cart becomes exactly that one line ----------------------
    post("/cart/buynow", {"product_id": cprod[0], "qty": "1"})
    t.chk("buy-now replaces cart", cart_cookie(), f"{cprod[0]}:1")
    st, body, _ = c.get("/checkout")
    html = body.decode()
    t.chk("checkout page (guest allowed)", st, 200)
    t.chk("checkout summary shows product", cprod[1] in html, True)
    t.chk("checkout contact form", 'name="address"' in html, True)

    # ---- happy path checkout ---------------------------------------------
    orders_before = db_row("SELECT COUNT(*) FROM orders")[0]
    stock_c_before = db_row(
        f"SELECT {STOCK_SQL} FROM products p WHERE p.id = ?", (cprod[0],))[0]

    st, hdr = post("/checkout", {
        "name": "សុខ មាន", "phone": PHONE, "address": "ផ្លូវ 51 ភ្នំពេញ",
        "note": "test", "subtotal": "0.01", "total_amount": "999",
        "unit_price": "0", "discount_amount": "500",
    })
    loc = hdr.get("Location", "")
    t.chk("checkout posts to an order", st, 303)
    t.chk("checkout lands on /order/<id>", loc.startswith("/order/"), True)
    t.chk("cart cookie cleared after order", "webc_cart" in c.cookies, False)

    oid = loc.rsplit("/", 1)[-1]
    row = db_row(
        "SELECT order_number, order_status_dict_id, subtotal, total_amount, "
        "org_unit_id, staff_id, customer_id FROM orders WHERE id = ?", (oid,))
    t.chk("order created", row is not None, True)
    t.chk("order count +1", db_row("SELECT COUNT(*) FROM orders")[0],
          orders_before + 1)
    t.chk("order number WEB-", row[0].startswith("WEB-"), True)
    t.chk("order status PENDING", row[1], "PENDING")
    t.chk("order money = DB total (forged 0.01/999 ignored)",
          (row[2], row[3]), (cprod[2], cprod[2]))
    t.chk("order org/staff filled", bool(row[4] and row[5]), True)
    t.chk("order customer linked", bool(row[6]), True)
    customer1 = row[6]

    item = db_row(
        "SELECT unit_price, quantity, total_line FROM order_items "
        "WHERE order_id = ?", (oid,))
    t.chk("order line = DB price", (item[0], item[1], item[2]),
          (cprod[2], 1.0, cprod[2]))

    stock_c_after = db_row(
        f"SELECT {STOCK_SQL} FROM products p WHERE p.id = ?", (cprod[0],))[0]
    t.chk("stock decremented", stock_c_after, stock_c_before - 1)
    led = db_row(
        "SELECT quantity_change, balance_after, reference_type "
        "FROM stock_ledger WHERE reference_id = ?", (oid,))
    t.chk("stock ledger row for the order",
          (led[0], led[1], led[2]), (-1.0, stock_c_after, "ORDER"))

    deliv = db_row(
        "SELECT recipient_name, recipient_phone, delivery_address, "
        "delivery_status FROM deliveries WHERE order_id = ?", (oid,))
    t.chk("delivery row carries guest contact",
          (deliv[1], deliv[2]), (PHONE, "ផ្លូវ 51 ភ្នំពេញ"))
    t.chk("delivery status pending", deliv[3], "pending")

    user = db_row(
        "SELECT username, user_type_dict_id, customer_id FROM users "
        "WHERE phone = ?", (PHONE,))
    t.chk("guest user created from phone",
          (user[0], user[1], user[2]), (PHONE, "CUSTOMER", customer1))

    st, body, _ = c.get(f"/order/{oid}")
    html = body.decode()
    t.chk("order confirmation page", st, 200)
    t.chk("confirmation shows order number", row[0] in html, True)
    t.chk("confirmation shows phone", PHONE in html, True)
    t.chk("unknown order id -> 404", c.get("/order/no-such-id")[0], 404)

    # ---- customer reuse: same phone -> same customer, no second user ------
    post("/cart/add", {"product_id": dprod[0], "qty": "1"})
    orders_before2 = db_row("SELECT COUNT(*) FROM orders")[0]
    st, hdr = post("/checkout", {
        "name": "សុខ មាន", "phone": PHONE, "address": "ផ្លូវ 51 ភ្នំពេញ"})
    t.chk("second checkout", st, 303)
    oid2 = hdr.get("Location", "").rsplit("/", 1)[-1]
    row2 = db_row("SELECT customer_id FROM orders WHERE id = ?", (oid2,))
    t.chk("same phone reuses the customer", row2[0], customer1)
    t.chk("users rows for that phone still 1",
          db_row("SELECT COUNT(*) FROM users WHERE phone = ?", (PHONE,))[0], 1)
    t.chk("second order count +1",
          db_row("SELECT COUNT(*) FROM orders")[0], orders_before2 + 1)

    # ---- over-stock reject: rollback, no order, stock intact --------------
    # Force the victim to exactly 1 unit across its stock rows (single row
    # in the seeds, but be robust), then ask for 3.
    db_exec("UPDATE inventory_stocks SET quantity = 0 "
            "WHERE product_id = ? AND deleted_at IS NULL", (e[0],))
    first_row = db_row(
        "SELECT id FROM inventory_stocks "
        "WHERE product_id = ? AND deleted_at IS NULL ORDER BY id LIMIT 1",
        (e[0],))
    db_exec("UPDATE inventory_stocks SET quantity = 1 WHERE id = ?",
            (first_row[0],))
    stock_e = db_row(
        f"SELECT {STOCK_SQL} FROM products p WHERE p.id = ?", (e[0],))[0]
    post("/cart/add", {"product_id": e[0], "qty": "3"})
    orders_before3 = db_row("SELECT COUNT(*) FROM orders")[0]
    st, _, body, _ = c.req_full(
        "POST", "/checkout",
        body=urllib.parse.urlencode({
            "name": "Over Stock", "phone": "012999002",
            "address": "nowhere"}).encode(),
        headers=URLC)
    t.chk("over-stock checkout rejected", st, 200)
    t.chk("over-stock error marker", b"data-checkout-error" in body, True)
    t.chk("over-stock makes no order",
          db_row("SELECT COUNT(*) FROM orders")[0], orders_before3)
    t.chk("over-stock keeps stock", db_row(
        f"SELECT {STOCK_SQL} FROM products p WHERE p.id = ?", (e[0],))[0],
        stock_e)
    # rollback does not touch the cookie - clear the line manually
    post("/cart/remove", {"product_id": e[0]})

    # ---- empty cart: POST /checkout -> /cart, no order --------------------
    orders_before4 = db_row("SELECT COUNT(*) FROM orders")[0]
    st, hdr = post("/checkout", {"name": "x", "phone": "012999003",
                                 "address": "y"})
    t.chk("empty cart checkout -> /cart", (st, hdr.get("Location")),
          (303, "/cart"))
    t.chk("empty cart makes no order",
          db_row("SELECT COUNT(*) FROM orders")[0], orders_before4)
    st, hdr, _, _ = c.req_full("GET", "/checkout")
    t.chk("GET /checkout without cart -> /cart",
          (st, hdr.get("Location")), (303, "/cart"))

    # ---- lockdown: staff pages require login ------------------------------
    st, hdr, _, _ = c.req_full("GET", "/pos/orders")
    t.chk("guest /pos/orders -> login", st, 303)
    t.chk("guest /pos/orders next=",
          "next=/pos/orders" in hdr.get("Location", ""), True)
    t.chk("guest /dashboard -> login", c.req_full("GET", "/dashboard")[0], 303)
    t.chk("guest /reports -> login", c.req_full("GET", "/reports")[0], 303)
    t.chk("guest /pos exact -> login", c.req_full("GET", "/pos")[0], 303)

    # forged staff order creation from a guest must bounce and change nothing
    orders_before5 = db_row("SELECT COUNT(*) FROM orders")[0]
    st, hdr = post("/pos/orders/create", {
        "order_number": "HACK-1", "org_unit_id": "sd-ou-phm",
        "staff_id": "sd-stf-1", "subtotal": "1", "total_amount": "1"})
    t.chk("guest POST order create -> login", st, 303)
    t.chk("guest POST order create changes nothing",
          db_row("SELECT COUNT(*) FROM orders")[0], orders_before5)

    # login: bad credentials stay on the form
    st, body, _ = c.post_urlencoded(
        "/login", {"username": "sd.staff1", "password": "nope"})
    t.chk("login bad password", st, 200)
    t.chk("login bad password marker", b"data-login-error" in body, True)

    # login: good credentials -> session cookie, default landing /dashboard
    st, hdr = c.login_as("sd.staff1", "posadmin1")
    t.chk("login ok", st, 303)
    t.chk("login sets webc_sid", "webc_sid" in hdr.get("Set-Cookie", ""), True)
    t.chk("login lands on /dashboard", hdr.get("Location"), "/dashboard")
    t.chk("staff /pos/orders", c.get("/pos/orders")[0], 200)
    t.chk("staff /dashboard", c.get("/dashboard")[0], 200)
    t.chk("staff /reports", c.get("/reports")[0], 200)

    # ?next= open-redirect guard: //evil.com falls back to /dashboard
    st, hdr, _, _ = c.req_full(
        "POST", "/login?next=" + urllib.parse.quote("//evil.com", safe=""),
        body=urllib.parse.urlencode(
            {"username": "sd.staff1", "password": "posadmin1"}).encode(),
        headers=URLC)
    t.chk("evil next redirected safely", hdr.get("Location"), "/dashboard")

    # logout kills the session server-side, not just the cookie
    sid = c.cookies.get("webc_sid")
    t.chk("session cookie present before logout", bool(sid), True)
    st, hdr, _, _ = c.req_full("GET", "/logout")
    t.chk("logout redirects", st, 303)
    sc = hdr.get("Set-Cookie", "")
    t.chk("logout clears cookie",
          "webc_sid=" in sc and "Max-Age=0" in sc, True)
    t.chk("logout lands on storefront", hdr.get("Location"), "/")
    # replay the old sid: the session row is gone
    c.cookies["webc_sid"] = sid
    t.chk("old session rejected after logout", c.get("/pos/orders")[0], 303)
    c.cookies.pop("webc_sid", None)

    # storefront stays public for guests
    t.chk("storefront public after logout", c.get("/")[0], 200)

    ok = t.summary()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
