#include "shop.h"

#include <stdlib.h>
#include <string.h>

#include "src/auth/auth.h"
#include "src/http/utils.h"
#include "src/i18n/i18n.h"
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

// The storefront chrome markup lives in display/shop/chrome_start.h.tt
// (design rule: state-driven HTML belongs to a template); this host feeds it
// the language-form return path and the session state.
void shop_chrome_start(String_Builder *sb, Serve_Context *sc,
                       const char *active) {
    (void) active;  // nav has no active states beyond the home link
    // Return path for the language form: the URI exactly as served.
    const char *back = sc->query_string.count
        ? temp_sprintf("%.*s?%.*s", (int) sc->uri.count, sc->uri.data,
                       (int) sc->query_string.count, sc->query_string.data)
        : temp_sprintf("%.*s", (int) sc->uri.count, sc->uri.data);
    // Phase 16 signup: anonymous = admin entry + sign in + sign up;
    // signed-in = identity + logout, and only ADMIN keeps the staff
    // dashboard link (CUSTOMER sessions belong on the storefront).
    const Auth_User *au = auth_current_user();
    bool signed_in = au && au->id;
    bool is_admin = signed_in && au->user_type
        && strcmp(au->user_type, "ADMIN") == 0;
#define OUT(buf, size) sb_append_buf(sb, buf, size);
#define STR(x) sb_append_cstr(sb, (x) ? (x) : "");
#define ESCAPED(x) sb_append_html_escaped(sb, (x));
#include "build/h_to_html/shop/chrome_start.h"
#undef OUT
#undef STR
#undef ESCAPED
}

void shop_chrome_end(String_Builder *sb) {
#define OUT(buf, size) sb_append_buf(sb, buf, size);
#define ESCAPED(x) sb_append_html_escaped(sb, (x));
#include "build/h_to_html/shop/chrome_end.h"
#undef OUT
#undef ESCAPED
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

// Category chip (data only; markup lives in display/shop/index.h.tt).
typedef struct {
    char   id[40];
    char   name[160];
    bool   on;
} Shop_Chip;

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

    // Category chips (data only; the markup lives in the template).
    struct { Shop_Chip *items; size_t count, capacity; } chips = {0};
    {
        sql_stmt stmt = {0};
        if (sql_prepare(db, cat_sql, &stmt)) {
            while (sql_step(&stmt) == SQL_ROW) {
                const char *cid = sql_col_text(&stmt, 0);
                const char *cname = sql_col_text(&stmt, 1);
                if (!cid || !cname) continue;
                Shop_Chip ch = {0};
                snprintf(ch.id, sizeof(ch.id), "%s", cid);
                snprintf(ch.name, sizeof(ch.name), "%s", cname);
                ch.on = cat.count == strlen(cid)
                    && memcmp(cat.data, cid, cat.count) == 0;
                da_append(&chips, ch);
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

    // Filter bar, product grid and pager: display/shop/index.h.tt (design
    // rule: state-driven HTML belongs to a template).
#define OUT(buf, size) sb_append_buf(&content, buf, size);
#define STR(x) sb_append_cstr(&content, (x) ? (x) : "");
#define ESCAPED(x) sb_append_html_escaped(&content, (x));
#include "build/h_to_html/shop/index.h"
#undef OUT
#undef STR
#undef ESCAPED

    shop_chrome_end(&content);
    shop_render(sc, 200, tr("shop.title_products", "ផលិតផល"), &content);
    sb_free(content);
    da_free(chips);
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
    const char *id_s = temp_sprintf("%.*s", (int) id.raw.count, id.raw.data);

    String_Builder content = {0};
    shop_chrome_start(&content, sc, "/");

    // Detail panel: display/shop/product.h.tt (design rule: state-driven
    // HTML belongs to a template).
#define OUT(buf, size) sb_append_buf(&content, buf, size);
#define STR(x) sb_append_cstr(&content, (x) ? (x) : "");
#define ESCAPED(x) sb_append_html_escaped(&content, (x));
#include "build/h_to_html/shop/product.h"
#undef OUT
#undef STR
#undef ESCAPED

    shop_chrome_end(&content);
    shop_render(sc, 200, name, &content);
    sb_free(content);
}
