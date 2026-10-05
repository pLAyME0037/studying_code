#include "shop.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/http/utils.h"
#include "core/i18n/i18n.h"
#include "src/db/db.h"

// Guest checkout - the ONLY customer-facing order path (Phase 11):
//   * every price/total is computed from the DB cart, posted amounts are
//     ignored;
//   * stock is decremented + written to stock_ledger inside one transaction
//     together with order/items/delivery, all-or-nothing;
//   * order.org/staff are the deterministic first rows (schema: NOT NULL),
//     status starts PENDING, no payment row - staff complete it in /pos.
// The admin form at /pos/orders/create stays for staff behind auth_gate().

#define CHECKOUT_MAX_STOCK_ROWS 16

static bool sv_nonempty(String_View s) {
    for (size_t i = 0; i < s.count; ++i) {
        char c = s.data[i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return true;
    }
    return false;
}

static const char *sv_cstr(String_View s) {
    return temp_sprintf("%.*s", (int) s.count, s.data);
}

// Order status label: tr("status.<code>") with the raw dictionary id as
// the last fallback (unknown codes pass through untranslated).
static const char *status_label(const char *code) {
    if (!code || !code[0]) return "";
    const char *t = tr(temp_sprintf("status.%s", code), "");
    return t[0] ? t : code;
}

// ---------------------------------------------------------------------------
// Checkout page (GET + error re-renders from POST)
// ---------------------------------------------------------------------------

static void render_checkout(Serve_Context *sc, const Shop_Cart *cart,
                            String_View name, String_View phone,
                            String_View addr, String_View note,
                            const char *err)
{
    String_Builder content = {0};
    shop_chrome_start(&content, sc, "/checkout");

    sb_append_cstr(&content, "<div class=\"grid md:grid-cols-2 gap-3\">");

    // Summary (live DB prices only).
    sb_append_cstr(&content,
        "<div class=\"bg-mantle border border-surface0 self-start\">"
        "<div class=\"bg-text text-onbase px-3 py-2 text-sm"
        " font-semibold\">");
    sb_append_html_escaped(&content,
                           tr("checkout.order_title",
                              "ការបញ្ជាទិញរបស់អ្នក"));
    sb_append_cstr(&content, "</div>");
    for (size_t i = 0; i < cart->count; ++i) {
        const Shop_Cart_Item *it = &cart->items[i];
        if (!it->valid) continue;
        sb_append_cstr(&content,
            "<div class=\"px-3 py-2 border-b border-surface0 flex"
            " items-center gap-2 text-sm\">"
            "<div class=\"flex-1 min-w-0\"><div class=\"text-text"
            " truncate\">");
        sb_append_html_escaped(&content, it->name);
        sb_append_cstr(&content, "</div><div class=\"text-xs text-overlay0\">");
        sb_append_html_escaped(&content, it->sku);
        sb_append_cstr(&content, " × ");
        sb_appendf(&content, "%d", it->qty);
        sb_append_cstr(&content, "</div></div><div class=\"text-text\">");
        sb_append_cstr(&content, shop_money(it->price * it->qty));
        sb_append_cstr(&content, "</div></div>");
    }
    sb_append_cstr(&content,
        "<div class=\"px-3 py-2 flex items-center justify-between"
        " bg-surface0/50 border-t border-surface0 text-sm\">"
        "<span class=\"font-semibold text-text\">");
    sb_append_html_escaped(&content, tr("checkout.total", "សរុប"));
    sb_append_cstr(&content,
        "</span>"
        "<span class=\"font-semibold text-text\">");
    sb_append_cstr(&content, shop_money(cart->subtotal));
    sb_append_cstr(&content, "</span></div></div>");

    // Form.
    sb_append_cstr(&content,
        "<form method=\"POST\" action=\"/checkout\" data-checkout-form"
        " class=\"bg-mantle border border-surface0 px-3 py-3 flex flex-col"
        " gap-2\">");
    if (err && err[0]) {
        sb_append_cstr(&content,
            "<div data-checkout-error class=\"bg-red/10 border-l-2"
            " border-red text-red px-2 py-1.5 text-xs\">");
        sb_append_html_escaped(&content, err);
        sb_append_cstr(&content, "</div>");
    }
    sb_append_cstr(&content,
        "<label class=\"text-xs text-subtext0\">");
    sb_append_html_escaped(&content,
                           tr("checkout.label_name", "ឈ្មោះ"));
    sb_append_cstr(&content,
        "</label>"
        "<input name=\"name\" required value=\"");
    sb_append_html_escaped(&content, sv_cstr(name));
    sb_append_cstr(&content,
        "\" class=\"border border-surface0 bg-base text-text px-2 py-1.5"
        " text-sm\">"
        "<label class=\"text-xs text-subtext0\">");
    sb_append_html_escaped(&content,
                           tr("checkout.label_phone", "លេខទូរស័ព្ទ"));
    sb_append_cstr(&content,
        "</label>"
        "<input name=\"phone\" required value=\"");
    sb_append_html_escaped(&content, sv_cstr(phone));
    sb_append_cstr(&content,
        "\" class=\"border border-surface0 bg-base text-text px-2 py-1.5"
        " text-sm\">"
        "<label class=\"text-xs text-subtext0\">");
    sb_append_html_escaped(&content,
                           tr("checkout.label_address", "អាសយដ្ឋាន"));
    sb_append_cstr(&content,
        "</label>"
        "<input name=\"address\" required value=\"");
    sb_append_html_escaped(&content, sv_cstr(addr));
    sb_append_cstr(&content,
        "\" class=\"border border-surface0 bg-base text-text px-2 py-1.5"
        " text-sm\">"
        "<label class=\"text-xs text-subtext0\">");
    sb_append_html_escaped(&content,
                           tr("checkout.label_note", "មតិ (ស្រេចចិត្ត)"));
    sb_append_cstr(&content,
        "</label>"
        "<input name=\"note\" value=\"");
    sb_append_html_escaped(&content, sv_cstr(note));
    sb_append_cstr(&content,
        "\" class=\"border border-surface0 bg-base text-text px-2 py-1.5"
        " text-sm\">"
        "<button class=\"bg-blue text-onbase hover:brightness-90 px-2"
        " py-1.5 text-sm\">");
    sb_append_html_escaped(&content,
                           tr("checkout.submit", "បញ្ជាទិញ"));
    sb_append_cstr(&content, "</button>"
        "</form></div>");

    shop_chrome_end(&content);
    shop_render(sc, 200, tr("checkout.page_title", "ការបង់ប្រាក់"), &content);
    sb_free(content);
}

void serve_shop_checkout(Serve_Context *sc) {
    Shop_Cart cart = {0};
    shop_cart_load(sc, &cart);
    size_t live = 0;
    for (size_t i = 0; i < cart.count; ++i) live += cart.items[i].valid ? 1 : 0;
    if (live == 0) {
        http_render_redirect(sc, 303, "/cart");
        return;
    }
    String_View none = {0};
    render_checkout(sc, &cart, none, none, none, none, NULL);
}

// ---------------------------------------------------------------------------
// POST /checkout - create the order
// ---------------------------------------------------------------------------

typedef struct {
    char   id[40];
    double qty;
} Stock_Row;

void serve_shop_checkout_post(Serve_Context *sc) {
    String_View req  = sb_to_sv(sc->request);
    String_View body = sb_to_sv(sc->body);
    String_View name = form_text(req, body, "name");
    String_View phone = form_text(req, body, "phone");
    String_View addr = form_text(req, body, "address");
    String_View note = form_text(req, body, "note");

    Shop_Cart cart = {0};
    shop_cart_load(sc, &cart);
    size_t live = 0;
    for (size_t i = 0; i < cart.count; ++i) live += cart.items[i].valid ? 1 : 0;
    if (live == 0) {
        http_render_redirect(sc, 303, "/cart");
        return;
    }
    if (!sv_nonempty(name)) {
        render_checkout(sc, &cart, name, phone, addr, note,
                        tr("checkout.err_name", "សូមបំពេញឈ្មោះ"));
        return;
    }
    if (!sv_nonempty(phone) || phone.count < 6) {
        render_checkout(sc, &cart, name, phone, addr, note,
                        tr("checkout.err_phone",
                           "សូមបញ្ចូលលេខទូរស័ព្ទត្រឹមត្រូវ"));
        return;
    }
    if (!sv_nonempty(addr)) {
        render_checkout(sc, &cart, name, phone, addr, note,
                        tr("checkout.err_address", "សូមបំពេញអាសយដ្ឋាន"));
        return;
    }

    db_t *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }
    if (!sql_txn_begin(db)) { db_close(db); serve_error(sc, 500); return; }

    const char *fail = NULL;
    char order_id[40] = {0};
    const char *order_number = NULL;
    double subtotal = cart.subtotal;

    // Deterministic org/staff (orders.* are NOT NULL; seeds guarantee both).
    char org_id[40] = {0}, staff_id[40] = {0};
    {
        static const char *const org_q[SQL_LANG_COUNT] = {
            [SQL_SQLITE]   = "SELECT id FROM org_units "
                             "WHERE deleted_at IS NULL ORDER BY id LIMIT 1;",
            [SQL_MYSQL]    = "SELECT id FROM org_units "
                             "WHERE deleted_at IS NULL ORDER BY id LIMIT 1;",
            [SQL_POSTGRES] = "SELECT id FROM org_units "
                             "WHERE deleted_at IS NULL ORDER BY id LIMIT 1;",
        };
        sql_stmt stmt = {0};
        if (sql_prepare(db, org_q[db->lang], &stmt)
            && sql_step(&stmt) == SQL_ROW) {
            const char *v = sql_col_text(&stmt, 0);
            if (v) snprintf(org_id, sizeof(org_id), "%s", v);
        }
        sql_finalize(&stmt);
        stmt = (sql_stmt) {0};
        if (sql_prepare(db,
                "SELECT id FROM staff WHERE deleted_at IS NULL "
                "ORDER BY id LIMIT 1;", &stmt)
            && sql_step(&stmt) == SQL_ROW) {
            const char *v = sql_col_text(&stmt, 0);
            if (v) snprintf(staff_id, sizeof(staff_id), "%s", v);
        }
        sql_finalize(&stmt);
    }
    if (!org_id[0] || !staff_id[0]) {
        fail = tr("checkout.err_org", "មិនអាចកំណត់ហាងបានទេ");
    }

    // Customer: reuse by phone, else create customers (+ users) rows.
    char customer_id[40] = {0};
    if (!fail) {
        static const char *const find_q[SQL_LANG_COUNT] = {
            [SQL_SQLITE]   = "SELECT id, COALESCE(customer_id, ''), "
                             "user_type_dict_id FROM users "
                             "WHERE phone = ? AND deleted_at IS NULL "
                             "LIMIT 1;",
            [SQL_MYSQL]    = "SELECT id, COALESCE(customer_id, ''), "
                             "user_type_dict_id FROM users "
                             "WHERE phone = ? AND deleted_at IS NULL "
                             "LIMIT 1;",
            [SQL_POSTGRES] = "SELECT id, COALESCE(customer_id, ''), "
                             "user_type_dict_id FROM users "
                             "WHERE phone = $1 AND deleted_at IS NULL "
                             "LIMIT 1;",
        };
        char found_uid[40] = {0}, found_cid[40] = {0};
        char found_type[40] = {0};
        {
            sql_stmt stmt = {0};
            if (sql_prepare(db, find_q[db->lang], &stmt)
                && sql_bind(&stmt, 1, SQL_SV(phone))
                && sql_step(&stmt) == SQL_ROW) {
                const char *u = sql_col_text(&stmt, 0);
                const char *c = sql_col_text(&stmt, 1);
                const char *t = sql_col_text(&stmt, 2);
                if (u) snprintf(found_uid, sizeof(found_uid), "%s", u);
                if (c) snprintf(found_cid, sizeof(found_cid), "%s", c);
                if (t) snprintf(found_type, sizeof(found_type), "%s", t);
            }
            sql_finalize(&stmt);
        }

        if (found_cid[0]) {
            snprintf(customer_id, sizeof(customer_id), "%s", found_cid);
        } else {
            // New customers row (defaults: TEIR_1, 0 points).
            static const char *const ins_q[SQL_LANG_COUNT] = {
                [SQL_SQLITE]   = "INSERT INTO customers (id) VALUES (?);",
                [SQL_MYSQL]    = "INSERT INTO customers (id) VALUES (?);",
                [SQL_POSTGRES] = "INSERT INTO customers (id) VALUES ($1);",
            };
            char cid[40];
            webc_uuid(cid);
            sql_stmt stmt = {0};
            if (sql_prepare(db, ins_q[db->lang], &stmt)
                && sql_bind(&stmt, 1, SQL_SV(sv_from_cstr(cid)))
                && sql_final_step(&stmt)) {
                snprintf(customer_id, sizeof(customer_id), "%s", cid);
            }
            sql_finalize(&stmt);
            if (!customer_id[0]) {
                fail = tr("checkout.err_customer",
                          "មិនអាចបង្កើតគណនីអតិថិជនបានទេ");
            } else if (found_uid[0]
                       && strcmp(found_type, "CUSTOMER") == 0) {
                // Existing customer-type user without a link: attach it so
                // reports resolve the name through users.customer_id.
                static const char *const link_q[SQL_LANG_COUNT] = {
                    [SQL_SQLITE]   = "UPDATE users SET customer_id = ? "
                                     "WHERE id = ?;",
                    [SQL_MYSQL]    = "UPDATE users SET customer_id = ? "
                                     "WHERE id = ?;",
                    [SQL_POSTGRES] = "UPDATE users SET customer_id = $1 "
                                     "WHERE id = $2;",
                };
                sql_stmt stmt = {0};
                if (sql_prepare(db, link_q[db->lang], &stmt)
                    && sql_bind(&stmt, 1, SQL_SV(sv_from_cstr(customer_id)))
                    && sql_bind(&stmt, 2, SQL_SV(sv_from_cstr(found_uid)))
                    && sql_final_step(&stmt)) {
                    // linked
                } else {
                    fail = tr("checkout.err_link",
                              "មិនអាចភ្ជាប់គណនីអតិថិជនបានទេ");
                }
                sql_finalize(&stmt);
            } else if (!found_uid[0]) {
                // Fresh guest: users row so /pos/users shows the buyer.
                // username/email derive from the unique phone; the empty
                // hash keeps the account out of any login (login also
                // requires ADMIN).
                static const char *const uq[SQL_LANG_COUNT] = {
                    [SQL_SQLITE] = "INSERT INTO users "
                        "(id, name, username, email, phone, password_hash, "
                         "user_type_dict_id, customer_id) "
                        "VALUES (?, ?, ?, ?, ?, '', 'CUSTOMER', ?);",
                    [SQL_MYSQL] = "INSERT INTO users "
                        "(id, name, username, email, phone, password_hash, "
                         "user_type_dict_id, customer_id) "
                        "VALUES (?, ?, ?, ?, ?, '', 'CUSTOMER', ?);",
                    [SQL_POSTGRES] = "INSERT INTO users "
                        "(id, name, username, email, phone, password_hash, "
                         "user_type_dict_id, customer_id) "
                        "VALUES ($1, $2, $3, $4, $5, '', 'CUSTOMER', $6);",
                };
                char uid[40];
                webc_uuid(uid);
                const char *ph = sv_cstr(phone);
                sql_stmt stmt = {0};
                if (sql_prepare(db, uq[db->lang], &stmt)
                    && sql_bind(&stmt, 1, SQL_SV(sv_from_cstr(uid)))
                    && sql_bind(&stmt, 2, SQL_SV(name))
                    && sql_bind(&stmt, 3, SQL_SV(sv_from_cstr(ph)))
                    && sql_bind(&stmt, 4, SQL_SV(sv_from_cstr(
                        temp_sprintf("%s@pos.kh", ph))))
                    && sql_bind(&stmt, 5, SQL_SV(phone))
                    && sql_bind(&stmt, 6, SQL_SV(sv_from_cstr(customer_id)))
                    && sql_final_step(&stmt)) {
                    // created
                } else {
                    fail = tr("checkout.err_customer",
                          "មិនអាចបង្កើតគណនីអតិថិជនបានទេ");
                }
                sql_finalize(&stmt);
            }
            // else: phone belongs to a non-CUSTOMER user (staff) - keep the
            // order customer separate, contact travels in the delivery row.
        }
    }

    // Order row.
    if (!fail) {
        webc_uuid(order_id);
        char onum[40], uuid_tail[40];
        time_t now = time(NULL);
        struct tm tmv;
        gmtime_r(&now, &tmv);
        webc_uuid(uuid_tail);
        // Strip dashes: "WEB-YYMMDD-" + first 8 hex chars = 19 chars.
        {
            char *src = uuid_tail, *dst = uuid_tail;
            while (*src) { if (*src != '-') *dst++ = *src; ++src; }
            *dst = '\0';
        }
        snprintf(onum, sizeof(onum), "WEB-%02d%02d%02d-%.8s",
                 (tmv.tm_year + 1900) % 100, tmv.tm_mon + 1, tmv.tm_mday,
                 uuid_tail);
        order_number = temp_strdup(onum);

        static const char *const oq[SQL_LANG_COUNT] = {
            [SQL_SQLITE] = "INSERT INTO orders "
                "(id, org_unit_id, staff_id, customer_id, order_number, "
                 "order_status_dict_id, subtotal, discount_amount, "
                 "tax_amount, delivery_fee, total_amount) "
                "VALUES (?, ?, ?, ?, ?, 'PENDING', ?, 0, 0, 0, ?);",
            [SQL_MYSQL] = "INSERT INTO orders "
                "(id, org_unit_id, staff_id, customer_id, order_number, "
                 "order_status_dict_id, subtotal, discount_amount, "
                 "tax_amount, delivery_fee, total_amount) "
                "VALUES (?, ?, ?, ?, ?, 'PENDING', ?, 0, 0, 0, ?);",
            [SQL_POSTGRES] = "INSERT INTO orders "
                "(id, org_unit_id, staff_id, customer_id, order_number, "
                 "order_status_dict_id, subtotal, discount_amount, "
                 "tax_amount, delivery_fee, total_amount) "
                "VALUES ($1, $2, $3, $4, $5, 'PENDING', $6, 0, 0, 0, $7);",
        };
        const char *sub = temp_sprintf("%.2f", subtotal);
        sql_stmt stmt = {0};
        if (sql_prepare(db, oq[db->lang], &stmt)
            && sql_bind(&stmt, 1, SQL_SV(sv_from_cstr(order_id)))
            && sql_bind(&stmt, 2, SQL_SV(sv_from_cstr(org_id)))
            && sql_bind(&stmt, 3, SQL_SV(sv_from_cstr(staff_id)))
            && sql_bind(&stmt, 4, SQL_SV(sv_from_cstr(customer_id)))
            && sql_bind(&stmt, 5, SQL_SV(sv_from_cstr(order_number)))
            && sql_bind(&stmt, 6, SQL_SV(sv_from_cstr(sub)))
            && sql_bind(&stmt, 7, SQL_SV(sv_from_cstr(sub)))
            && sql_final_step(&stmt)) {
            // inserted
        } else {
            fail = tr("checkout.err_save_order",
                      "មិនអាចរក្សាទុកការបញ្ជាទិញបានទេ");
        }
        sql_finalize(&stmt);
    }

    // Lines + stock movement.
    if (!fail) {
        static const char *const iq[SQL_LANG_COUNT] = {
            [SQL_SQLITE] = "INSERT INTO order_items "
                "(order_id, product_id, variant_id, unit_price, unit_cost, "
                 "quantity, discount_amount, tax_amount, total_line) "
                "VALUES (?, ?, NULL, ?, ?, ?, 0, 0, ?);",
            [SQL_MYSQL] = "INSERT INTO order_items "
                "(order_id, product_id, variant_id, unit_price, unit_cost, "
                 "quantity, discount_amount, tax_amount, total_line) "
                "VALUES (?, ?, NULL, ?, ?, ?, 0, 0, ?);",
            [SQL_POSTGRES] = "INSERT INTO order_items "
                "(order_id, product_id, variant_id, unit_price, unit_cost, "
                 "quantity, discount_amount, tax_amount, total_line) "
                "VALUES ($1, $2, NULL, $3, $4, $5, 0, 0, $6);",
        };
        static const char stock_sql[] =
            "SELECT id, quantity FROM inventory_stocks "
            "WHERE product_id = ? AND deleted_at IS NULL "
            "ORDER BY CASE WHEN org_unit_id = ? THEN 0 ELSE 1 END, id;";
        static const char stock_pg[] =
            "SELECT id, quantity FROM inventory_stocks "
            "WHERE product_id = $1 AND deleted_at IS NULL "
            "ORDER BY CASE WHEN org_unit_id = $2 THEN 0 ELSE 1 END, id;";
        static const char *const stock_q[SQL_LANG_COUNT] = {
            [SQL_SQLITE]   = stock_sql,
            [SQL_MYSQL]    = stock_sql,
            [SQL_POSTGRES] = stock_pg,
        };
        static const char *const dec_q[SQL_LANG_COUNT] = {
            [SQL_SQLITE] = "UPDATE inventory_stocks SET quantity = "
                           "quantity - ? WHERE id = ?;",
            [SQL_MYSQL]    = "UPDATE inventory_stocks SET quantity = "
                             "quantity - ? WHERE id = ?;",
            [SQL_POSTGRES] = "UPDATE inventory_stocks SET quantity = "
                             "quantity - $1 WHERE id = $2;",
        };
        static const char *const led_q[SQL_LANG_COUNT] = {
            [SQL_SQLITE] = "INSERT INTO stock_ledger "
                "(stock_id, reference_type, reference_id, quantity_change, "
                 "balance_after, note) VALUES (?, 'ORDER', ?, ?, ?, ?);",
            [SQL_MYSQL] = "INSERT INTO stock_ledger "
                "(stock_id, reference_type, reference_id, quantity_change, "
                 "balance_after, note) VALUES (?, 'ORDER', ?, ?, ?, ?);",
            [SQL_POSTGRES] = "INSERT INTO stock_ledger "
                "(stock_id, reference_type, reference_id, quantity_change, "
                 "balance_after, note) VALUES ($1, 'ORDER', $2, $3, $4, $5);",
        };

        for (size_t i = 0; i < cart.count && !fail; ++i) {
            const Shop_Cart_Item *it = &cart.items[i];
            if (!it->valid) continue;

            const char *unit = temp_sprintf("%.2f", it->price);
            const char *cost = temp_sprintf("%.2f", it->cost);
            const char *line = temp_sprintf("%.2f", it->price * it->qty);
            const char *qty = temp_sprintf("%.2f", (double) it->qty);
            sql_stmt stmt = {0};
            if (sql_prepare(db, iq[db->lang], &stmt)
                && sql_bind(&stmt, 1, SQL_SV(sv_from_cstr(order_id)))
                && sql_bind(&stmt, 2, SQL_SV(sv_from_cstr(it->id)))
                && sql_bind(&stmt, 3, SQL_SV(sv_from_cstr(unit)))
                && sql_bind(&stmt, 4, SQL_SV(sv_from_cstr(cost)))
                && sql_bind(&stmt, 5, SQL_SV(sv_from_cstr(qty)))
                && sql_bind(&stmt, 6, SQL_SV(sv_from_cstr(line)))
                && sql_final_step(&stmt)) {
                // line stored
            } else {
                fail = tr("checkout.err_save_line",
                          "មិនអាចរក្សាទុកខ្សែទំនិញបានទេ");
            }
            sql_finalize(&stmt);
            if (fail) break;

            // Stock: prefer this org's rows, walk them greedily.
            Stock_Row rows[CHECKOUT_MAX_STOCK_ROWS];
            size_t n_rows = 0;
            double available = 0;
            stmt = (sql_stmt) {0};
            if (sql_prepare(db, stock_q[db->lang], &stmt)
                && sql_bind(&stmt, 1, SQL_SV(sv_from_cstr(it->id)))
                && sql_bind(&stmt, 2, SQL_SV(sv_from_cstr(org_id)))) {
                while (sql_step(&stmt) == SQL_ROW
                       && n_rows < CHECKOUT_MAX_STOCK_ROWS) {
                    const char *sid = sql_col_text(&stmt, 0);
                    const char *sq = sql_col_text(&stmt, 1);
                    if (!sid) continue;
                    snprintf(rows[n_rows].id, sizeof(rows[n_rows].id), "%s",
                             sid);
                    rows[n_rows].qty = sq && sq[0] ? atof(sq) : 0.0;
                    available += rows[n_rows].qty;
                    ++n_rows;
                }
            } else {
                fail = tr("checkout.err_read_stock",
                          "មិនអាចអានស្តុកបានទេ");
            }
            sql_finalize(&stmt);
            if (fail) break;

            if (available + 1e-9 < (double) it->qty) {
                fail = tr("checkout.err_low_stock",
                          "ស្តុកមិនគ្រប់គ្រាន់សម្រាប់ការបញ្ជាទិញនេះ");
                break;
            }

            double need = (double) it->qty;
            for (size_t r = 0; r < n_rows && need > 1e-9; ++r) {
                double take = rows[r].qty < need ? rows[r].qty : need;
                if (take <= 0) continue;
                double balance = rows[r].qty - take;
                const char *take_s = temp_sprintf("%.4f", take);
                const char *chg_s = temp_sprintf("-%.4f", take);
                const char *bal_s = temp_sprintf("%.4f", balance);

                stmt = (sql_stmt) {0};
                if (sql_prepare(db, dec_q[db->lang], &stmt)
                    && sql_bind(&stmt, 1, SQL_SV(sv_from_cstr(take_s)))
                    && sql_bind(&stmt, 2, SQL_SV(sv_from_cstr(rows[r].id)))
                    && sql_final_step(&stmt)) {
                    // decremented
                } else {
                    fail = tr("checkout.err_dec_stock",
                              "មិនអាចកែប្រែស្តុកបានទេ");
                }
                sql_finalize(&stmt);
                if (fail) break;

                const char *note_txt = temp_sprintf("កម្មង់ %s", order_number);
                stmt = (sql_stmt) {0};
                if (sql_prepare(db, led_q[db->lang], &stmt)
                    && sql_bind(&stmt, 1, SQL_SV(sv_from_cstr(rows[r].id)))
                    && sql_bind(&stmt, 2, SQL_SV(sv_from_cstr(order_id)))
                    && sql_bind(&stmt, 3, SQL_SV(sv_from_cstr(chg_s)))
                    && sql_bind(&stmt, 4, SQL_SV(sv_from_cstr(bal_s)))
                    && sql_bind(&stmt, 5, SQL_SV(sv_from_cstr(note_txt)))
                    && sql_final_step(&stmt)) {
                    // ledger row
                } else {
                    fail = tr("checkout.err_ledger",
                              "មិនអាចកត់ត្រាបញ្ជីស្តុកបានទេ");
                }
                sql_finalize(&stmt);
                need -= take;
            }
        }
    }

    // Delivery row carries the guest contact for staff fulfilment.
    if (!fail) {
        static const char *const dq[SQL_LANG_COUNT] = {
            [SQL_SQLITE] = "INSERT INTO deliveries "
                "(order_id, recipient_name, recipient_phone, "
                 "delivery_address) VALUES (?, ?, ?, ?);",
            [SQL_MYSQL] = "INSERT INTO deliveries "
                "(order_id, recipient_name, recipient_phone, "
                 "delivery_address) VALUES (?, ?, ?, ?);",
            [SQL_POSTGRES] = "INSERT INTO deliveries "
                "(order_id, recipient_name, recipient_phone, "
                 "delivery_address) VALUES ($1, $2, $3, $4);",
        };
        sql_stmt stmt = {0};
        if (sql_prepare(db, dq[db->lang], &stmt)
            && sql_bind(&stmt, 1, SQL_SV(sv_from_cstr(order_id)))
            && sql_bind(&stmt, 2, SQL_SV(name))
            && sql_bind(&stmt, 3, SQL_SV(phone))
            && sql_bind(&stmt, 4, SQL_SV(addr))
            && sql_final_step(&stmt)) {
            // delivery row
        } else {
            fail = tr("checkout.err_delivery",
                      "មិនអាចកំណត់ការដឹកជញ្ជូនបានទេ");
        }
        sql_finalize(&stmt);
    }

    if (fail) {
        sql_txn_rollback(db);
        db_close(db);
        render_checkout(sc, &cart, name, phone, addr, note, fail);
        return;
    }
    if (!sql_txn_commit(db)) {
        db_close(db);
        serve_error(sc, 500);
        return;
    }
    db_close(db);

    // Empty the cart, then celebrate.
    shop_cart_save(sc, &(Shop_Cart) {0});
    http_render_redirect(sc, 303,
                         temp_sprintf("/order/%s", order_id));
}

