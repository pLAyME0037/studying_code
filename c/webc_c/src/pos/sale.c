#include "sale.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "third_party/nob/nob.h"
#include "src/auth/auth.h"
#include "src/db/db.h"
#include "src/http/utils.h"
#include "src/pos/pos_util.h"
#include "src/layout/header.h"
#include "src/layout/footer.h"

// =========================================================================
// /pos/sale: dedicated in-store POS sale. The cashier picks products from a
// single dropdown (product_variants joined to products, plus products with
// no variants) and a quantity per line; the server builds the whole order -
// staff/org from the session, customer NULL, status PAID, auto id + order
// number, delivery fee 0 - and computes every money figure from
// products.base_price (+ variant price_delta) x quantity. A CASH payment is
// written for the total and stock is decremented, all in one transaction.
// The online / e-shop checkout path is untouched.
// =========================================================================

#define SALE_MAX_LINES 64
#define SALE_MAX_STOCK_ROWS 16

typedef struct { char id[40]; double qty; } Stock_Row;

static void sv_copy(char *dst, size_t sz, String_View sv) {
    size_t n = sv.count < sz - 1 ? sv.count : sz - 1;
    if (n) memcpy(dst, sv.data, n);
    dst[n] = '\0';
}

// Options for the product dropdown: one row per variant, plus one row for
// every product that has no variants (v.* NULL). The value is "product_id"
// or "product_id:variant_id"; data-price carries the effective unit price so
// the page can total live. Dialect-neutral (no string concat in SQL).
static void sale_load_options(String_Builder *opts) {
    db_t *db = open_webc_db();
    if (!db) return;
    static const char q[] =
        "SELECT p.id, v.id, p.name, COALESCE(v.variant_name, ''), "
               "COALESCE(v.sku, p.sku, ''), "
               "p.base_price + COALESCE(v.price_delta, 0) AS price "
        "FROM products p "
        "LEFT JOIN product_variants v ON v.product_id = p.id "
            "AND v.deleted_at IS NULL "
        "WHERE p.deleted_at IS NULL "
        "ORDER BY p.name;";
    sql_stmt st = {0};
    if (sql_prepare(db, q, &st)) {
        while (sql_step(&st) == SQL_ROW) {
            const char *pid   = sql_col_text(&st, 0);
            const char *vid   = sql_col_text(&st, 1);
            const char *name  = sql_col_text(&st, 2);
            const char *vname = sql_col_text(&st, 3);
            const char *sku   = sql_col_text(&st, 4);
            const char *price = sql_col_text(&st, 5);
            if (!pid || !pid[0]) continue;
            const char *value = (vid && vid[0])
                ? temp_sprintf("%s:%s", pid, vid) : pid;
            double pr = price ? atof(price) : 0.0;
            String_Builder label = {0};
            sb_append_html_escaped(&label, name ? name : "");
            if (vname && vname[0]) {
                sb_append_cstr(&label, " \xE2\x80\xA2 ");
                sb_append_html_escaped(&label, vname);
            }
            if (sku && sku[0]) {
                sb_append_cstr(&label, " \xE2\x80\xA2 ");
                sb_append_html_escaped(&label, sku);
            }
            sb_append_null(&label);
            sb_appendf(opts,
                "<option value=\"%s\" data-price=\"%.2f\">%s &mdash; %.2f"
                "</option>", value, pr, label.items, pr);
            sb_free(label);
        }
    }
    sql_finalize(&st);
    db_close(db);
}

