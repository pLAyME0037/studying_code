#include "shop.h"

#include <stdlib.h>
#include <string.h>

#include "core/http/utils.h"
#include "src/db/db.h"

// Cookie cart: "id:qty,id:qty,..." - HttpOnly, no server state. Every load
// re-validates against the DB (unknown/deleted products become invalid
// lines and are dropped on the next save).

static bool id_valid(String_View s) {
    if (s.count == 0 || s.count > 39) return false;
    for (size_t i = 0; i < s.count; ++i) {
        char c = s.data[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

static int qty_parse(String_View s, int fallback) {
    if (s.count == 0 || s.count > 6) return fallback;
    int v = 0;
    for (size_t i = 0; i < s.count; ++i) {
        if (s.data[i] < '0' || s.data[i] > '9') return fallback;
        v = v * 10 + (s.data[i] - '0');
        if (v > 999999) return 999999;
    }
    return v;
}

void shop_cart_load(Serve_Context *sc, Shop_Cart *cart) {
    memset(cart, 0, sizeof(*cart));
    String_View raw = {0};
    if (!http_cookie_find(sc, CART_COOKIE, &raw) || raw.count == 0) return;

    String_View rest = raw;
    while (rest.count > 0 && cart->count < CART_MAX) {
        String_View pair = sv_chop_by_delim(&rest, ',');
        String_View idsv = sv_chop_by_delim(&pair, ':');
        if (!id_valid(idsv)) continue;
        int qty = qty_parse(pair, 0);
        if (qty < 1) continue;
        // Merge duplicates instead of trusting the sender.
        bool merged = false;
        for (size_t i = 0; i < cart->count; ++i) {
            Shop_Cart_Item *it = &cart->items[i];
            if (strlen(it->id) == idsv.count
                && memcmp(it->id, idsv.data, idsv.count) == 0) {
                if (it->qty <= 999 - qty) it->qty += qty; else it->qty = 999;
                merged = true;
                break;
            }
        }
        if (merged) continue;
        Shop_Cart_Item *it = &cart->items[cart->count++];
        memset(it, 0, sizeof(*it));
        memcpy(it->id, idsv.data, idsv.count);
        it->qty = qty > 999 ? 999 : qty;
    }

    // Enrich with live DB data (name/price/stock) - one open, N statements.
    db_t *db = open_webc_db();
    if (!db) return;
    static const char sql[] =
        "SELECT p.name, p.sku, p.base_price, p.cost_price, "
        "COALESCE((SELECT SUM(s.quantity) FROM inventory_stocks s "
        "          WHERE s.product_id = p.id AND s.deleted_at IS NULL), 0) "
        "FROM products p WHERE p.id = ? AND p.deleted_at IS NULL;";
    for (size_t i = 0; i < cart->count; ++i) {
        Shop_Cart_Item *it = &cart->items[i];
        sql_stmt stmt = {0};
        if (sql_prepare(db, sql, &stmt)
            && sql_bind(&stmt, 1, SQL_SV(sv_from_parts(it->id, strlen(it->id))))
            && sql_step(&stmt) == SQL_ROW) {
            snprintf(it->name, sizeof(it->name), "%s",
                     sql_col_text(&stmt, 0) ? sql_col_text(&stmt, 0) : "");
            snprintf(it->sku, sizeof(it->sku), "%s",
                     sql_col_text(&stmt, 1) ? sql_col_text(&stmt, 1) : "");
            const char *p = sql_col_text(&stmt, 2);
            const char *c = sql_col_text(&stmt, 3);
            const char *s = sql_col_text(&stmt, 4);
            it->price = p && p[0] ? atof(p) : 0.0;
            it->cost = c && c[0] ? atof(c) : 0.0;
            it->stock = s && s[0] ? atof(s) : 0.0;
            it->valid = true;
            cart->subtotal += it->price * it->qty;
        }
        sql_finalize(&stmt);
    }
    db_close(db);
}

void shop_cart_save(Serve_Context *sc, const Shop_Cart *cart) {
    char buf[3600];
    size_t n = 0;
    bool any = false;
    for (size_t i = 0; i < cart->count && n + 48 < sizeof(buf); ++i) {
        const Shop_Cart_Item *it = &cart->items[i];
        if (!it->valid) continue;
        if (any) buf[n++] = ',';
        n += (size_t) snprintf(buf + n, sizeof(buf) - n, "%s:%d",
                               it->id, it->qty);
        any = true;
    }
    if (!any) {
        http_set_cookie(sc, CART_COOKIE "=; Path=/; HttpOnly; Max-Age=0");
        return;
    }
    http_set_cookie(sc, temp_sprintf(
        CART_COOKIE "=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=2592000",
        buf));
}

// A ?redirect= carried in the POST body (hidden input), same open-redirect
// guard as http_redirect_target.
static const char *body_redirect(Serve_Context *sc, const char *fallback) {
    String_View req = sb_to_sv(sc->request);
    String_View body = sb_to_sv(sc->body);
    String_View t = form_text(req, body, "redirect");
    if (t.count > 1 && t.data[0] == '/'
        && t.data[1] != '/' && t.data[1] != '\\') {
        return temp_sprintf("%.*s", (int) t.count, t.data);
    }
    return fallback;
}

static bool form_pid(Serve_Context *sc, String_View *pid) {
    *pid = form_text(sb_to_sv(sc->request), sb_to_sv(sc->body), "product_id");
    return id_valid(*pid);
}

// ---------------------------------------------------------------------------
// GET /cart
// ---------------------------------------------------------------------------

void serve_shop_cart(Serve_Context *sc) {
    Shop_Cart cart = {0};
    shop_cart_load(sc, &cart);

    String_Builder content = {0};
    shop_chrome_start(&content, sc, "/cart");
    sb_append_cstr(&content,
        "<div class=\"bg-white border border-slate-200\">"
        "<div class=\"bg-slate-900 text-white px-3 py-2 flex items-center"
        " justify-between\"><span class=\"text-sm font-semibold\">"
        "រទេះទំនិញ</span>"
        "<a href=\"/\" class=\"text-xs text-slate-300 hover:text-white\">"
        "បន្តទិញ</a></div>");

    if (cart.count == 0) {
        sb_append_cstr(&content,
            "<div class=\"text-center px-3 py-8\">"
            "<div class=\"text-slate-500 text-sm\">រទេះទំនិញទំនេរ</div>"
            "<a href=\"/\" class=\"text-xs text-indigo-600 hover:underline\">"
            "មើលផលិតផល</a></div>");
    } else {
        for (size_t i = 0; i < cart.count; ++i) {
            Shop_Cart_Item *it = &cart.items[i];
            if (!it->valid) continue;
            sb_append_cstr(&content,
                "<div data-cart-row class=\"px-3 py-2 border-b"
                " border-slate-100 flex items-center gap-3\">"
                "<div class=\"flex-1 min-w-0\">"
                "<div class=\"text-sm font-medium text-slate-800 truncate\">");
            sb_append_html_escaped(&content, it->name);
            sb_append_cstr(&content,
                "</div><div class=\"text-xs text-slate-400\">");
            sb_append_html_escaped(&content, it->sku);
            sb_append_cstr(&content, " · ");
            sb_append_cstr(&content, shop_money(it->price));
            if ((double) it->qty > it->stock) {
                sb_append_cstr(&content,
                    " · <span class=\"text-red-500\">ស្តុកមិនគ្រប់គ្រាន់</span>");
            }
            sb_append_cstr(&content,
                "</div></div>"
                "<form method=\"POST\" action=\"/cart/update\""
                " class=\"flex items-center gap-1\">"
                "<input type=\"hidden\" name=\"product_id\" value=\"");
            sb_append_html_escaped(&content, it->id);
            sb_append_cstr(&content,
                "\"><input type=\"hidden\" name=\"redirect\" value=\"/cart\">"
                "<input type=\"number\" name=\"qty\" value=\"");
            sb_appendf(&content, "%d", it->qty);
            sb_append_cstr(&content,
                "\" min=\"0\" class=\"border border-slate-300 px-1 py-1.5"
                " text-sm w-16\">"
                "<button class=\"border border-slate-300 px-2 py-1.5 text-xs"
                " bg-white hover:bg-slate-50\">កែ</button>"
                "</form>"
                "<div class=\"text-sm w-24 text-right font-medium\">");
            sb_append_cstr(&content, shop_money(it->price * it->qty));
            sb_append_cstr(&content,
                "</div>"
                "<form method=\"POST\" action=\"/cart/remove\">"
                "<input type=\"hidden\" name=\"product_id\" value=\"");
            sb_append_html_escaped(&content, it->id);
            sb_append_cstr(&content,
                "\"><button class=\"text-red-500 text-xs hover:underline\">"
                "លុប</button></form></div>");
        }
        sb_append_cstr(&content,
            "<div class=\"px-3 py-2 flex items-center justify-between"
            " bg-slate-100 border-t border-slate-200\">"
            "<span class=\"text-sm font-semibold text-slate-700\">សរុប</span>"
            "<span class=\"text-sm font-semibold text-slate-900\">");
        sb_append_cstr(&content, shop_money(cart.subtotal));
        sb_append_cstr(&content,
            "</span></div>"
            "<div class=\"px-3 py-2 flex items-center justify-end gap-2"
            " border-t border-slate-200\">"
            "<a href=\"/\" class=\"border border-slate-300 px-2 py-1.5"
            " text-sm bg-white hover:bg-slate-50\">បន្តទិញ</a>"
            "<a href=\"/checkout\" class=\"bg-indigo-600 hover:bg-indigo-700"
            " text-white px-2 py-1.5 text-sm\">បង់ប្រាក់</a>"
            "</div>");
    }
    sb_append_cstr(&content, "</div>");

    shop_chrome_end(&content);
    shop_render(sc, 200, "រទេះទំនិញ", &content);
    sb_free(content);
}

// ---------------------------------------------------------------------------
// POST /cart/add, /cart/buynow, /cart/update, /cart/remove
// ---------------------------------------------------------------------------

// Reads product_id + qty from the body and loads the product row; false when
// the id is malformed or the product is gone (caller answers 404).
static bool load_posted_product(Serve_Context *sc, String_View *pid,
                                int *qty, char *name, size_t name_sz,
                                char *sku, size_t sku_sz,
                                double *price, double *cost, double *stock) {
    String_View qv = form_text(sb_to_sv(sc->request), sb_to_sv(sc->body),
                               "qty");
    *qty = qty_parse(qv, 1);

    db_t *db = open_webc_db();
    if (!db) return false;
    static const char sql[] =
        "SELECT p.name, p.sku, p.base_price, p.cost_price, "
        "COALESCE((SELECT SUM(s.quantity) FROM inventory_stocks s "
        "          WHERE s.product_id = p.id AND s.deleted_at IS NULL), 0) "
        "FROM products p WHERE p.id = ? AND p.deleted_at IS NULL;";
    bool found = false;
    sql_stmt stmt = {0};
    if (sql_prepare(db, sql, &stmt)
        && sql_bind(&stmt, 1, SQL_SV(*pid))
        && sql_step(&stmt) == SQL_ROW) {
        snprintf(name, name_sz, "%s",
                 sql_col_text(&stmt, 0) ? sql_col_text(&stmt, 0) : "");
        snprintf(sku, sku_sz, "%s",
                 sql_col_text(&stmt, 1) ? sql_col_text(&stmt, 1) : "");
        const char *p = sql_col_text(&stmt, 2);
        const char *c = sql_col_text(&stmt, 3);
        const char *s = sql_col_text(&stmt, 4);
        *price = p && p[0] ? atof(p) : 0.0;
        *cost = c && c[0] ? atof(c) : 0.0;
        *stock = s && s[0] ? atof(s) : 0.0;
        found = true;
    }
    sql_finalize(&stmt);
    db_close(db);
    return found;
}

static void cart_add_common(Serve_Context *sc, bool replace_whole_cart,
                            const char *after) {
    String_View pid = {0};
    if (!form_pid(sc, &pid)) { serve_error(sc, 400); return; }

    char name[160] = {0}, sku[64] = {0};
    double price = 0, cost = 0, stock = 0;
    int qty = 1;
    if (!load_posted_product(sc, &pid, &qty, name, sizeof(name),
                             sku, sizeof(sku), &price, &cost, &stock)) {
        serve_error(sc, 404);
        return;
    }
    if (qty < 1) qty = 1;
    if (qty > 999) qty = 999;

    Shop_Cart cart = {0};
    if (!replace_whole_cart) shop_cart_load(sc, &cart);

    Shop_Cart_Item *slot = NULL;
    for (size_t i = 0; i < cart.count; ++i) {
        Shop_Cart_Item *it = &cart.items[i];
        if (strlen(it->id) == pid.count
            && memcmp(it->id, pid.data, pid.count) == 0) {
            slot = it;
            break;
        }
    }
    if (slot) {
        slot->qty = slot->qty <= 999 - qty ? slot->qty + qty : 999;
        slot->valid = true;
    } else if (cart.count < CART_MAX) {
        slot = &cart.items[cart.count++];
        memset(slot, 0, sizeof(*slot));
        memcpy(slot->id, pid.data, pid.count);
        slot->qty = qty;
        slot->valid = true;
    } else {
        serve_error(sc, 400);  // cart cookie is full
        return;
    }

    shop_cart_save(sc, &cart);
    http_render_redirect(sc, 303, body_redirect(sc, after));
}

void serve_shop_cart_add(Serve_Context *sc) {
    cart_add_common(sc, false, "/cart");
}

void serve_shop_cart_buynow(Serve_Context *sc) {
    // Buy now = the cart becomes exactly this one line, straight to checkout.
    cart_add_common(sc, true, "/checkout");
}

void serve_shop_cart_update(Serve_Context *sc) {
    String_View pid = {0};
    if (!form_pid(sc, &pid)) { serve_error(sc, 400); return; }
    String_View qv = form_text(sb_to_sv(sc->request), sb_to_sv(sc->body),
                               "qty");
    int qty = qty_parse(qv, 1);
    if (qty > 999) qty = 999;

    Shop_Cart cart = {0};
    shop_cart_load(sc, &cart);
    for (size_t i = 0; i < cart.count; ++i) {
        Shop_Cart_Item *it = &cart.items[i];
        if (strlen(it->id) == pid.count
            && memcmp(it->id, pid.data, pid.count) == 0) {
            if (qty < 1) {  // qty 0 removes the line
                for (size_t j = i + 1; j < cart.count; ++j)
                    cart.items[j - 1] = cart.items[j];
                --cart.count;
            } else {
                it->qty = qty;
            }
            break;
        }
    }
    shop_cart_save(sc, &cart);
    http_render_redirect(sc, 303, body_redirect(sc, "/cart"));
}

void serve_shop_cart_remove(Serve_Context *sc) {
    String_View pid = {0};
    if (!form_pid(sc, &pid)) { serve_error(sc, 400); return; }

    Shop_Cart cart = {0};
    shop_cart_load(sc, &cart);
    for (size_t i = 0; i < cart.count; ++i) {
        Shop_Cart_Item *it = &cart.items[i];
        if (strlen(it->id) == pid.count
            && memcmp(it->id, pid.data, pid.count) == 0) {
            for (size_t j = i + 1; j < cart.count; ++j)
                cart.items[j - 1] = cart.items[j];
            --cart.count;
            break;
        }
    }
    shop_cart_save(sc, &cart);
    http_render_redirect(sc, 303, body_redirect(sc, "/cart"));
}