// ---------------------------------------------------------------------------
// GET /order/<id> - guest confirmation (uuid id is the unguessable token)
// ---------------------------------------------------------------------------

void serve_shop_order(Serve_Context *sc) {
    Route_Id id = sc->route_id;
    if (id.kind == ID_NONE) { serve_error(sc, 404); return; }

    db_t *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }

    char number[64] = {0}, status[40] = {0};
    char rname[128] = {0}, rphone[64] = {0}, raddr[256] = {0};
    double total = 0;
    char created[40] = {0};
    bool found = false;
    {
        static const char sql[] =
            "SELECT o.order_number, COALESCE(o.order_status_dict_id, ''), "
            "o.total_amount, o.created_at, "
            "COALESCE((SELECT recipient_name FROM deliveries "
            "          WHERE order_id = o.id LIMIT 1), ''), "
            "COALESCE((SELECT recipient_phone FROM deliveries "
            "          WHERE order_id = o.id LIMIT 1), ''), "
            "COALESCE((SELECT delivery_address FROM deliveries "
            "          WHERE order_id = o.id LIMIT 1), '') "
            "FROM orders o WHERE o.id = ? AND o.deleted_at IS NULL;";
        sql_stmt stmt = {0};
        if (sql_prepare(db, sql, &stmt)
            && sql_bind(&stmt, 1, SQL_SV(id.raw))
            && sql_step(&stmt) == SQL_ROW) {
            snprintf(number, sizeof(number), "%s",
                     sql_col_text(&stmt, 0) ? sql_col_text(&stmt, 0) : "");
            snprintf(status, sizeof(status), "%s",
                     sql_col_text(&stmt, 1) ? sql_col_text(&stmt, 1) : "");
            const char *t = sql_col_text(&stmt, 2);
            total = t && t[0] ? atof(t) : 0.0;
            snprintf(created, sizeof(created), "%s",
                     sql_col_text(&stmt, 3) ? sql_col_text(&stmt, 3) : "");
            snprintf(rname, sizeof(rname), "%s",
                     sql_col_text(&stmt, 4) ? sql_col_text(&stmt, 4) : "");
            snprintf(rphone, sizeof(rphone), "%s",
                     sql_col_text(&stmt, 5) ? sql_col_text(&stmt, 5) : "");
            snprintf(raddr, sizeof(raddr), "%s",
                     sql_col_text(&stmt, 6) ? sql_col_text(&stmt, 6) : "");
            found = true;
        }
        sql_finalize(&stmt);
    }

    // Lines.
    String_Builder lines = {0};
    if (found) {
        static const char sql[] =
            "SELECT p.name, oi.unit_price, oi.quantity, oi.total_line "
            "FROM order_items oi JOIN products p ON p.id = oi.product_id "
            "WHERE oi.order_id = ? AND oi.deleted_at IS NULL "
            "ORDER BY oi.created_at, oi.id;";
        sql_stmt stmt = {0};
        if (sql_prepare(db, sql, &stmt)
            && sql_bind(&stmt, 1, SQL_SV(id.raw))) {
            while (sql_step(&stmt) == SQL_ROW) {
                const char *nm = sql_col_text(&stmt, 0)
                    ? sql_col_text(&stmt, 0) : "";
                const char *qt = sql_col_text(&stmt, 2);
                const char *tl = sql_col_text(&stmt, 3);
                sb_append_cstr(&lines,
                    "<div class=\"px-3 py-2 border-b border-surface0"
                    " flex items-center gap-2 text-sm\">"
                    "<div class=\"flex-1 min-w-0 text-text truncate\">");
                sb_append_html_escaped(&lines, nm);
                sb_append_cstr(&lines, "</div>"
                    "<div class=\"text-xs text-overlay0\">× ");
                sb_appendf(&lines, "%.0f",
                           qt && qt[0] ? atof(qt) : 0.0);
                sb_append_cstr(&lines, "</div>"
                    "<div class=\"text-text\">");
                sb_append_cstr(&lines,
                    shop_money((tl && tl[0] ? atof(tl) : 0.0)));
                sb_append_cstr(&lines, "</div></div>");
            }
        }
        sql_finalize(&stmt);
    }
    db_close(db);

    if (!found) { sb_free(lines); serve_error(sc, 404); return; }

    String_Builder content = {0};
    shop_chrome_start(&content, sc, "/order");
    sb_append_cstr(&content,
        "<div class=\"bg-mantle border border-surface0 max-w-lg mx-auto"
        " w-full\">"
        "<div class=\"bg-green text-onbase px-3 py-3\">"
        "<div class=\"font-semibold\">");
    sb_append_html_escaped(&content,
                           tr("order.thanks", "អរគុណសម្រាប់ការបញ្ជាទិញ!"));
    sb_append_cstr(&content,
        "</div>"
        "<div class=\"text-xs text-onbase/80\">");
    sb_append_html_escaped(&content, tr("order.number", "លេខបញ្ជាទិញ"));
    sb_append_cstr(&content, ": ");
    sb_append_html_escaped(&content, number);
    sb_append_cstr(&content, " · ");
    sb_append_html_escaped(&content, created);
    sb_append_cstr(&content, "</div></div>"
        "<div class=\"px-3 py-2 border-b border-surface0 flex items-center"
        " gap-2\">"
        "<span class=\"bg-yellow text-onbase border border-yellow"
        " px-2 py-0.5 text-xs\">");
    sb_append_html_escaped(&content, status_label(status));
    sb_append_cstr(&content, "</span>"
        "<span class=\"text-xs text-overlay0\">");
    sb_append_html_escaped(&content,
                           tr("order.status_now", "ស្ថានភាពបច្ចុប្បន្ន"));
    sb_append_cstr(&content, "</span>"
        "</div>");
    sb_append_buf(&content, lines.items, lines.count);
    sb_append_cstr(&content,
        "<div class=\"px-3 py-2 flex items-center justify-between"
        " bg-surface0/50 border-y border-surface0 text-sm\">"
        "<span class=\"font-semibold text-text\">");
    sb_append_html_escaped(&content,
                           tr("checkout.total_due", "សរុបត្រូវបង់"));
    sb_append_cstr(&content,
        "</span>"
        "<span class=\"font-semibold text-text\">");
    sb_append_cstr(&content, shop_money(total));
    sb_append_cstr(&content, "</span></div>"
        "<div class=\"px-3 py-2 text-xs text-subtext0 border-b"
        " border-surface0\">");
    sb_append_html_escaped(&content, tr("checkout.label_name", "ឈ្មោះ"));
    sb_append_cstr(&content, ": ");
    sb_append_html_escaped(&content, rname);
    sb_append_cstr(&content, " · ");
    sb_append_html_escaped(&content, tr("checkout.label_phone", "លេខទូរស័ព្ទ"));
    sb_append_cstr(&content, ": ");
    sb_append_html_escaped(&content, rphone);
    sb_append_cstr(&content, "<br>");
    sb_append_html_escaped(&content, tr("checkout.label_address", "អាសយដ្ឋាន"));
    sb_append_cstr(&content, ": ");
    sb_append_html_escaped(&content, raddr);
    sb_append_cstr(&content,
        "</div>"
        "<div class=\"px-3 py-2 text-xs text-subtext0\">");
    sb_append_html_escaped(&content,
                           tr("order.contact_note",
                              "យើងនឹងទាក់ទងលោកអ្នកដើម្បីបញ្ជាក់ការ"
                              "បញ្ជាទិញ។"));
    sb_append_cstr(&content,
        "</div>"
        "<div class=\"px-3 py-2 border-t border-surface0\">"
        "<a href=\"/\" class=\"text-xs text-blue hover:underline\">");
    sb_append_html_escaped(&content, tr("shop.back", "« ត្រឡប់ទៅហាង"));
    sb_append_cstr(&content, "</a>"
        "</div></div>");
    shop_chrome_end(&content);
    shop_render(sc, 200, number, &content);
    sb_free(content);
    sb_free(lines);
}
