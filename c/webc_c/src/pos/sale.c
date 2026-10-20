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
// /pos/sale: dedicated in-store POS sale. The cashier finds products with a
// faceted finder - nullable Category, Type, SKU and Barcode dropdowns, each
// an individual trigger that narrows the product list (filtered client-side
// so already-built sale lines survive). The picked product's price is shown
// as read-only text; every other attribute is just a way to locate it. The
// server then builds the whole order - staff/org from the session, customer
// NULL, status PAID, auto id + order number, delivery fee 0 - and computes
// every money figure from products.base_price (+ variant price_delta) x
// quantity. A CASH payment is written and stock is decremented, all in one
// transaction. The online / e-shop checkout path is untouched.
// =========================================================================

#define SALE_MAX_LINES 64
#define SALE_MAX_STOCK_ROWS 16

typedef struct { char id[40]; double qty; } Stock_Row;

static void sv_copy(char *dst, size_t sz, String_View sv) {
    size_t n = sv.count < sz - 1 ? sv.count : sz - 1;
    if (n) memcpy(dst, sv.data, n);
    dst[n] = '\0';
}

// Emit a JSON string literal (with surrounding quotes). This JSON is
// embedded straight into a <script> block, so escape '"', '\', control
// bytes as \uXXXX, and also the characters <, > and & as the escapes
// <, > and & - that keeps a stray "</script>" or '&' inside a
// product name from ever breaking the page.
static void json_str(String_Builder *sb, const char *s) {
    sb_append_cstr(sb, "\"");
    if (s) {
        for (const unsigned char *p = (const unsigned char *) s; *p; ++p) {
            unsigned char c = *p;
            if (c == '"')       sb_append_cstr(sb, "\\\"");
            else if (c == '\\') sb_append_cstr(sb, "\\\\");
            else if (c == '<')  sb_append_cstr(sb, "\\u003c");
            else if (c == '>')  sb_append_cstr(sb, "\\u003e");
            else if (c == '&')  sb_append_cstr(sb, "\\u0026");
            else if (c == '\n') sb_append_cstr(sb, "\\n");
            else if (c == '\r') sb_append_cstr(sb, "\\r");
            else if (c == '\t') sb_append_cstr(sb, "\\t");
            else if (c < 0x20)  sb_appendf(sb, "\\u%04x", c);
            else                sb_append_buf(sb, (const char *) &c, 1);
        }
    }
    sb_append_cstr(sb, "\"");
}

// The sellable list as a JSON array the finder narrows client-side. One row
// per product variant, plus one for products with no variants. Each entry
// carries the product-level filter fields (category name, type label, product
// sku, barcode) next to the unit sku and effective price, so the page can
// filter and total live without a round trip. Dialect-neutral SQL.
static void sale_load_units(String_Builder *json) {
    db_t *db = open_webc_db();
    if (!db) return;
    static const char q[] =
        "SELECT p.id, v.id, p.name, COALESCE(v.variant_name, ''), "
               "COALESCE(v.sku, p.sku, ''), p.sku, COALESCE(p.barcode, ''), "
               "COALESCE(c.name, ''), "
               "COALESCE(d.label, p.product_type_dict_id, ''), "
               "p.base_price + COALESCE(v.price_delta, 0) AS price "
        "FROM products p "
        "LEFT JOIN product_variants v ON v.product_id = p.id "
            "AND v.deleted_at IS NULL "
        "LEFT JOIN categories c ON c.id = p.category_id "
            "AND c.deleted_at IS NULL "
        "LEFT JOIN dictionaries d ON d.id = p.product_type_dict_id "
            "AND d.deleted_at IS NULL "
        "WHERE p.deleted_at IS NULL "
        "ORDER BY p.name;";
    sql_stmt st = {0};
    sb_append_cstr(json, "[");
    bool first = true;
    if (sql_prepare(db, q, &st)) {
        while (sql_step(&st) == SQL_ROW) {
            const char *pid   = sql_col_text(&st, 0);
            const char *vid   = sql_col_text(&st, 1);
            const char *name  = sql_col_text(&st, 2);
            const char *vname = sql_col_text(&st, 3);
            const char *usku  = sql_col_text(&st, 4);
            const char *psku  = sql_col_text(&st, 5);
            const char *bar   = sql_col_text(&st, 6);
            const char *cat   = sql_col_text(&st, 7);
            const char *type  = sql_col_text(&st, 8);
            const char *price = sql_col_text(&st, 9);
            if (!pid || !pid[0]) continue;
            const char *key = (vid && vid[0])
                ? temp_sprintf("%s:%s", pid, vid) : pid;
            String_Builder disp = {0};
            sb_append_cstr(&disp, name ? name : "");
            if (vname && vname[0]) {
                sb_append_cstr(&disp, " \xE2\x80\xA2 ");
                sb_append_cstr(&disp, vname);
            }
            sb_append_null(&disp);
            if (!first) sb_append_cstr(json, ",");
            first = false;
            sb_append_cstr(json, "{\"key\":");
            json_str(json, key);
            sb_append_cstr(json, ",\"name\":");
            json_str(json, disp.items ? disp.items : "");
            sb_append_cstr(json, ",\"sku\":");
            json_str(json, usku);
            sb_append_cstr(json, ",\"psku\":");
            json_str(json, psku);
            sb_append_cstr(json, ",\"barcode\":");
            json_str(json, bar);
            sb_append_cstr(json, ",\"cat\":");
            json_str(json, cat);
            sb_append_cstr(json, ",\"type\":");
            json_str(json, type);
            sb_appendf(json, ",\"price\":%.2f}",
                       price ? atof(price) : 0.0);
            sb_free(disp);
        }
    }
    sql_finalize(&st);
    sb_append_cstr(json, "]");
    db_close(db);
}

