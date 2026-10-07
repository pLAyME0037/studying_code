#include "role_permissions.h"

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "core/http/utils.h"

// =========================================================================
// role_permissions rows: created/edited from the /pos/roles Permissions
// child tab (role_id rides the form's query string). UNIQUE(role_id,
// permission_id) -- a duplicate grant fails loudly (500).
// =========================================================================

static bool create_role_permission(db_t *db, String_View *fields,
                                   size_t count)
{
    if (count < 2) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO role_permissions (role_id, permission_id) "
                         "VALUES (?, ?);",
        [SQL_MYSQL]    = "INSERT INTO role_permissions (role_id, permission_id) "
                         "VALUES (?, ?);",
        [SQL_POSTGRES] = "INSERT INTO role_permissions (role_id, permission_id) "
                         "VALUES ($1, $2);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt))  return_defer(false);
    if (!sql_bind(&stmt, 1, pos_sv(fields[1]))) return_defer(false);  // role
    if (!sql_bind(&stmt, 2, SQL_SV(fields[0]))) return_defer(false);  // permission
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

// The grant row's permission can be re-pointed; the role never moves (it
// is the child fk, re-sent unchanged in the query string).
static bool update_role_permission(db_t *db, String_View *fields,
                                   size_t count, String_View id)
{
    if (count < 2) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE role_permissions SET "
                         "permission_id = COALESCE(NULLIF(?, ''), permission_id) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE role_permissions SET "
                         "permission_id = COALESCE(NULLIF(?, ''), permission_id) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE role_permissions SET "
                         "permission_id = COALESCE(NULLIF($1, ''), permission_id) "
                         "WHERE id = $2;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt))  return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(fields[0]))) return_defer(false);
    if (!sql_bind(&stmt, 2, SQL_SV(id)))        return_defer(false);
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool soft_delete_role_permission(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE role_permissions "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE role_permissions SET deleted_at = UTC_TIMESTAMP() "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE role_permissions SET deleted_at = now() "
                         "WHERE id = $1;",
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

static bool restore_role_permission(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE role_permissions "
                         "SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE role_permissions "
                         "SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE role_permissions "
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

static const char *rp_fields[] = { "permission_id" };
static const char *rp_opt_fields[] = { "role_id" };
SERVE_CREATE(pos_role_permissions, role_permission, rp_fields, rp_opt_fields)
SERVE_UPDATE(pos_role_permissions, role_permission, rp_fields, rp_opt_fields)
SERVE_SOFT_DELETE(pos_role_permissions, role_permission)
