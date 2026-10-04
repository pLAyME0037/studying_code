#include "order_items.h"

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "core/http/utils.h"

// =========================================================================
// order_items rows: created/edited from the /pos/orders Items child tab
// (order_id rides the form's query string). unit_cost / discount_amount /
// tax_amount are not form columns (business-computed; default 0 here).
// =========================================================================

static bool create_order_item(db_t *db, String_View *fields, size_t count) {
    if (count < 9) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO order_items "
                         "(order_id, product_id, variant_id, unit_price, "
                          "unit_cost, quantity, discount_amount, tax_amount, "
                          "total_line) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO order_items "
                         "(order_id, product_id, variant_id, unit_price, "
                          "unit_cost, quantity, discount_amount, tax_amount, "
                          "total_line) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO order_items "
                         "(order_id, product_id, variant_id, unit_price, "
                          "unit_cost, quantity, discount_amount, tax_amount, "
                          "total_line) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, pos_sv(fields[8])))   return_defer(false);  // order
    if (!sql_bind(&stmt, 2, SQL_SV(fields[0])))   return_defer(false);  // product
    if (!sql_bind(&stmt, 3, pos_sv(fields[1])))   return_defer(false);  // variant
    if (!sql_bind(&stmt, 4, pos_num(fields[2])))  return_defer(false);  // unit price
    if (!sql_bind(&stmt, 5, pos_num(fields[5])))  return_defer(false);  // unit cost (opt)
    if (!sql_bind(&stmt, 6, pos_num(fields[3])))  return_defer(false);  // quantity
    if (!sql_bind(&stmt, 7, pos_num(fields[6])))  return_defer(false);  // discount (opt)
    if (!sql_bind(&stmt, 8, pos_num(fields[7])))  return_defer(false);  // tax (opt)
    if (!sql_bind(&stmt, 9, pos_num(fields[4])))  return_defer(false);  // total
    if (!sql_final_step(&stmt))                   return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

// unit_cost/discount_amount/tax_amount are not form columns -> skipped
// (they keep their stored values).
static bool update_order_item(db_t *db, String_View *fields, size_t count,
                              String_View id)
{
    if (count < 9) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE order_items SET "
                         "product_id = COALESCE(NULLIF(?, ''), product_id), "
                         "variant_id = COALESCE(NULLIF(?, ''), variant_id), "
                         "unit_price = COALESCE(NULLIF(?, ''), unit_price), "
                         "quantity = COALESCE(NULLIF(?, ''), quantity), "
                         "total_line = COALESCE(NULLIF(?, ''), total_line) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE order_items SET "
                         "product_id = COALESCE(NULLIF(?, ''), product_id), "
                         "variant_id = COALESCE(NULLIF(?, ''), variant_id), "
                         "unit_price = COALESCE(NULLIF(?, ''), unit_price), "
                         "quantity = COALESCE(NULLIF(?, ''), quantity), "
                         "total_line = COALESCE(NULLIF(?, ''), total_line) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE order_items SET "
                         "product_id = COALESCE(NULLIF($1, ''), product_id), "
                         "variant_id = COALESCE(NULLIF($2, ''), variant_id), "
                         "unit_price = COALESCE(NULLIF($3, ''), unit_price), "
                         "quantity = COALESCE(NULLIF($4, ''), quantity), "
                         "total_line = COALESCE(NULLIF($5, ''), total_line) "
                         "WHERE id = $6;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(fields[0]))) return_defer(false);
    if (!sql_bind(&stmt, 2, pos_sv(fields[1]))) return_defer(false);
    if (!sql_bind(&stmt, 3, SQL_SV(fields[2]))) return_defer(false);
    if (!sql_bind(&stmt, 4, SQL_SV(fields[3]))) return_defer(false);
    if (!sql_bind(&stmt, 5, SQL_SV(fields[4]))) return_defer(false);
    if (!sql_bind(&stmt, 6, SQL_SV(id)))        return_defer(false);
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool soft_delete_order_item(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE order_items "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE order_items SET deleted_at = NOW() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE order_items SET deleted_at = now() WHERE id = $1;",
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

static bool restore_order_item(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE order_items SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE order_items SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE order_items SET deleted_at = NULL WHERE id = $1;",
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

static const char *oi_fields[] = {
    "product_id", "variant_id", "unit_price", "quantity", "total_line",
};
static const char *oi_opt_fields[] = {
    "unit_cost", "discount_amount", "tax_amount", "order_id",
};
SERVE_CREATE(pos_order_items, order_item, oi_fields, oi_opt_fields)
SERVE_UPDATE(pos_order_items, order_item, oi_fields, oi_opt_fields)
SERVE_SOFT_DELETE(pos_order_items, order_item)