static void render_sale(Serve_Context *sc, bool can_sell, const char *err) {
    String_Builder opts = {0};
    sale_load_options(&opts);
    sb_append_null(&opts);

    String_Builder *sb = &sc->body;
    sb->count = 0;
    render_page_header(sb, "New sale", "/pos/sale");

    sb_append_cstr(sb,
        "<div class=\"max-w-2xl mx-auto\">"
        "<div class=\"bg-mantle border border-surface0\">"
        "<div class=\"bg-text text-onbase px-4 py-2 flex items-center"
        " justify-between\">"
        "<div class=\"text-sm font-semibold\">New sale</div>"
        "<a href=\"/pos/orders\" class=\"text-xs text-onbase/80"
        " hover:underline\">Orders</a></div>");
    if (err && err[0]) {
        sb_append_cstr(sb,
            "<div class=\"bg-red/10 border-l-2 border-red text-red px-3"
            " py-2 text-xs m-3\">");
        sb_append_html_escaped(sb, err);
        sb_append_cstr(sb, "</div>");
    }
    if (!can_sell) {
        sb_append_cstr(sb,
            "<div class=\"px-4 py-6 text-sm text-subtext0\">This account"
            " isn't linked to a staff record with a branch, so it can't"
            " record a sale. Ask a manager to link it under "
            "<a href=\"/pos/staff\" class=\"text-blue hover:underline\">"
            "Staff</a>.</div></div></div>");
    } else {
        sb_append_cstr(sb,
            "<form method=\"POST\" action=\"/pos/sale\" class=\"p-4"
            " space-y-3\">"
            "<select id=\"sale-opt-tpl\" class=\"hidden\">");
        sb_append_cstr(sb, opts.items ? opts.items : "");
        sb_append_cstr(sb,
            "</select>"
            "<div id=\"sale-lines\" class=\"space-y-2\"></div>"
            "<div class=\"flex items-center justify-between pt-2"
            " border-t border-surface0\">"
            "<button type=\"button\" id=\"sale-add\" class=\"text-sm"
            " text-blue border border-surface0 px-3 py-1.5\">+ Add line"
            "</button>"
            "<div class=\"text-sm\">Total&nbsp;<span id=\"sale-total\""
            " class=\"font-semibold\">0.00</span></div></div>"
            "<button type=\"submit\" class=\"w-full bg-blue text-onbase"
            " text-sm px-3 py-2\">Complete sale (paid)</button>"
            "</form>"
            "<script>"
            "(function(){"
            "var tpl=document.getElementById('sale-opt-tpl').innerHTML;"
            "var lines=document.getElementById('sale-lines');"
            "var total=document.getElementById('sale-total');"
            "var n=0;"
            "function recalc(){var t=0;"
            "lines.querySelectorAll('[data-row]').forEach(function(r){"
            "var s=r.querySelector('select');"
            "var q=parseFloat(r.querySelector('input').value)||0;"
            "var o=s.options[s.selectedIndex];"
            "var pr=o?parseFloat(o.getAttribute('data-price'))||0:0;"
            "t+=pr*q;});"
            "total.textContent=t.toFixed(2);}"
            "function add(){var i=n++;"
            "var d=document.createElement('div');"
            "d.setAttribute('data-row','');"
            "d.className='flex items-center gap-2';"
            "d.innerHTML='<select name=\"line'+i+'_item\" class=\"flex-1"
            " bg-base border border-surface0 text-sm px-2 py-1.5\">'+tpl"
            "+'</select>'"
            "+'<input type=\"number\" name=\"line'+i+'_qty\" value=\"1\""
            " min=\"1\" step=\"1\" class=\"w-20 bg-base border"
            " border-surface0 text-sm px-2 py-1.5\">'"
            "+'<button type=\"button\" data-rm class=\"text-red"
            " px-2\">&#10005;</button>';"
            "lines.appendChild(d);recalc();}"
            "document.getElementById('sale-add').addEventListener"
            "('click',add);"
            "lines.addEventListener('change',recalc);"
            "lines.addEventListener('input',recalc);"
            "lines.addEventListener('click',function(e){"
            "if(e.target.hasAttribute&&e.target.hasAttribute('data-rm')){"
            "var r=e.target.closest('[data-row]');if(r){r.remove();"
            "recalc();}}});"
            "add();"
            "})();"
            "</script>"
            "</div></div>");
    }

    render_page_footer(sb);
    http_render_response(sc, 200, "text/html", sb_to_sv(*sb));
    sb_free(opts);
}

