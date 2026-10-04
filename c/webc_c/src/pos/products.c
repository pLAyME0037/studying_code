#include "products.h"

#include <string.h>

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "core/http/utils.h"

// =========================================================================
// /pos/products: name + codes cell + prices cell + category/type FKs.
// Two child tabs: product_variants and inventory_stocks.
// =========================================================================

static const char *pr_sku_parts[]  = { "sku", "barcode" };
static const char *pr_sku_labels[] = { "SKU", "Barcode" };
static const MD_Cell pr_sku_cell = {
    .parts       = pr_sku_parts,
    .part_labels = pr_sku_labels,
    .part_count  = 2,
    .style       = "stack",
};

static const char *pr_price_parts[]  = { "base_price", "cost_price", "tax_rate" };
static const char *pr_price_labels[] = { "Base", "Cost", "Tax %" };
static const MD_Cell pr_price_cell = {
    .parts       = pr_price_parts,
    .part_labels = pr_price_labels,
    .part_count  = 3,
    .style       = "stack",
};

MD_Column md_products_columns[] = {
    { .name = "name", .label = "Product", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "sku", .label = "Codes", .type = COL_TYPE_TEXT,
      .nullable = false, .cell = &pr_sku_cell },
    { .name = "base_price", .label = "Prices", .type = COL_TYPE_NUM,
      .nullable = false, .cell = &pr_price_cell },
    { .name = "category_id", .label = "Category", .type = COL_TYPE_FK_SELECT,
      .nullable = false, .fk_table = "categories", .fk_label = "name" },
    { .name = "product_type_dict_id", .label = "Type",
      .type = COL_TYPE_FK_SELECT, .nullable = true,
      .fk_table = "dictionaries", .fk_label = "label",
      .fk_where = "category = 'PRODUCT_TYPE'" },
};
const size_t md_products_columns_count = ARRAY_LEN(md_products_columns);

// Products as a child tab under /pos/categories: category_id is the
// fk_column (query string), so it is not a column here.
MD_Column md_products_child_columns[] = {
    { .name = "name", .label = "Product", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "sku", .label = "Codes", .type = COL_TYPE_TEXT,
      .nullable = false, .cell = &pr_sku_cell },
    { .name = "base_price", .label = "Prices", .type = COL_TYPE_NUM,
      .nullable = false, .cell = &pr_price_cell },
};
const size_t md_products_child_columns_count = ARRAY_LEN(md_products_child_columns);

// Child tab shapes (view-local): variants and stock rows under a product.
static MD_Column md_pos_variant_columns[] = {
    { .name = "variant_name", .label = "Variant", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "sku", .label = "SKU", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "price_delta", .label = "\xCE\x94 Price", .type = COL_TYPE_NUM,
      .nullable = false },
    { .name = "attributes", .label = "Attributes", .type = COL_TYPE_TEXT,
      .nullable = true },
};
static const size_t md_pos_variant_columns_count = ARRAY_LEN(md_pos_variant_columns);

static MD_Column md_pos_stock_child_columns[] = {
    { .name = "org_unit_id", .label = "Org", .type = COL_TYPE_FK_SELECT,
      .nullable = false, .fk_table = "org_units", .fk_label = "ou_name" },
    { .name = "variant_id", .label = "Variant", .type = COL_TYPE_FK_SELECT,
      .nullable = true, .fk_table = "product_variants",
      .fk_label = "variant_name" },
    { .name = "quantity", .label = "Qty", .type = COL_TYPE_NUM,
      .nullable = false },
};
static const size_t md_pos_stock_child_columns_count = ARRAY_LEN(md_pos_stock_child_columns);

