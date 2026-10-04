#include "permissions.h"

#include <string.h>

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "core/http/utils.h"

// =========================================================================
// /pos/permissions: permission catalogue, master-only. created_at is the
// real-but-unbound opt slot (SERVE_* needs a non-empty opt array under
// -pedantic); it rides its column default like everywhere else.
// =========================================================================

MD_Column md_permissions_columns[] = {
    { .name = "perm_code", .label = "Code", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "perm_name", .label = "Name", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "module_name", .label = "Module", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "created_at", .label = "Since", .type = COL_TYPE_DATE,
      .nullable = false },
};
const size_t md_permissions_columns_count = ARRAY_LEN(md_permissions_columns);

// DB mutations only -- list loading lives in the master_child engine.
static bool create_permission(db_t *db, String_View *fields, size_t count) {
    if (count < 4) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO permissions (perm_code, perm_name, "
                         "module_name) VALUES (?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO permissions (perm_code, perm_name, "
                         "module_name) VALUES (?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO permissions (perm_code, perm_name, "
                         "module_name) VALUES ($1, $2, $3);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    for (int i = 1; i <= 3; ++i) {
        if (!sql_bind(&stmt, i, SQL_SV(fields[i - 1]))) return_defer(false);
    }
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool update_permission(db_t *db, String_View *fields, size_t count,
                              String_View id)
{
    if (count < 4) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE permissions SET "
                         "perm_code = COALESCE(NULLIF(?, ''), perm_code), "
                         "perm_name = COALESCE(NULLIF(?, ''), perm_name), "
                         "module_name = COALESCE(NULLIF(?, ''), module_name) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE permissions SET "
                         "perm_code = COALESCE(NULLIF(?, ''), perm_code), "
                         "perm_name = COALESCE(NULLIF(?, ''), perm_name), "
                         "module_name = COALESCE(NULLIF(?, ''), module_name) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE permissions SET "
                         "perm_code = COALESCE(NULLIF($1, ''), perm_code), "
                         "perm_name = COALESCE(NULLIF($2, ''), perm_name), "
                         "module_name = COALESCE(NULLIF($3, ''), module_name) "
                         "WHERE id = $4;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    for (int i = 1; i <= 3; ++i) {
        if (!sql_bind(&stmt, i, SQL_SV(fields[i - 1]))) return_defer(false);
    }
    if (!sql_bind(&stmt, 4, SQL_SV(id)))        return_defer(false);
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool soft_delete_permission(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE permissions "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE permissions SET deleted_at = NOW() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE permissions SET deleted_at = now() WHERE id = $1;",
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

static bool restore_permission(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE permissions SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE permissions SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE permissions SET deleted_at = NULL WHERE id = $1;",
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

void serve_pos_permissions(Serve_Context *sc) {
    MD_MasterConfig config = {
        .table          = "permissions",
        .title          = "Permissions",
        .id_column      = "id",
        .crud_path      = "/pos/permissions",
        .columns        = md_permissions_columns,
        .column_count   = md_permissions_columns_count,
        .soft_delete    = 1,
    };
    serve_master_child(sc, &config);
}

static const char *prm_fields[] = { "perm_code", "perm_name", "module_name" };
static const char *prm_opt_fields[] = { "created_at" };
SERVE_CREATE(pos_permissions, permission, prm_fields, prm_opt_fields)
SERVE_UPDATE(pos_permissions, permission, prm_fields, prm_opt_fields)
SERVE_SOFT_DELETE(pos_permissions, permission)