static void render_sale(Serve_Context *sc, bool can_sell, const char *err) {
    String_Builder units = {0};
    sale_load_units(&units);
    sb_append_null(&units);

    String_Builder *sb = &sc->body;
    sb->count = 0;
    render_page_header(sb, "New sale", "/pos/sale");

    sb_append_cstr(sb,
        "<div class=\"max-w-3xl mx-auto\">"
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
        const char *sel =
            "mt-1 w-full bg-base border border-surface0 text-sm px-2"
            " py-1.5";
        // ---- the form: finder (filters + product + read-only price +
        // qty + add line), then the built lines, then total + submit ----
        sb_append_cstr(sb,
            "<form method=\"POST\" action=\"/pos/sale\" class=\"p-4"
            " space-y-4\">"
            "<div>"
            "<div class=\"text-xs font-semibold text-subtext0 uppercase"
            " tracking-wide mb-1\">Find product</div>"
            "<div class=\"grid grid-cols-2 md:grid-cols-4 gap-2\">");
        sb_appendf(sb,
            "<label class=\"text-xs text-subtext0\">Category"
            "<select id=\"sale-cat\" class=\"%s\"></select></label>", sel);
        sb_appendf(sb,
            "<label class=\"text-xs text-subtext0\">Type"
            "<select id=\"sale-type\" class=\"%s\"></select></label>", sel);
        sb_appendf(sb,
            "<label class=\"text-xs text-subtext0\">SKU"
            "<select id=\"sale-sku\" class=\"%s\"></select></label>", sel);
        sb_appendf(sb,
            "<label class=\"text-xs text-subtext0\">Barcode"
            "<select id=\"sale-bar\" class=\"%s\"></select></label>", sel);
        sb_append_cstr(sb,
            "</div>"
            "<div class=\"flex flex-wrap items-end gap-3 mt-3\">"
            "<label class=\"flex-1 text-xs text-subtext0\">Product");
        sb_appendf(sb,
            "<select id=\"sale-product\" class=\"%s\"></select></label>",
            sel);
        sb_append_cstr(sb,
            "<div class=\"text-xs text-subtext0\">Price"
            "<div id=\"sale-price\" class=\"mt-1 px-3 py-2 text-sm"
            " font-mono bg-surface0 border border-surface0 text-text\">-"
            "</div></div>"
            "<label class=\"w-24 text-xs text-subtext0\">Qty");
        sb_appendf(sb,
            "<input id=\"sale-qty\" type=\"number\" value=\"1\" min=\"1\""
            " step=\"1\" class=\"%s\"></label>", sel);
        sb_append_cstr(sb,
            "<button type=\"button\" id=\"sale-add\" class=\"text-sm"
            " text-blue border border-surface0 px-3 py-2\">+ Add line"
            "</button>"
            "</div></div>"
            "<div id=\"sale-lines\" class=\"space-y-2\"></div>"
            "<div class=\"flex items-center justify-between pt-2 border-t"
            " border-surface0\">"
            "<div class=\"text-sm\">Total&nbsp;<span id=\"sale-total\""
            " class=\"font-semibold\">0.00</span></div>"
            "<button type=\"submit\" class=\"bg-blue text-onbase text-sm"
            " px-4 py-2\">Complete sale (paid)</button>"
            "</div>"
            "</form>");
        // ---- data: every sellable unit + its filter attributes ----
        sb_append_cstr(sb, "<script>window.SALE_UNITS=");
        sb_append_cstr(sb, units.items ? units.items : "[]");
        sb_append_cstr(sb, ";</script>");
        // ---- client-side finder + line builder ----
        sb_append_cstr(sb,
            "<script>(function(){"
            "var U=window.SALE_UNITS||[];"
            "function g(i){return document.getElementById(i);}"
            "function fmt(n){return (Math.round(n*100)/100).toFixed(2);}"
            // build each nullable filter's options ("" = all) from U
            "function distinct(fs){var s={},o=[];U.forEach(function(u){"
            "fs.forEach(function(f){var v=u[f]||'';if(v&&!s[v]){s[v]=1;"
            "o.push(v);}});});o.sort();return o;}"
            "function fill(id,vals,all){var s=g(id);"
            "while(s.firstChild)s.removeChild(s.firstChild);"
            "var a=document.createElement('option');a.value='';"
            "a.textContent=all;s.appendChild(a);"
            "vals.forEach(function(v){var o=document.createElement('option');"
            "o.value=v;o.textContent=v;s.appendChild(o);});}"
            "fill('sale-cat',distinct(['cat']),'All categories');"
            "fill('sale-type',distinct(['type']),'All types');"
            "fill('sale-sku',distinct(['sku','psku']),'All SKUs');"
            "fill('sale-bar',distinct(['barcode']),'All barcodes');"
            "var prod=g('sale-product'),priceEl=g('sale-price');"
            // a unit matches only the filters that are actually set
            "function fv(){return{cat:g('sale-cat').value,"
            "type:g('sale-type').value,sku:g('sale-sku').value,"
            "bar:g('sale-bar').value};}"
            "function ok(u,k){if(k.cat&&u.cat!==k.cat)return false;"
            "if(k.type&&u.type!==k.type)return false;"
            "if(k.sku&&u.sku!==k.sku&&u.psku!==k.sku)return false;"
            "if(k.bar&&u.barcode!==k.bar)return false;return true;}"
            // price is read-only text: mirror the picked unit's price
            "function showPrice(){var o=prod.options[prod.selectedIndex];"
            "priceEl.textContent=(o&&o.value)"
            "?o.getAttribute('data-price'):'-';}"
            "function rebuild(){var k=fv(),prev=prod.value;"
            "while(prod.firstChild)prod.removeChild(prod.firstChild);"
            "var b=document.createElement('option');b.value='';"
            "b.textContent='Select product...';prod.appendChild(b);"
            "U.forEach(function(u){if(!ok(u,k))return;"
            "var o=document.createElement('option');o.value=u.key;"
            "o.textContent=u.name+' - '+(u.sku||u.psku||'');"
            "o.setAttribute('data-price',fmt(u.price));"
            "prod.appendChild(o);});"
            "if(prev){prod.value=prev;if(prod.value!==prev)prod.value='';}"
            "showPrice();}"
            // every filter is an individual trigger -> narrows the list
            "['sale-cat','sale-type','sale-sku','sale-bar']"
            ".forEach(function(i){g(i).addEventListener('change',rebuild);});"
            "prod.addEventListener('change',showPrice);"
            "var lines=g('sale-lines'),totalEl=g('sale-total'),seq=0;"
            "function recalc(){var t=0;"
            "lines.querySelectorAll('[data-line]').forEach(function(r){"
            "t+=parseFloat(r.getAttribute('data-total'))||0;});"
            "totalEl.textContent=fmt(t);}"
            // add the picked unit + qty as a posted line, then reset
            "function add(){var o=prod.options[prod.selectedIndex];"
            "if(!o||!o.value)return;"
            "var key=o.value,label=o.textContent,"
            "pr=parseFloat(o.getAttribute('data-price'))||0;"
            "var q=parseInt(g('sale-qty').value,10);if(!(q>0))q=1;"
            "var i=seq++,row=document.createElement('div');"
            "row.setAttribute('data-line','');"
            "row.setAttribute('data-total',fmt(pr*q));"
            "row.className='flex items-center gap-2 text-sm border-t"
            " border-surface0 pt-2';"
            "var hi=document.createElement('input');hi.type='hidden';"
            "hi.name='line'+i+'_item';hi.value=key;row.appendChild(hi);"
            "var hq=document.createElement('input');hq.type='hidden';"
            "hq.name='line'+i+'_qty';hq.value=String(q);row.appendChild(hq);"
            "var nm=document.createElement('div');"
            "nm.className='flex-1 min-w-0 truncate';"
            "nm.textContent=label;row.appendChild(nm);"
            "var pc=document.createElement('div');"
            "pc.className='w-20 text-right text-subtext0';"
            "pc.textContent=fmt(pr);row.appendChild(pc);"
            "var qc=document.createElement('div');qc.className='w-10"
            " text-right';qc.textContent='x'+q;row.appendChild(qc);"
            "var tt=document.createElement('div');"
            "tt.className='w-24 text-right font-mono';"
            "tt.textContent=fmt(pr*q);row.appendChild(tt);"
            "var rm=document.createElement('button');rm.type='button';"
            "rm.setAttribute('data-rm','');rm.className='text-red px-1';"
            "rm.textContent='\\u2715';row.appendChild(rm);"
            "lines.appendChild(row);prod.value='';showPrice();"
            "g('sale-qty').value='1';recalc();}"
            "g('sale-add').addEventListener('click',add);"
            "lines.addEventListener('click',function(e){"
            "if(e.target.hasAttribute&&e.target.hasAttribute('data-rm')){"
            "var r=e.target.closest('[data-line]');if(r){r.remove();"
            "recalc();}}});"
            "rebuild();"
            "})();</script>"
            "</div></div>");
    }

    render_page_footer(sb);
    http_render_response(sc, 200, "text/html", sb_to_sv(*sb));
    sb_free(units);
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
