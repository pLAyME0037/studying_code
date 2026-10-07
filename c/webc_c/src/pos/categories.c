#include "categories.h"

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "src/pos/products.h"   // child tab columns (products under a category)
#include "core/http/utils.h"

// =========================================================================
// /pos/categories: code + name + self-FK parent, products as child tab.
// =========================================================================

MD_Column md_categories_columns[] = {
    { .name = "cat_code",   .label = "Code",   .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "name",       .label = "Name",   .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "parent_id",  .label = "Parent", .type = COL_TYPE_FK_SELECT,
      .nullable = true, .fk_table = "categories", .fk_label = "name" },
    { .name = "created_at", .label = "Created", .type = COL_TYPE_DATE,
      .nullable = false, .computed = 1 },
};
const size_t md_categories_columns_count = ARRAY_LEN(md_categories_columns);

// DB mutations only -- list loading lives in the master_child engine.
static bool create_category(db_t *db, String_View *fields, size_t count) {
    if (count < 3) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO categories "
                         "(cat_code, name, parent_id) VALUES (?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO categories "
                         "(cat_code, name, parent_id) VALUES (?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO categories "
                         "(cat_code, name, parent_id) VALUES ($1, $2, $3);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(fields[0]))) return_defer(false);
    if (!sql_bind(&stmt, 2, SQL_SV(fields[1]))) return_defer(false);
    if (!sql_bind(&stmt, 3, pos_sv(fields[2]))) return_defer(false);
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

// COALESCE(NULLIF(?,''), col): empty keeps the stored value, so a form
// that does not carry a column (child edits) cannot blank it.
static bool update_category(db_t *db, String_View *fields, size_t count,
                            String_View id)
{
    if (count < 3) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE categories SET "
                         "cat_code = COALESCE(NULLIF(?, ''), cat_code), "
                         "name = COALESCE(NULLIF(?, ''), name), "
                         "parent_id = COALESCE(NULLIF(?, ''), parent_id) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE categories SET "
                         "cat_code = COALESCE(NULLIF(?, ''), cat_code), "
                         "name = COALESCE(NULLIF(?, ''), name), "
                         "parent_id = COALESCE(NULLIF(?, ''), parent_id) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE categories SET "
                         "cat_code = COALESCE(NULLIF($1, ''), cat_code), "
                         "name = COALESCE(NULLIF($2, ''), name), "
                         "parent_id = COALESCE(NULLIF($3, ''), parent_id) "
                         "WHERE id = $4;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    for (int i = 1; i <= 3; ++i) {
        if (!sql_bind(&stmt, i, SQL_SV(fields[i - 1]))) return_defer(false);
    }
    if (!sql_bind(&stmt, 4, SQL_SV(id))) return_defer(false);
    if (!sql_final_step(&stmt))          return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

// Soft delete: the row stays, deleted_at is stamped and the live list
// filters it out; /restore clears the column.
static bool soft_delete_category(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE categories "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE categories SET deleted_at = UTC_TIMESTAMP() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE categories SET deleted_at = now() WHERE id = $1;",
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

static bool restore_category(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE categories SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE categories SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE categories SET deleted_at = NULL WHERE id = $1;",
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

void serve_pos_categories(Serve_Context *sc) {
    // Products under a category: fk = category_id arrives in the query
    // string of the child create form (handlers in products.c).
    MD_ChildTab children[] = {
        {
            .table        = "products",
            .title        = "Products",
            .fk_column    = "category_id",
            .id_column    = "id",
            .crud_path    = "/pos/products",
            .columns      = md_products_child_columns,
            .column_count = md_products_child_columns_count,
            .soft_delete  = 1,
        },
    };
    MD_MasterConfig config = {
        .table          = "categories",
        .title          = "Categories",
        .id_column      = "id",
        .crud_path      = "/pos/categories",
        .columns        = md_categories_columns,
        .column_count   = md_categories_columns_count,
        .children       = children,
        .children_count = ARRAY_LEN(children),
        .soft_delete    = 1,
    };
    serve_master_child(sc, &config);
}

// The SERVE_* macros require an opt array; parent_id is read from the
// body (the only form that writes categories) or the query string.
static const char *cat_fields[] = { "cat_code", "name" };
static const char *cat_opt_fields[] = { "parent_id" };
SERVE_CREATE(pos_categories, category, cat_fields, cat_opt_fields)
SERVE_UPDATE(pos_categories, category, cat_fields, cat_opt_fields)
SERVE_SOFT_DELETE(pos_categories, category)
