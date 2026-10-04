#include "variants.h"

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "core/http/utils.h"

// =========================================================================
// product_variants rows: created/edited from the /pos/products child tab
// (product_id rides the form's query string); soft-deletable like every
// POS entity.
// =========================================================================

static bool create_pos_variant(db_t *db, String_View *fields, size_t count) {
    if (count < 5) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO product_variants "
                         "(variant_name, sku, price_delta, attributes, "
                          "product_id) VALUES (?, ?, ?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO product_variants "
                         "(variant_name, sku, price_delta, attributes, "
                          "product_id) VALUES (?, ?, ?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO product_variants "
                         "(variant_name, sku, price_delta, attributes, "
                          "product_id) VALUES ($1, $2, $3, $4, $5);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(fields[0])))  return_defer(false);  // name
    if (!sql_bind(&stmt, 2, SQL_SV(fields[1])))  return_defer(false);  // sku
    if (!sql_bind(&stmt, 3, pos_num(fields[2]))) return_defer(false);  // delta
    if (!sql_bind(&stmt, 4, pos_sv(fields[3])))  return_defer(false);  // attrs
    if (!sql_bind(&stmt, 5, pos_sv(fields[4])))  return_defer(false);  // product
    if (!sql_final_step(&stmt))                  return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

// product_id is intentionally not UPDATEd: the fk never changes, and a
// master-side edit of the product must not touch it either.
static bool update_pos_variant(db_t *db, String_View *fields, size_t count,
                               String_View id)
{
    if (count < 4) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE product_variants SET "
                         "variant_name = COALESCE(NULLIF(?, ''), variant_name), "
                         "sku = COALESCE(NULLIF(?, ''), sku), "
                         "price_delta = COALESCE(NULLIF(?, ''), price_delta), "
                         "attributes = COALESCE(NULLIF(?, ''), attributes) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE product_variants SET "
                         "variant_name = COALESCE(NULLIF(?, ''), variant_name), "
                         "sku = COALESCE(NULLIF(?, ''), sku), "
                         "price_delta = COALESCE(NULLIF(?, ''), price_delta), "
                         "attributes = COALESCE(NULLIF(?, ''), attributes) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE product_variants SET "
                         "variant_name = COALESCE(NULLIF($1, ''), variant_name), "
                         "sku = COALESCE(NULLIF($2, ''), sku), "
                         "price_delta = COALESCE(NULLIF($3, ''), price_delta), "
                         "attributes = COALESCE(NULLIF($4, ''), attributes) "
                         "WHERE id = $5;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    for (int i = 1; i <= 4; ++i) {
        if (!sql_bind(&stmt, i, SQL_SV(fields[i - 1]))) return_defer(false);
    }
    if (!sql_bind(&stmt, 5, SQL_SV(id))) return_defer(false);
    if (!sql_final_step(&stmt))          return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool soft_delete_pos_variant(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE product_variants "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE product_variants "
                         "SET deleted_at = NOW() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE product_variants "
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

static bool restore_pos_variant(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE product_variants "
                         "SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE product_variants "
                         "SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE product_variants "
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

static const char *var_fields[] = { "variant_name", "sku" };
static const char *var_opt_fields[] = { "price_delta", "attributes", "product_id" };
SERVE_CREATE(pos_variants, pos_variant, var_fields, var_opt_fields)
SERVE_UPDATE(pos_variants, pos_variant, var_fields, var_opt_fields)
SERVE_SOFT_DELETE(pos_variants, pos_variant)
