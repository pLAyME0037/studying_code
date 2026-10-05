#include "shop.h"

#include <stdlib.h>
#include <string.h>

#include "core/http/utils.h"
#include "src/db/db.h"

#define SHOP_PER_PAGE 12
#define SHOP_MAX_ROWS 64

// ---------------------------------------------------------------------------
// Shared chrome / rendering
// ---------------------------------------------------------------------------

const char *shop_money(double v) {
    return temp_sprintf("%.2f $", v);
}

// First UTF-8 glyph of a name, for the placeholder product tile.
static void sb_append_first_glyph(String_Builder *sb, const char *name) {
    if (!name || !name[0]) return;
    size_t len = strlen(name);
    size_t glen = 1;
    unsigned char c = (unsigned char) name[0];
    if ((c & 0xE0) == 0xC0) glen = 2;
    else if ((c & 0xF0) == 0xE0) glen = 3;
    else if ((c & 0xF8) == 0xF0) glen = 4;
    if (glen > len) glen = len;
    char buf[8];
    memcpy(buf, name, glen);
    buf[glen] = '\0';
    sb_append_html_escaped(sb, buf);
}

static size_t cart_entry_count(Serve_Context *sc) {
    String_View raw = {0};
    if (!http_cookie_find(sc, CART_COOKIE, &raw) || raw.count == 0) return 0;
    size_t n = 0;
    String_View rest = raw;
    while (rest.count > 0 && n < CART_MAX) {
        String_View pair = sv_chop_by_delim(&rest, ',');
        if (pair.count > 0) ++n;
    }
    return n;
}

static const char *shop_name(void) {
    const char *name = NULL;
    db_t *db = open_webc_db();
    if (db) {
        sql_stmt stmt = {0};
        if (sql_prepare(db,
                "SELECT config_value FROM system_configs "
                "WHERE config_key = 'pos.shop_name' "
                "AND deleted_at IS NULL;", &stmt)) {
            if (sql_step(&stmt) == SQL_ROW) {
                const char *v = sql_col_text(&stmt, 0);
                if (v && v[0]) name = temp_strdup(v);
            }
            sql_finalize(&stmt);
        }
        db_close(db);
    }
    return name && name[0] ? name : "ហាង POS";
}

void shop_chrome_start(String_Builder *sb, Serve_Context *sc,
                       const char *active) {
    (void) active;  // nav has no active states beyond the home link
    sb_append_cstr(sb,
        "<div class=\"min-h-screen flex flex-col bg-slate-50\">"
        "<header class=\"bg-white border-b border-slate-200\">"
        "<div class=\"max-w-5xl mx-auto px-3 py-2 flex items-center gap-3\">"
        "<a href=\"/\" class=\"font-bold text-slate-900 whitespace-nowrap\">");
    sb_append_html_escaped(sb, shop_name());
    sb_append_cstr(sb,
        "</a>"
        "<form method=\"GET\" action=\"/\" class=\"flex items-center gap-1 flex-1\">"
        "<input name=\"q\" placeholder=\"ស្វែងរកផលិតផល...\""
        " class=\"border border-slate-300 px-2 py-1.5 text-sm flex-1 min-w-0\">"
        "<button class=\"bg-slate-800 hover:bg-slate-900 text-white px-2"
        " py-1.5 text-sm whitespace-nowrap\">ស្វែងរក</button>"
        "</form>"
        "<a href=\"/cart\" class=\"text-sm text-slate-700 border border-slate-300"
        " px-2 py-1.5 whitespace-nowrap\">រទេះ (");
    sb_appendf(sb, "%zu", cart_entry_count(sc));
    sb_append_cstr(sb,
        ")</a>"
        "<a href=\"/dashboard\" class=\"text-sm text-indigo-600"
        " whitespace-nowrap\">ផ្ទៃគ្រប់គ្រង</a>"
        "</div></header>"
        "<main class=\"max-w-5xl mx-auto w-full px-3 py-3 flex flex-col gap-3\">");
}