void serve_pos_sale(Serve_Context *sc) {
    const Auth_User *au = auth_current_user();
    bool can = au && au->staff_id && au->staff_id[0]
               && au->org_unit_id && au->org_unit_id[0];
    render_sale(sc, can, NULL);
}

void serve_pos_sale_post(Serve_Context *sc) {
    const Auth_User *au = auth_current_user();
    bool can = au && au->staff_id && au->staff_id[0]
               && au->org_unit_id && au->org_unit_id[0];
    if (!can) {
        render_sale(sc, false,
                    "This account can't record a sale.");
        return;
    }

    String_View req  = sb_to_sv(sc->request);
    String_View body = sb_to_sv(sc->body);

    // Posted lines: line{i}_item ("pid" or "pid:vid") + line{i}_qty. A line
    // removed in the UI leaves a gap, so scan every index and keep the
    // non-empty ones instead of stopping at the first hole.
    typedef struct { char pid[40], vid[40]; double qty; } Posted;
    Posted posted[SALE_MAX_LINES];
    size_t n = 0;
    for (int i = 0; i < SALE_MAX_LINES; ++i) {
        char ki[24], kq[24];
        snprintf(ki, sizeof ki, "line%d_item", i);
        snprintf(kq, sizeof kq, "line%d_qty", i);
        String_View item = form_text(req, body, ki);
        if (item.count == 0) continue;
        char pid[40] = {0}, vid[40] = {0};
        const char *colon = (const char *) memchr(item.data, ':', item.count);
        if (colon) {
            size_t plen = (size_t) (colon - item.data);
            memcpy(pid, item.data, plen < 39 ? plen : 39);
            sv_copy(vid, sizeof vid, (String_View) {
                .data = colon + 1, .count = item.count - plen - 1 });
        } else {
            sv_copy(pid, sizeof pid, item);
        }
        if (!pid[0]) continue;
        double qty = 1.0;
        String_View qv = form_text(req, body, kq);
        if (qv.count) { char b[32]; sv_copy(b, sizeof b, qv); qty = atof(b); }
        if (qty < 1) qty = 1;
        if (n < SALE_MAX_LINES) {
            memcpy(posted[n].pid, pid, sizeof pid);
            memcpy(posted[n].vid, vid, sizeof vid);
            posted[n].qty = qty;
            ++n;
        }
    }
    if (n == 0) {
        render_sale(sc, true, "Add at least one product.");
        return;
    }

    db_t *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }
    if (!sql_txn_begin(db)) { db_close(db); serve_error(sc, 500); return; }

    const char *fail = NULL;

    // Resolve each line's unit price + cost from the DB (never trust the
    // form). unit_price = base_price + variant price_delta; unit_cost =
    // products.cost_price.
    typedef struct {
        char pid[40], vid[40];
        double qty, unit_price, unit_cost, line;
    } Res;
    Res res[SALE_MAX_LINES];
    double subtotal = 0;
    static const char price_sql[] =
        "SELECT p.base_price, p.cost_price, COALESCE(v.price_delta, 0) "
        "FROM products p "
        "LEFT JOIN product_variants v ON v.id = ? AND v.product_id = p.id "
        "WHERE p.id = ? AND p.deleted_at IS NULL;";
    static const char price_pg[] =
        "SELECT p.base_price, p.cost_price, COALESCE(v.price_delta, 0) "
        "FROM products p "
        "LEFT JOIN product_variants v ON v.id = $1 AND v.product_id = p.id "
        "WHERE p.id = $2 AND p.deleted_at IS NULL;";
    static const char *const price_q[SQL_LANG_COUNT] = {
        [SQL_SQLITE] = price_sql, [SQL_MYSQL] = price_sql,
        [SQL_POSTGRES] = price_pg,
    };
    for (size_t i = 0; i < n && !fail; ++i) {
        sql_stmt st = {0};
        double base = 0, cost = 0, delta = 0;
        bool got = false;
        if (sql_prepare(db, price_q[db->lang], &st)
            && sql_bind(&st, 1, pos_sv(sv_from_cstr(posted[i].vid)))
            && sql_bind(&st, 2, SQL_SV(sv_from_cstr(posted[i].pid)))
            && sql_step(&st) == SQL_ROW) {
            const char *b = sql_col_text(&st, 0);
            const char *c = sql_col_text(&st, 1);
            const char *d = sql_col_text(&st, 2);
            base  = b ? atof(b) : 0;
            cost  = c ? atof(c) : 0;
            delta = d ? atof(d) : 0;
            got = true;
        }
        sql_finalize(&st);
        if (!got) { fail = "Unknown product in the sale."; break; }
        memcpy(res[i].pid, posted[i].pid, sizeof res[i].pid);
        memcpy(res[i].vid, posted[i].vid, sizeof res[i].vid);
        res[i].qty        = posted[i].qty;
        res[i].unit_price = base + delta;
        res[i].unit_cost  = cost;
        res[i].line       = res[i].unit_price * posted[i].qty;
        subtotal += res[i].line;
    }

    // Order row: session staff + org, customer NULL, PAID, delivery 0.
    char order_id[40] = {0}, order_number[40] = {0};
    if (!fail && !webc_uuid(order_id)) fail = "Could not allocate an order id.";
    if (!fail) {
        char tail[40];
        webc_uuid(tail);
        { char *s = tail, *d = tail; while (*s) { if (*s != '-') *d++ = *s; ++s; } *d = 0; }
        time_t now = time(NULL);
        struct tm tmv;
        localtime_r(&now, &tmv);
        snprintf(order_number, sizeof order_number, "POS-%02d%02d%02d-%.8s",
                 (tmv.tm_year + 1900) % 100, tmv.tm_mon + 1, tmv.tm_mday, tail);
    }
    if (!fail) {
        const char *sub = temp_sprintf("%.2f", subtotal);
        static const char oq_q[] =
            "INSERT INTO orders (id, org_unit_id, staff_id, customer_id, "
            "order_number, order_status_dict_id, subtotal, discount_amount, "
            "tax_amount, delivery_fee, total_amount) "
            "VALUES (?, ?, ?, NULL, ?, 'PAID', ?, 0, 0, 0, ?);";
        static const char oq_pg[] =
            "INSERT INTO orders (id, org_unit_id, staff_id, customer_id, "
            "order_number, order_status_dict_id, subtotal, discount_amount, "
            "tax_amount, delivery_fee, total_amount) "
            "VALUES ($1, $2, $3, NULL, $4, 'PAID', $5, 0, 0, 0, $6);";
        static const char *const oq[SQL_LANG_COUNT] = {
            [SQL_SQLITE] = oq_q, [SQL_MYSQL] = oq_q, [SQL_POSTGRES] = oq_pg,
        };
        sql_stmt st = {0};
        if (sql_prepare(db, oq[db->lang], &st)
            && sql_bind(&st, 1, SQL_SV(sv_from_cstr(order_id)))
            && sql_bind(&st, 2, SQL_SV(sv_from_cstr(au->org_unit_id)))
            && sql_bind(&st, 3, SQL_SV(sv_from_cstr(au->staff_id)))
            && sql_bind(&st, 4, SQL_SV(sv_from_cstr(order_number)))
            && sql_bind(&st, 5, SQL_SV(sv_from_cstr(sub)))
            && sql_bind(&st, 6, SQL_SV(sv_from_cstr(sub)))
            && sql_final_step(&st)) {
            // stored
        } else {
            fail = "Could not save the order.";
        }
        sql_finalize(&st);
    }

    // Lines + stock movement + payment.
    if (!fail) {
        static const char item_sql[] =
            "INSERT INTO order_items (order_id, product_id, variant_id, "
            "unit_price, unit_cost, quantity, discount_amount, tax_amount, "
            "total_line) VALUES (?, ?, ?, ?, ?, ?, 0, 0, ?);";
        static const char item_pg[] =
            "INSERT INTO order_items (order_id, product_id, variant_id, "
            "unit_price, unit_cost, quantity, discount_amount, tax_amount, "
            "total_line) VALUES ($1, $2, $3, $4, $5, $6, 0, 0, $7);";
        static const char *const iq[SQL_LANG_COUNT] = {
            [SQL_SQLITE] = item_sql, [SQL_MYSQL] = item_sql,
            [SQL_POSTGRES] = item_pg,
        };
        // Stock is pooled per product (variant rows included), preferring
        // this org's rows - mirrors the e-shop checkout.
        static const char stock_sql[] =
            "SELECT id, quantity FROM inventory_stocks "
            "WHERE product_id = ? AND deleted_at IS NULL "
            "ORDER BY CASE WHEN org_unit_id = ? THEN 0 ELSE 1 END, id;";
        static const char stock_pg[] =
            "SELECT id, quantity FROM inventory_stocks "
            "WHERE product_id = $1 AND deleted_at IS NULL "
            "ORDER BY CASE WHEN org_unit_id = $2 THEN 0 ELSE 1 END, id;";
        static const char *const stock_q[SQL_LANG_COUNT] = {
            [SQL_SQLITE] = stock_sql, [SQL_MYSQL] = stock_sql,
            [SQL_POSTGRES] = stock_pg,
        };
        static const char dec_sql[] =
            "UPDATE inventory_stocks SET quantity = quantity - ? WHERE id = ?;";
        static const char dec_pg[] =
            "UPDATE inventory_stocks SET quantity = quantity - $1 WHERE id = $2;";
        static const char *const dec_q[SQL_LANG_COUNT] = {
            [SQL_SQLITE] = dec_sql, [SQL_MYSQL] = dec_sql,
            [SQL_POSTGRES] = dec_pg,
        };
        static const char led_sql[] =
            "INSERT INTO stock_ledger (stock_id, reference_type, "
            "reference_id, quantity_change, balance_after, note) "
            "VALUES (?, 'ORDER', ?, ?, ?, ?);";
        static const char led_pg[] =
            "INSERT INTO stock_ledger (stock_id, reference_type, "
            "reference_id, quantity_change, balance_after, note) "
            "VALUES ($1, 'ORDER', $2, $3, $4, $5);";
        static const char *const led_q[SQL_LANG_COUNT] = {
            [SQL_SQLITE] = led_sql, [SQL_MYSQL] = led_sql,
            [SQL_POSTGRES] = led_pg,
        };
        for (size_t i = 0; i < n && !fail; ++i) {
            const char *unit = temp_sprintf("%.2f", res[i].unit_price);
            const char *cost = temp_sprintf("%.2f", res[i].unit_cost);
            const char *qty  = temp_sprintf("%.2f", res[i].qty);
            const char *line = temp_sprintf("%.2f", res[i].line);
            sql_stmt st = {0};
            if (sql_prepare(db, iq[db->lang], &st)
                && sql_bind(&st, 1, SQL_SV(sv_from_cstr(order_id)))
                && sql_bind(&st, 2, SQL_SV(sv_from_cstr(res[i].pid)))
                && sql_bind(&st, 3, pos_sv(sv_from_cstr(res[i].vid)))
                && sql_bind(&st, 4, SQL_SV(sv_from_cstr(unit)))
                && sql_bind(&st, 5, SQL_SV(sv_from_cstr(cost)))
                && sql_bind(&st, 6, SQL_SV(sv_from_cstr(qty)))
                && sql_bind(&st, 7, SQL_SV(sv_from_cstr(line)))
                && sql_final_step(&st)) {
                // stored
            } else {
                fail = "Could not save a sale line.";
            }
            sql_finalize(&st);
            if (fail) break;

            // Stock movement (skip products with no tracked stock rows).
            Stock_Row rows[SALE_MAX_STOCK_ROWS];
            size_t nr = 0;
            double avail = 0;
            st = (sql_stmt) {0};
            if (sql_prepare(db, stock_q[db->lang], &st)
                && sql_bind(&st, 1, SQL_SV(sv_from_cstr(res[i].pid)))
                && sql_bind(&st, 2, SQL_SV(sv_from_cstr(au->org_unit_id)))) {
                while (sql_step(&st) == SQL_ROW && nr < SALE_MAX_STOCK_ROWS) {
                    const char *sid = sql_col_text(&st, 0);
                    const char *sq  = sql_col_text(&st, 1);
                    if (!sid) continue;
                    snprintf(rows[nr].id, sizeof rows[nr].id, "%s", sid);
                    rows[nr].qty = sq ? atof(sq) : 0;
                    avail += rows[nr].qty;
                    ++nr;
                }
            }
            sql_finalize(&st);
            if (nr > 0 && avail + 1e-9 < res[i].qty) {
                fail = "Not enough stock for a sale line.";
                break;
            }
            double need = res[i].qty;
            for (size_t r = 0; r < nr && need > 1e-9; ++r) {
                double take = rows[r].qty < need ? rows[r].qty : need;
                if (take <= 0) continue;
                double bal = rows[r].qty - take;
                const char *take_s = temp_sprintf("%.4f", take);
                const char *chg_s  = temp_sprintf("-%.4f", take);
                const char *bal_s  = temp_sprintf("%.4f", bal);
                const char *note   = temp_sprintf("POS %s", order_number);
                st = (sql_stmt) {0};
                if (sql_prepare(db, dec_q[db->lang], &st)
                    && sql_bind(&st, 1, SQL_SV(sv_from_cstr(take_s)))
                    && sql_bind(&st, 2, SQL_SV(sv_from_cstr(rows[r].id)))
                    && sql_final_step(&st)) {
                    // decremented
                } else {
                    fail = "Could not adjust stock.";
                }
                sql_finalize(&st);
                if (fail) break;
                st = (sql_stmt) {0};
                if (sql_prepare(db, led_q[db->lang], &st)
                    && sql_bind(&st, 1, SQL_SV(sv_from_cstr(rows[r].id)))
                    && sql_bind(&st, 2, SQL_SV(sv_from_cstr(order_id)))
                    && sql_bind(&st, 3, SQL_SV(sv_from_cstr(chg_s)))
                    && sql_bind(&st, 4, SQL_SV(sv_from_cstr(bal_s)))
                    && sql_bind(&st, 5, SQL_SV(sv_from_cstr(note)))
                    && sql_final_step(&st)) {
                    // ledger row
                } else {
                    fail = "Could not record the stock ledger.";
                }
                sql_finalize(&st);
                need -= take;
            }
        }
    }

    // Cash payment for the total (so sales reports + finance see it).
    if (!fail) {
        const char *amt = temp_sprintf("%.2f", subtotal);
        static const char pay_sql[] =
            "INSERT INTO payments (order_id, payment_method_dict_id, "
            "amount, payment_status) VALUES (?, 'CASH', ?, 'COMPLETED');";
        static const char pay_pg[] =
            "INSERT INTO payments (order_id, payment_method_dict_id, "
            "amount, payment_status) VALUES ($1, 'CASH', $2, 'COMPLETED');";
        static const char *const pq[SQL_LANG_COUNT] = {
            [SQL_SQLITE] = pay_sql, [SQL_MYSQL] = pay_sql,
            [SQL_POSTGRES] = pay_pg,
        };
        sql_stmt st = {0};
        if (sql_prepare(db, pq[db->lang], &st)
            && sql_bind(&st, 1, SQL_SV(sv_from_cstr(order_id)))
            && sql_bind(&st, 2, SQL_SV(sv_from_cstr(amt)))
            && sql_final_step(&st)) {
            // paid
        } else {
            fail = "Could not record the payment.";
        }
        sql_finalize(&st);
    }

    if (fail) sql_txn_rollback(db);
    else      sql_txn_commit(db);
    db_close(db);

    if (fail) { render_sale(sc, true, fail); return; }
    http_render_redirect(sc, 303, "/pos/orders");
}
