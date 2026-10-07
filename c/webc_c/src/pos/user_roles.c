#include "user_roles.h"

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "core/http/utils.h"

// =========================================================================
// user_roles rows: created/edited from the /pos/users Roles child tab
// (user_id rides the form's query string). UNIQUE(user_id, role_id) --
// a duplicate link fails loudly (500).
// =========================================================================

static bool create_user_role(db_t *db, String_View *fields, size_t count) {
    if (count < 2) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO user_roles (user_id, role_id) "
                         "VALUES (?, ?);",
        [SQL_MYSQL]    = "INSERT INTO user_roles (user_id, role_id) "
                         "VALUES (?, ?);",
        [SQL_POSTGRES] = "INSERT INTO user_roles (user_id, role_id) "
                         "VALUES ($1, $2);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, pos_sv(fields[1]))) return_defer(false);  // user
    if (!sql_bind(&stmt, 2, SQL_SV(fields[0]))) return_defer(false);  // role
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

// The link row's role can be re-pointed; the user never moves (it is
// the child fk, re-sent unchanged in the query string).
static bool update_user_role(db_t *db, String_View *fields, size_t count,
                             String_View id)
{
    if (count < 2) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE user_roles SET "
                         "role_id = COALESCE(NULLIF(?, ''), role_id) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE user_roles SET "
                         "role_id = COALESCE(NULLIF(?, ''), role_id) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE user_roles SET "
                         "role_id = COALESCE(NULLIF($1, ''), role_id) "
                         "WHERE id = $2;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(fields[0]))) return_defer(false);
    if (!sql_bind(&stmt, 2, SQL_SV(id)))        return_defer(false);
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool soft_delete_user_role(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE user_roles "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE user_roles SET deleted_at = UTC_TIMESTAMP() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE user_roles SET deleted_at = now() WHERE id = $1;",
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

static bool restore_user_role(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE user_roles SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE user_roles SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE user_roles SET deleted_at = NULL WHERE id = $1;",
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

static const char *ur_fields[] = { "role_id" };
static const char *ur_opt_fields[] = { "user_id" };
SERVE_CREATE(pos_user_roles, user_role, ur_fields, ur_opt_fields)
SERVE_UPDATE(pos_user_roles, user_role, ur_fields, ur_opt_fields)
SERVE_SOFT_DELETE(pos_user_roles, user_role)

// ---- /pos/role_members: the same link seen from /pos/roles ----------------
// The body/query split mirrors this view: user_id is a body column and
// role_id rides the query string. SERVE_EXTRACT_FIELDS is body-only (a
// missing key is a 400), so the two views cannot share one fields array.
static bool create_role_member(db_t *db, String_View *fields, size_t count) {
    if (count < 2) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO user_roles (user_id, role_id) "
                         "VALUES (?, ?);",
        [SQL_MYSQL]    = "INSERT INTO user_roles (user_id, role_id) "
                         "VALUES (?, ?);",
        [SQL_POSTGRES] = "INSERT INTO user_roles (user_id, role_id) "
                         "VALUES ($1, $2);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt))  return_defer(false);
    if (!sql_bind(&stmt, 1, pos_sv(fields[0]))) return_defer(false);  // user
    if (!sql_bind(&stmt, 2, pos_sv(fields[1]))) return_defer(false);  // role
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

// Only the user can move here; role is the child fk (query echo).
static bool update_role_member(db_t *db, String_View *fields, size_t count,
                               String_View id)
{
    if (count < 2) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE user_roles SET "
                         "user_id = COALESCE(NULLIF(?, ''), user_id) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE user_roles SET "
                         "user_id = COALESCE(NULLIF(?, ''), user_id) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE user_roles SET "
                         "user_id = COALESCE(NULLIF($1, ''), user_id) "
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

static const char *rm_fields[] = { "user_id" };
static const char *rm_opt_fields[] = { "role_id" };
SERVE_CREATE(pos_role_members, role_member, rm_fields, rm_opt_fields)
SERVE_UPDATE(pos_role_members, role_member, rm_fields, rm_opt_fields)
SERVE_SOFT_DELETE(pos_role_members, user_role)