// DB mutations only -- list loading lives in the master_child engine.
// One handler serves both views of products (master + child under a
// category): fields = body keys both forms carry, everything else is an
// opt (body first, then the child form's ?category_id= query).
static bool create_product(db_t *db, String_View *fields, size_t count) {
    if (count < 8) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO products "
                         "(name, sku, barcode, base_price, cost_price, "
                          "tax_rate, category_id, product_type_dict_id) "
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO products "
                         "(name, sku, barcode, base_price, cost_price, "
                          "tax_rate, category_id, product_type_dict_id) "
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO products "
                         "(name, sku, barcode, base_price, cost_price, "
                          "tax_rate, category_id, product_type_dict_id) "
                         "VALUES ($1, $2, $3, $4, $5, $6, $7, $8);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(fields[0])))  return_defer(false);  // name
    if (!sql_bind(&stmt, 2, SQL_SV(fields[1])))  return_defer(false);  // sku
    if (!sql_bind(&stmt, 3, pos_sv(fields[2])))  return_defer(false);  // barcode
    if (!sql_bind(&stmt, 4, pos_num(fields[3]))) return_defer(false);  // base_price
    if (!sql_bind(&stmt, 5, pos_num(fields[4]))) return_defer(false);  // cost_price
    if (!sql_bind(&stmt, 6, pos_num(fields[5]))) return_defer(false);  // tax_rate
    if (!sql_bind(&stmt, 7, pos_sv(fields[6])))  return_defer(false);  // category_id
    if (!sql_bind(&stmt, 8, pos_sv(fields[7])))  return_defer(false);  // type dict
    if (!sql_final_step(&stmt))                  return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool update_product(db_t *db, String_View *fields, size_t count,
                           String_View id)
{
    if (count < 8) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE products SET "
                         "name = COALESCE(NULLIF(?, ''), name), "
                         "sku = COALESCE(NULLIF(?, ''), sku), "
                         "barcode = COALESCE(NULLIF(?, ''), barcode), "
                         "base_price = COALESCE(NULLIF(?, ''), base_price), "
                         "cost_price = COALESCE(NULLIF(?, ''), cost_price), "
                         "tax_rate = COALESCE(NULLIF(?, ''), tax_rate), "
                         "category_id = COALESCE(NULLIF(?, ''), category_id), "
                         "product_type_dict_id = COALESCE(NULLIF(?, ''), "
                         "product_type_dict_id) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE products SET "
                         "name = COALESCE(NULLIF(?, ''), name), "
                         "sku = COALESCE(NULLIF(?, ''), sku), "
                         "barcode = COALESCE(NULLIF(?, ''), barcode), "
                         "base_price = COALESCE(NULLIF(?, ''), base_price), "
                         "cost_price = COALESCE(NULLIF(?, ''), cost_price), "
                         "tax_rate = COALESCE(NULLIF(?, ''), tax_rate), "
                         "category_id = COALESCE(NULLIF(?, ''), category_id), "
                         "product_type_dict_id = COALESCE(NULLIF(?, ''), "
                         "product_type_dict_id) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE products SET "
                         "name = COALESCE(NULLIF($1, ''), name), "
                         "sku = COALESCE(NULLIF($2, ''), sku), "
                         "barcode = COALESCE(NULLIF($3, ''), barcode), "
                         "base_price = COALESCE(NULLIF($4, ''), base_price), "
                         "cost_price = COALESCE(NULLIF($5, ''), cost_price), "
                         "tax_rate = COALESCE(NULLIF($6, ''), tax_rate), "
                         "category_id = COALESCE(NULLIF($7, ''), category_id), "
                         "product_type_dict_id = COALESCE(NULLIF($8, ''), "
                         "product_type_dict_id) "
                         "WHERE id = $9;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    for (int i = 1; i <= 8; ++i) {
        if (!sql_bind(&stmt, i, SQL_SV(fields[i - 1]))) return_defer(false);
    }
    if (!sql_bind(&stmt, 9, SQL_SV(id)))  return_defer(false);
    if (!sql_final_step(&stmt))           return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool soft_delete_product(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE products "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE products SET deleted_at = NOW() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE products SET deleted_at = now() WHERE id = $1;",
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

static bool restore_product(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE products SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE products SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE products SET deleted_at = NULL WHERE id = $1;",
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

void serve_pos_products(Serve_Context *sc) {
    MD_ChildTab children[] = {
        {
            .table        = "product_variants",
            .title        = "Variants",
            .fk_column    = "product_id",
            .id_column    = "id",
            .crud_path    = "/pos/variants",
            .columns      = md_pos_variant_columns,
            .column_count = md_pos_variant_columns_count,
            .soft_delete  = 1,
        },
        {
            .table        = "inventory_stocks",
            .title        = "Stock",
            .fk_column    = "product_id",
            .id_column    = "id",
            .crud_path    = "/pos/stocks",
            .columns      = md_pos_stock_child_columns,
            .column_count = md_pos_stock_child_columns_count,
            .soft_delete  = 1,
        },
    };
    MD_MasterConfig config = {
        .table          = "products",
        .title          = "Products",
        .id_column      = "id",
        .crud_path      = "/pos/products",
        .columns        = md_products_columns,
        .column_count   = md_products_columns_count,
        .children       = children,
        .children_count = ARRAY_LEN(children),
        .soft_delete    = 1,
    };
    serve_master_child(sc, &config);
}

static const char *prd_fields[] = { "name", "sku" };
static const char *prd_opt_fields[] = {
    "barcode", "base_price", "cost_price", "tax_rate",
    "category_id", "product_type_dict_id",
};
SERVE_CREATE(pos_products, product, prd_fields, prd_opt_fields)
SERVE_UPDATE(pos_products, product, prd_fields, prd_opt_fields)
SERVE_SOFT_DELETE(pos_products, product)
