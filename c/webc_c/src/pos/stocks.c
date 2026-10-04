#include "stocks.h"

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "core/http/utils.h"

// =========================================================================
// inventory_stocks rows: created/edited from the /pos/products Stock tab
// (product_id rides the form's query string). Thresholds are optional
// (form-less child rows default them to 0). Soft delete like every POS
// entity; org_unit_id stays inside the UNIQUE(org_unit, product, variant)
// triple, so an edit may move it (SQLite FK RESTRICT is satisfied).
// =========================================================================

static bool create_pos_stock(db_t *db, String_View *fields, size_t count) {
    if (count < 6) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO inventory_stocks "
                         "(org_unit_id, variant_id, quantity, min_threshold, "
                          "max_threshold, product_id) VALUES (?, ?, ?, ?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO inventory_stocks "
                         "(org_unit_id, variant_id, quantity, min_threshold, "
                          "max_threshold, product_id) VALUES (?, ?, ?, ?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO inventory_stocks "
                         "(org_unit_id, variant_id, quantity, min_threshold, "
                          "max_threshold, product_id) VALUES ($1, $2, $3, $4, $5, $6);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(fields[0])))  return_defer(false);   // org
    if (!sql_bind(&stmt, 2, pos_sv(fields[1])))  return_defer(false);   // variant
    if (!sql_bind(&stmt, 3, pos_num(fields[2]))) return_defer(false);   // qty
    if (!sql_bind(&stmt, 4, pos_num(fields[3]))) return_defer(false);   // min
    if (!sql_bind(&stmt, 5, pos_num(fields[4]))) return_defer(false);   // max
    if (!sql_bind(&stmt, 6, pos_sv(fields[5])))  return_defer(false);   // product
    if (!sql_final_step(&stmt))                  return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

// product_id is intentionally not UPDATEd (see variants).
static bool update_pos_stock(db_t *db, String_View *fields, size_t count,
                             String_View id)
{
    if (count < 6) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE inventory_stocks SET "
                         "org_unit_id = COALESCE(NULLIF(?, ''), org_unit_id), "
                         "variant_id = COALESCE(NULLIF(?, ''), variant_id), "
                         "quantity = COALESCE(NULLIF(?, ''), quantity), "
                         "min_threshold = COALESCE(NULLIF(?, ''), min_threshold), "
                         "max_threshold = COALESCE(NULLIF(?, ''), max_threshold) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE inventory_stocks SET "
                         "org_unit_id = COALESCE(NULLIF(?, ''), org_unit_id), "
                         "variant_id = COALESCE(NULLIF(?, ''), variant_id), "
                         "quantity = COALESCE(NULLIF(?, ''), quantity), "
                         "min_threshold = COALESCE(NULLIF(?, ''), min_threshold), "
                         "max_threshold = COALESCE(NULLIF(?, ''), max_threshold) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE inventory_stocks SET "
                         "org_unit_id = COALESCE(NULLIF($1, ''), org_unit_id), "
                         "variant_id = COALESCE(NULLIF($2, ''), variant_id), "
                         "quantity = COALESCE(NULLIF($3, ''), quantity), "
                         "min_threshold = COALESCE(NULLIF($4, ''), min_threshold), "
                         "max_threshold = COALESCE(NULLIF($5, ''), max_threshold) "
                         "WHERE id = $6;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    for (int i = 1; i <= 5; ++i) {
        if (!sql_bind(&stmt, i, SQL_SV(fields[i - 1]))) return_defer(false);
    }
    if (!sql_bind(&stmt, 6, SQL_SV(id)))  return_defer(false);
    if (!sql_final_step(&stmt))           return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool soft_delete_pos_stock(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE inventory_stocks "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE inventory_stocks "
                         "SET deleted_at = NOW() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE inventory_stocks "
                         "SET deleted_at = now() WHERE id = $1;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(id)))      return_defer(false);
    if (!sql_final_step(&stmt))               return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool restore_pos_stock(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE inventory_stocks "
                         "SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE inventory_stocks "
                         "SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE inventory_stocks "
                         "SET deleted_at = NULL WHERE id = $1;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(id)))      return_defer(false);
    if (!sql_final_step(&stmt))               return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static const char *stk_fields[] = { "org_unit_id", "variant_id", "quantity" };
static const char *stk_opt_fields[] = {
    "min_threshold", "max_threshold", "product_id",
};
SERVE_CREATE(pos_stocks, pos_stock, stk_fields, stk_opt_fields)
SERVE_UPDATE(pos_stocks, pos_stock, stk_fields, stk_opt_fields)
SERVE_SOFT_DELETE(pos_stocks, pos_stock)