void shop_chrome_end(String_Builder *sb) {
    sb_append_cstr(sb,
        "</main>"
        "<footer class=\"bg-white border-t border-slate-200 mt-auto\">"
        "<div class=\"max-w-5xl mx-auto px-3 py-2 text-xs text-slate-400\">"
        "POS · ហាងលក់រាយ</div>"
        "</footer></div>");
}

void shop_render(Serve_Context *sc, int status, const char *title,
                 String_Builder *content) {
    sc->body.count = 0;
    String_View t = sv_from_cstr(title ? title : "POS");
    render_page_shell(sc, t, sb_to_sv(*content));
    http_render_response(sc, status, "text/html", sb_to_sv(sc->body));
}

// ---------------------------------------------------------------------------
// GET / - catalog grid with category chips, search and plain ?page= links
// ---------------------------------------------------------------------------

typedef struct {
    char   id[40];
    char   name[160];
    char   sku[64];
    char   cat[96];
    double price;
    double stock;
} Shop_Row;

static double sql_col_double(sql_stmt *stmt, int col) {
    const char *v = sql_col_text(stmt, col);
    return v && v[0] ? atof(v) : 0.0;
}

// "?cat=&q=&page=" preserving the other active filters (raw values are
// html-escaped by the caller's attribute context).
static const char *shop_list_query(String_View cat, String_View q, int page) {
    return temp_sprintf("/?cat=%.*s&q=%.*s&page=%d",
                        (int) cat.count, cat.data,
                        (int) q.count, q.data, page);
}

