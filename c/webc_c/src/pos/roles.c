#include "roles.h"

#include <string.h>

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "core/http/utils.h"

// =========================================================================
// /pos/roles: role_code + role_name + org FK + description.
// Children: role_permissions (fk role_id) and user_roles (fk role_id --
// the same handlers as the /pos/users Roles tab; the fk simply rides a
// different query key). Note: no soft-delete cascade from roles in the
// schema -- deleted links stay live and are hidden by the trash filter.
// =========================================================================

MD_Column md_roles_columns[] = {
    { .name = "role_code", .label = "Code", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "role_name", .label = "Name", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "org_unit_id", .label = "Org", .type = COL_TYPE_FK_SELECT,
      .nullable = true, .fk_table = "org_units", .fk_label = "ou_name" },
    { .name = "description", .label = "Description", .type = COL_TYPE_TEXT,
      .nullable = true },
};
const size_t md_roles_columns_count = ARRAY_LEN(md_roles_columns);

// Child shapes: permission link / user link under a role.
static MD_Column md_role_permission_columns[] = {
    { .name = "permission_id", .label = "Permission",
      .type = COL_TYPE_FK_SELECT, .nullable = false,
      .fk_table = "permissions", .fk_label = "perm_name" },
    { .name = "created_at", .label = "Granted", .type = COL_TYPE_DATE,
      .nullable = false, .computed = 1 },
};
static const size_t md_role_permission_columns_count =
    ARRAY_LEN(md_role_permission_columns);

static MD_Column md_role_user_columns[] = {
    { .name = "user_id", .label = "User", .type = COL_TYPE_FK_SELECT,
      .nullable = false, .fk_table = "users", .fk_label = "name" },
    { .name = "created_at", .label = "Linked", .type = COL_TYPE_DATE,
      .nullable = false, .computed = 1 },
};
static const size_t md_role_user_columns_count = ARRAY_LEN(md_role_user_columns);

// DB mutations only -- list loading lives in the master_child engine.
static bool create_role(db_t *db, String_View *fields, size_t count) {
    if (count < 4) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO roles "
                         "(org_unit_id, role_code, role_name, description) "
                         "VALUES (?, ?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO roles "
                         "(org_unit_id, role_code, role_name, description) "
                         "VALUES (?, ?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO roles "
                         "(org_unit_id, role_code, role_name, description) "
                         "VALUES ($1, $2, $3, $4);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, pos_sv(fields[2]))) return_defer(false);  // org
    if (!sql_bind(&stmt, 2, SQL_SV(fields[0]))) return_defer(false);  // code
    if (!sql_bind(&stmt, 3, SQL_SV(fields[1]))) return_defer(false);  // name
    if (!sql_bind(&stmt, 4, pos_sv(fields[3]))) return_defer(false);  // desc
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool update_role(db_t *db, String_View *fields, size_t count,
                        String_View id)
{
    if (count < 4) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE roles SET "
                         "role_code = COALESCE(NULLIF(?, ''), role_code), "
                         "role_name = COALESCE(NULLIF(?, ''), role_name), "
                         "org_unit_id = COALESCE(NULLIF(?, ''), org_unit_id), "
                         "description = COALESCE(NULLIF(?, ''), description) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE roles SET "
                         "role_code = COALESCE(NULLIF(?, ''), role_code), "
                         "role_name = COALESCE(NULLIF(?, ''), role_name), "
                         "org_unit_id = COALESCE(NULLIF(?, ''), org_unit_id), "
                         "description = COALESCE(NULLIF(?, ''), description) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE roles SET "
                         "role_code = COALESCE(NULLIF($1, ''), role_code), "
                         "role_name = COALESCE(NULLIF($2, ''), role_name), "
                         "org_unit_id = COALESCE(NULLIF($3, ''), org_unit_id), "
                         "description = COALESCE(NULLIF($4, ''), description) "
                         "WHERE id = $5;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    for (int i = 1; i <= 4; ++i) {
        if (!sql_bind(&stmt, i, SQL_SV(fields[i - 1]))) return_defer(false);
    }
    if (!sql_bind(&stmt, 5, SQL_SV(id)))        return_defer(false);
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool soft_delete_role(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE roles "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE roles SET deleted_at = UTC_TIMESTAMP() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE roles SET deleted_at = now() WHERE id = $1;",
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

static bool restore_role(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE roles SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE roles SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE roles SET deleted_at = NULL WHERE id = $1;",
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

void serve_pos_roles(Serve_Context *sc) {
    MD_ChildTab children[] = {
        {
            .table        = "role_permissions",
            .title        = "Permissions",
            .fk_column    = "role_id",
            .id_column    = "id",
            .crud_path    = "/pos/role_permissions",
            .columns      = md_role_permission_columns,
            .column_count = md_role_permission_columns_count,
            .soft_delete  = 1,
        },
        {
            .table        = "user_roles",
            .title        = "Members",
            .fk_column    = "role_id",
            .id_column    = "id",
            .crud_path    = "/pos/role_members",
            .columns      = md_role_user_columns,
            .column_count = md_role_user_columns_count,
            .soft_delete  = 1,
        },
    };
    MD_MasterConfig config = {
        .table          = "roles",
        .title          = "Roles",
        .id_column      = "id",
        .crud_path      = "/pos/roles",
        .columns        = md_roles_columns,
        .column_count   = md_roles_columns_count,
        .children       = children,
        .children_count = ARRAY_LEN(children),
        .soft_delete    = 1,
    };
    serve_master_child(sc, &config);
}

static const char *rol_fields[] = { "role_code", "role_name" };
static const char *rol_opt_fields[] = { "org_unit_id", "description" };
SERVE_CREATE(pos_roles, role, rol_fields, rol_opt_fields)
SERVE_UPDATE(pos_roles, role, rol_fields, rol_opt_fields)
SERVE_SOFT_DELETE(pos_roles, role)
