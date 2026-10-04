#include "stocks.h"

#include <string.h>

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "core/http/utils.h"

// =========================================================================
// /pos/stocks: inventory master (product/org/variant FKs + qty + range
// cell) with stock_ledger as the child tab. Rows are also created as a
// child of /pos/products (product_id rides the query string there).
// =========================================================================

static const char *stk_minmax_parts[]  = { "min_threshold", "max_threshold" };
static const char *stk_minmax_labels[] = { "Min", "Max" };
static const MD_Cell stk_minmax_cell = {
    .parts       = stk_minmax_parts,
    .part_labels = stk_minmax_labels,
    .part_count  = 2,
    .style       = "stack",
};

// Child tab shape for stock_ledger (view-local): a movement log entry.
static MD_Column md_pos_ledger_columns[] = {
    { .name = "reference_type", .label = "Type", .type = COL_TYPE_TEXT,
      .nullable = false },   // ORDER | PURCHASE | ADJUST | RETURN
    { .name = "reference_id", .label = "Ref", .type = COL_TYPE_TEXT,
      .nullable = true },
    { .name = "quantity_change", .label = "Change", .type = COL_TYPE_NUM,
      .nullable = false },
    { .name = "balance_after", .label = "Balance", .type = COL_TYPE_NUM,
      .nullable = false },
};
static const size_t md_pos_ledger_columns_count = ARRAY_LEN(md_pos_ledger_columns);

static MD_Column md_stocks_columns[] = {
    { .name = "product_id", .label = "Product", .type = COL_TYPE_FK_SELECT,
      .nullable = false, .fk_table = "products", .fk_label = "name" },
    { .name = "org_unit_id", .label = "Org", .type = COL_TYPE_FK_SELECT,
      .nullable = false, .fk_table = "org_units", .fk_label = "ou_name" },
    { .name = "variant_id", .label = "Variant", .type = COL_TYPE_FK_SELECT,
      .nullable = true, .fk_table = "product_variants",
      .fk_label = "variant_name" },
    { .name = "quantity", .label = "Qty", .type = COL_TYPE_NUM,
      .nullable = false },
    { .name = "min_threshold", .label = "Range", .type = COL_TYPE_NUM,
      .nullable = false, .cell = &stk_minmax_cell },
};
static const size_t md_stocks_columns_count = ARRAY_LEN(md_stocks_columns);


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

// product_id IS updated here (master edit may move a stock to another
// product); the products-page child edit form does not carry it, so
// COALESCE keeps the stored fk.
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
                         "max_threshold = COALESCE(NULLIF(?, ''), max_threshold), "
                         "product_id = COALESCE(NULLIF(?, ''), product_id) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE inventory_stocks SET "
                         "org_unit_id = COALESCE(NULLIF(?, ''), org_unit_id), "
                         "variant_id = COALESCE(NULLIF(?, ''), variant_id), "
                         "quantity = COALESCE(NULLIF(?, ''), quantity), "
                         "min_threshold = COALESCE(NULLIF(?, ''), min_threshold), "
                         "max_threshold = COALESCE(NULLIF(?, ''), max_threshold), "
                         "product_id = COALESCE(NULLIF(?, ''), product_id) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE inventory_stocks SET "
                         "org_unit_id = COALESCE(NULLIF($1, ''), org_unit_id), "
                         "variant_id = COALESCE(NULLIF($2, ''), variant_id), "
                         "quantity = COALESCE(NULLIF($3, ''), quantity), "
                         "min_threshold = COALESCE(NULLIF($4, ''), min_threshold), "
                         "max_threshold = COALESCE(NULLIF($5, ''), max_threshold), "
                         "product_id = COALESCE(NULLIF($6, ''), product_id) "
                         "WHERE id = $7;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    for (int i = 1; i <= 6; ++i) {
        if (!sql_bind(&stmt, i, SQL_SV(fields[i - 1]))) return_defer(false);
    }
    if (!sql_bind(&stmt, 7, SQL_SV(id)))  return_defer(false);
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

void serve_pos_stocks(Serve_Context *sc) {
    MD_ChildTab children[] = {
        {
            .table        = "stock_ledger",
            .title        = "Ledger",
            .fk_column    = "stock_id",
            .id_column    = "id",
            .crud_path    = "/pos/ledger",
            .columns      = md_pos_ledger_columns,
            .column_count = md_pos_ledger_columns_count,
            .soft_delete  = 1,
        },
    };
    MD_MasterConfig config = {
        .table          = "inventory_stocks",
        .title          = "Stock",
        .id_column      = "id",
        .crud_path      = "/pos/stocks",
        .columns        = md_stocks_columns,
        .column_count   = md_stocks_columns_count,
        .children       = children,
        .children_count = ARRAY_LEN(children),
        .soft_delete    = 1,
    };
    serve_master_child(sc, &config);
}

static const char *stk_fields[] = { "org_unit_id", "variant_id", "quantity" };
static const char *stk_opt_fields[] = {
    "min_threshold", "max_threshold", "product_id",
};
SERVE_CREATE(pos_stocks, pos_stock, stk_fields, stk_opt_fields)
SERVE_UPDATE(pos_stocks, pos_stock, stk_fields, stk_opt_fields)
SERVE_SOFT_DELETE(pos_stocks, pos_stock)