void serve_shop_index(Serve_Context *sc) {
    // Non-NULL empty views: sql_bind maps data == NULL to SQL NULL, which
    // would turn the "= ''" tautologies below into NULL and filter out all
    // rows on a bare "/" request.
    String_View q = sv_from_cstr(""), cat = sv_from_cstr("");
    String_View page_sv = {0};
    form_find(sc->query_string, "q", &q);
    form_find(sc->query_string, "cat", &cat);
    form_find(sc->query_string, "page", &page_sv);
    if (q.count > 100) q.count = 100;

    int page = page_sv.count ? atoi(temp_sprintf("%.*s", (int) page_sv.count,
                                                 page_sv.data)) : 1;
    if (page < 1) page = 1;
    int offset = (page - 1) * SHOP_PER_PAGE;

    char pattern[320] = "%";
    if (q.count > 0) {
        snprintf(pattern, sizeof(pattern), "%%%.*s%%", (int) q.count, q.data);
    }

    static const char cat_sql[] =
        "SELECT id, name FROM categories "
        "WHERE deleted_at IS NULL ORDER BY name LIMIT 50;";
    static const char count_sql[] =
        "SELECT COUNT(*) FROM products p "
        "JOIN categories c ON c.id = p.category_id "
        "WHERE p.deleted_at IS NULL AND c.deleted_at IS NULL "
        "AND (? = '' OR c.id = ?) "
        "AND (? = '' OR p.name LIKE ? OR p.sku LIKE ?);";
    static const char rows_sql[] =
        "SELECT p.id, p.name, p.sku, p.base_price, c.name, "
        "COALESCE((SELECT SUM(s.quantity) FROM inventory_stocks s "
        "          WHERE s.product_id = p.id AND s.deleted_at IS NULL), 0) "
        "FROM products p "
        "JOIN categories c ON c.id = p.category_id "
        "WHERE p.deleted_at IS NULL AND c.deleted_at IS NULL "
        "AND (? = '' OR c.id = ?) "
        "AND (? = '' OR p.name LIKE ? OR p.sku LIKE ?) "
        "ORDER BY p.created_at DESC, p.id DESC "
        "LIMIT ? OFFSET ?;";
    static const char rows_pg[] =
        "SELECT p.id, p.name, p.sku, p.base_price, c.name, "
        "COALESCE((SELECT SUM(s.quantity) FROM inventory_stocks s "
        "          WHERE s.product_id = p.id AND s.deleted_at IS NULL), 0) "
        "FROM products p "
        "JOIN categories c ON c.id = p.category_id "
        "WHERE p.deleted_at IS NULL AND c.deleted_at IS NULL "
        "AND ($1 = '' OR c.id = $2) "
        "AND ($3 = '' OR p.name LIKE $4 OR p.sku LIKE $5) "
        "ORDER BY p.created_at DESC, p.id DESC "
        "LIMIT $6 OFFSET $7;";
    static const char *const q_rows[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = rows_sql,
        [SQL_MYSQL]    = rows_sql,
        [SQL_POSTGRES] = rows_pg,
    };
    static const char count_pg[] =
        "SELECT COUNT(*) FROM products p "
        "JOIN categories c ON c.id = p.category_id "
        "WHERE p.deleted_at IS NULL AND c.deleted_at IS NULL "
        "AND ($1 = '' OR c.id = $2) "
        "AND ($3 = '' OR p.name LIKE $4 OR p.sku LIKE $5);";
    static const char *const q_count[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = count_sql,
        [SQL_MYSQL]    = count_sql,
        [SQL_POSTGRES] = count_pg,
    };

    db_t *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }

    // Category chips.
    String_Builder cats = {0};
    {
        sql_stmt stmt = {0};
        if (sql_prepare(db, cat_sql, &stmt)) {
            while (sql_step(&stmt) == SQL_ROW) {
                const char *cid = sql_col_text(&stmt, 0);
                const char *cname = sql_col_text(&stmt, 1);
                if (!cid || !cname) continue;
                bool on = cat.count == strlen(cid)
                    && memcmp(cat.data, cid, cat.count) == 0;
                sb_append_cstr(&cats, "<a href=\"");
                sb_append_html_escaped(&cats,
                    shop_list_query(sv_from_cstr(cid), q, 1));
                sb_append_cstr(&cats, on
                    ? "\" class=\"px-2 py-1.5 text-xs border border-indigo-600"
                      " bg-indigo-600 text-white\">"
                    : "\" class=\"px-2 py-1.5 text-xs border border-slate-300"
                      " bg-white text-slate-600 hover:border-slate-400\">");
                sb_append_html_escaped(&cats, cname);
                sb_append_cstr(&cats, "</a>");
            }
            sql_finalize(&stmt);
        }
    }

    // Total pages + page rows.
    long long total = 0;
    {
        sql_stmt stmt = {0};
        if (sql_prepare(db, q_count[db->lang], &stmt)
            && sql_bind(&stmt, 1, SQL_SV(cat))
            && sql_bind(&stmt, 2, SQL_SV(cat))
            && sql_bind(&stmt, 3, SQL_SV(sv_from_cstr(pattern)))
            && sql_bind(&stmt, 4, SQL_SV(sv_from_cstr(pattern)))
            && sql_bind(&stmt, 5, SQL_SV(sv_from_cstr(pattern)))
            && sql_step(&stmt) == SQL_ROW) {
            total = sql_column_int64(&stmt, 0);
        }
        sql_finalize(&stmt);
    }
    int pages = (int) ((total + SHOP_PER_PAGE - 1) / SHOP_PER_PAGE);
    if (pages < 1) pages = 1;
    if (page > pages) { page = pages; offset = (page - 1) * SHOP_PER_PAGE; }

    Shop_Row rows[SHOP_PER_PAGE];
    size_t row_count = 0;
    {
        sql_stmt stmt = {0};
        if (sql_prepare(db, q_rows[db->lang], &stmt)
            && sql_bind(&stmt, 1, SQL_SV(cat))
            && sql_bind(&stmt, 2, SQL_SV(cat))
            && sql_bind(&stmt, 3, SQL_SV(sv_from_cstr(pattern)))
            && sql_bind(&stmt, 4, SQL_SV(sv_from_cstr(pattern)))
            && sql_bind(&stmt, 5, SQL_SV(sv_from_cstr(pattern)))
            && sql_bind(&stmt, 6, SQL_I(SHOP_PER_PAGE))
            && sql_bind(&stmt, 7, SQL_I(offset))) {
            while (sql_step(&stmt) == SQL_ROW
                   && row_count < SHOP_PER_PAGE) {
                Shop_Row *r = &rows[row_count];
                memset(r, 0, sizeof(*r));
                snprintf(r->id, sizeof(r->id), "%s",
                         sql_col_text(&stmt, 0) ? sql_col_text(&stmt, 0) : "");
                snprintf(r->name, sizeof(r->name), "%s",
                         sql_col_text(&stmt, 1) ? sql_col_text(&stmt, 1) : "");
                snprintf(r->sku, sizeof(r->sku), "%s",
                         sql_col_text(&stmt, 2) ? sql_col_text(&stmt, 2) : "");
                r->price = sql_col_double(&stmt, 3);
                snprintf(r->cat, sizeof(r->cat), "%s",
                         sql_col_text(&stmt, 4) ? sql_col_text(&stmt, 4) : "");
                r->stock = sql_col_double(&stmt, 5);
                ++row_count;
            }
        }
        sql_finalize(&stmt);
    }
    db_close(db);

    String_Builder content = {0};
    shop_chrome_start(&content, sc, "/");

    // Filter bar: chips + active query indicator.
    sb_append_cstr(&content,
        "<div class=\"flex items-center gap-1 flex-wrap\">");
    {
        bool on = cat.count == 0;
        sb_append_cstr(&content, "<a href=\"");
        sb_append_html_escaped(&content, shop_list_query(sv_from_cstr(""), q, 1));
        sb_append_cstr(&content, on
            ? "\" class=\"px-2 py-1.5 text-xs border border-slate-800"
              " bg-slate-900 text-white\">ទាំងអស់</a>"
            : "\" class=\"px-2 py-1.5 text-xs border border-slate-300"
              " bg-white text-slate-600 hover:border-slate-400\">ទាំងអស់</a>");
    }
    sb_append_buf(&content, cats.items, cats.count);
    if (q.count > 0) {
        sb_append_cstr(&content,
            "<span class=\"text-xs text-slate-500 px-1\">លទ្ធផលសម្រាប់ \"");
        sb_append_html_escaped(&content,
            temp_sprintf("%.*s", (int) q.count, q.data));
        sb_append_cstr(&content, "\"</span>");
    }
    sb_append_cstr(&content, "</div>");

    // Grid.
    if (row_count == 0) {
        sb_append_cstr(&content,
            "<div class=\"bg-white border border-slate-200 text-center"
            " px-3 py-8\"><div class=\"text-slate-500 text-sm\">"
            "មិនមានផលិតផលទេ</div>"
            "<a href=\"/\" class=\"text-xs text-indigo-600 hover:underline\">"
            "លុបតម្រង</a></div>");
    } else {
        sb_append_cstr(&content, "<div class=\"grid grid-cols-2 md:grid-cols-4"
                                 " gap-3\">");
        for (size_t i = 0; i < row_count; ++i) {
            Shop_Row *r = &rows[i];
            bool out = r->stock <= 0;
            sb_append_cstr(&content, "<div class=\"bg-white border"
                                     " border-slate-200 flex flex-col\">");
            sb_append_cstr(&content, "<a href=\"/product/");
            sb_append_html_escaped(&content, r->id);
            sb_append_cstr(&content,
                "\" class=\"bg-indigo-50 h-24 flex items-center justify-center"
                " text-indigo-300 font-bold text-3xl\">");
            sb_append_first_glyph(&content, r->name);
            sb_append_cstr(&content, "</a>"
                "<div class=\"px-2 py-2 flex flex-col gap-1 flex-1\">"
                "<a href=\"/product/");
            sb_append_html_escaped(&content, r->id);
            sb_append_cstr(&content,
                "\" class=\"text-sm font-medium text-slate-800"
                " hover:text-indigo-600 leading-snug\">");
            sb_append_html_escaped(&content, r->name);
            sb_append_cstr(&content, "</a>"
                "<div class=\"text-xs text-slate-400 truncate\">");
            sb_append_html_escaped(&content, r->sku);
            sb_append_cstr(&content, " · ");
            sb_append_html_escaped(&content, r->cat);
            sb_append_cstr(&content, "</div>"
                "<div class=\"text-sm font-semibold text-indigo-600\">");
            sb_append_cstr(&content, shop_money(r->price));
            sb_append_cstr(&content, "</div>"
                "<div class=\"text-xs ");
            sb_append_cstr(&content, out
                ? "text-red-500\">អស់ពីរាក់"
                : "text-emerald-600\">នៅសល់: ");
            if (!out) sb_appendf(&content, "%.0f", r->stock);
            sb_append_cstr(&content, "</div></div>"
                "<form method=\"POST\" action=\"/cart/add\" class=\"border-t"
                " border-slate-200 p-1\">"
                "<input type=\"hidden\" name=\"product_id\" value=\"");
            sb_append_html_escaped(&content, r->id);
            sb_append_cstr(&content,
                "\"><input type=\"hidden\" name=\"redirect\" value=\"/");
            sb_append_cstr(&content,
                "\"><button class=\"w-full bg-indigo-600 hover:bg-indigo-700"
                " text-white px-2 py-1.5 text-sm disabled:bg-slate-200"
                " disabled:text-slate-400\"");
            if (out) sb_append_cstr(&content, " disabled");
            sb_append_cstr(&content, ">ដាក់ក្នុងរទេះ</button></form></div>");
        }
        sb_append_cstr(&content, "</div>");
    }

    // Pager.
    if (pages > 1) {
        sb_append_cstr(&content,
            "<div class=\"flex items-center justify-center gap-2 text-sm\">");
        if (page > 1) {
            sb_append_cstr(&content, "<a href=\"");
            sb_append_html_escaped(&content,
                shop_list_query(cat, q, page - 1));
            sb_append_cstr(&content,
                "\" class=\"px-2 py-1.5 border border-slate-300 bg-white\">«</a>");
        }
        sb_appendf(&content,
                   "<span class=\"text-slate-500\">ទំព័រ %d/%d</span>",
                   page, pages);
        if (page < pages) {
            sb_append_cstr(&content, "<a href=\"");
            sb_append_html_escaped(&content,
                shop_list_query(cat, q, page + 1));
            sb_append_cstr(&content,
                "\" class=\"px-2 py-1.5 border border-slate-300 bg-white\">»</a>");
        }
        sb_append_cstr(&content, "</div>");
    }

    shop_chrome_end(&content);
    shop_render(sc, 200, "ផលិតផល", &content);
    sb_free(content);
    sb_free(cats);
}

// ---------------------------------------------------------------------------
// GET /product/<id> - detail with qty + Add to cart + Buy now
// ---------------------------------------------------------------------------

void serve_shop_product(Serve_Context *sc) {
    Route_Id id = sc->route_id;
    if (id.kind == ID_NONE) { serve_error(sc, 404); return; }

    db_t *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }

    static const char sql[] =
        "SELECT p.name, p.sku, p.base_price, c.name, "
        "COALESCE((SELECT SUM(s.quantity) FROM inventory_stocks s "
        "          WHERE s.product_id = p.id AND s.deleted_at IS NULL), 0) "
        "FROM products p JOIN categories c ON c.id = p.category_id "
        "WHERE p.id = ? AND p.deleted_at IS NULL;";
    char name[160] = {0}, sku[64] = {0}, cat[96] = {0};
    double price = 0, stock = 0;
    bool found = false;
    {
        sql_stmt stmt = {0};
        if (sql_prepare(db, sql, &stmt)
            && sql_bind(&stmt, 1, SQL_SV(id.raw))
            && sql_step(&stmt) == SQL_ROW) {
            snprintf(name, sizeof(name), "%s",
                     sql_col_text(&stmt, 0) ? sql_col_text(&stmt, 0) : "");
            snprintf(sku, sizeof(sku), "%s",
                     sql_col_text(&stmt, 1) ? sql_col_text(&stmt, 1) : "");
            price = sql_col_double(&stmt, 2);
            snprintf(cat, sizeof(cat), "%s",
                     sql_col_text(&stmt, 3) ? sql_col_text(&stmt, 3) : "");
            stock = sql_col_double(&stmt, 4);
            found = true;
        }
        sql_finalize(&stmt);
    }
    db_close(db);
    if (!found) { serve_error(sc, 404); return; }

    int stock_i = (int) stock;
    bool out = stock_i <= 0;

    String_Builder content = {0};
    shop_chrome_start(&content, sc, "/");
    sb_append_cstr(&content,
        "<div class=\"bg-white border border-slate-200 flex flex-col"
        " md:flex-row\">"
        "<div class=\"bg-indigo-50 md:w-64 h-40 flex items-center justify-center"
        " text-indigo-300 font-bold text-5xl\">");
    sb_append_first_glyph(&content, name);
    sb_append_cstr(&content,
        "</div>"
        "<div class=\"flex-1 px-3 py-3 flex flex-col gap-2\">"
        "<div class=\"text-xs text-slate-400\">SKU: ");
    sb_append_html_escaped(&content, sku);
    sb_append_cstr(&content, " · ");
    sb_append_html_escaped(&content, cat);
    sb_append_cstr(&content,
        "</div>"
        "<h1 class=\"text-xl font-semibold text-slate-900\">");
    sb_append_html_escaped(&content, name);
    sb_append_cstr(&content,
        "</h1>"
        "<div class=\"text-2xl font-bold text-indigo-600\">");
    sb_append_cstr(&content, shop_money(price));
    sb_append_cstr(&content,
        "</div>"
        "<div class=\"text-sm ");
    sb_append_cstr(&content, out ? "text-red-500\">អស់ពីរាក់"
                                 : "text-emerald-600\">នៅសល់: ");
    if (!out) sb_appendf(&content, "%d", stock_i);
    sb_append_cstr(&content, "</div>");

    if (out) {
        sb_append_cstr(&content,
            "<div class=\"bg-red-50 border-l-2 border-red-500 text-red-700"
            " px-2 py-1.5 text-xs\">ផលិតផលនេះអស់ពីរាក់</div>");
    } else {
        sb_append_cstr(&content,
            "<div class=\"flex items-center gap-2\">"
            "<form method=\"POST\" action=\"/cart/add\" class=\"flex items-center"
            " gap-2\">"
            "<input type=\"hidden\" name=\"product_id\" value=\"");
        sb_append_html_escaped(&content, temp_sprintf("%.*s", (int) id.raw.count,
                                                      id.raw.data));
        sb_append_cstr(&content,
            "\"><input type=\"hidden\" name=\"redirect\" value=\"/product/");
        sb_append_html_escaped(&content, temp_sprintf("%.*s", (int) id.raw.count,
                                                      id.raw.data));
        sb_append_cstr(&content,
            "\">"
            "<input type=\"number\" name=\"qty\" value=\"1\" min=\"1\" max=\"");
        sb_appendf(&content, "%d", stock_i);
        sb_append_cstr(&content,
            "\" class=\"border border-slate-300 px-2 py-1.5 text-sm w-20\">"
            "<button class=\"bg-slate-800 hover:bg-slate-900 text-white px-2"
            " py-1.5 text-sm\">ដាក់ក្នុងរទេះ</button>"
            "</form>"
            "<form method=\"POST\" action=\"/cart/buynow\""
            " class=\"flex items-center gap-2\">"
            "<input type=\"hidden\" name=\"product_id\" value=\"");
        sb_append_html_escaped(&content, temp_sprintf("%.*s", (int) id.raw.count,
                                                      id.raw.data));
        sb_append_cstr(&content,
            "\">"
            "<button class=\"bg-indigo-600 hover:bg-indigo-700 text-white px-2"
            " py-1.5 text-sm\">ទិញឥឡូវនេះ</button>"
            "</form>"
            "</div>");
    }
    sb_append_cstr(&content,
        "<a href=\"/\" class=\"text-xs text-indigo-600 hover:underline"
        " mt-1\">« ត្រឡប់ទៅបញ្ជីផលិតផល</a>"
        "</div></div>");
    shop_chrome_end(&content);
    shop_render(sc, 200, name, &content);
    sb_free(content);
}
