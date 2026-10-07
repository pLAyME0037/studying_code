#include "customer_interactions.h"

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "core/http/utils.h"

// =========================================================================
// customer_interactions rows: created/edited from the /pos/customers
// Activity child tab (customer_id rides the form's query string).
// user_id is nullable (anonymous touches).
// =========================================================================

static bool create_interaction(db_t *db, String_View *fields, size_t count) {
    if (count < 4) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO customer_interactions "
                         "(user_id, customer_id, interaction_type, raw_payload) "
                         "VALUES (?, ?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO customer_interactions "
                         "(user_id, customer_id, interaction_type, raw_payload) "
                         "VALUES (?, ?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO customer_interactions "
                         "(user_id, customer_id, interaction_type, raw_payload) "
                         "VALUES ($1, $2, $3, $4);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt))   return_defer(false);
    if (!sql_bind(&stmt, 1, pos_sv(fields[2]))) return_defer(false);  // user
    if (!sql_bind(&stmt, 2, pos_sv(fields[3]))) return_defer(false);  // customer
    if (!sql_bind(&stmt, 3, SQL_SV(fields[0]))) return_defer(false);  // kind
    if (!sql_bind(&stmt, 4, SQL_SV(fields[1]))) return_defer(false);  // payload
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool update_interaction(db_t *db, String_View *fields, size_t count,
                               String_View id)
{
    if (count < 4) return false;
    // customer_id is never UPDATEd (the child edit form's query fk just
    // echoes the stored value back).
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE customer_interactions SET "
                         "interaction_type = COALESCE(NULLIF(?, ''), "
                         "interaction_type), "
                         "raw_payload = COALESCE(NULLIF(?, ''), raw_payload), "
                         "user_id = COALESCE(NULLIF(?, ''), user_id) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE customer_interactions SET "
                         "interaction_type = COALESCE(NULLIF(?, ''), "
                         "interaction_type), "
                         "raw_payload = COALESCE(NULLIF(?, ''), raw_payload), "
                         "user_id = COALESCE(NULLIF(?, ''), user_id) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE customer_interactions SET "
                         "interaction_type = COALESCE(NULLIF($1, ''), "
                         "interaction_type), "
                         "raw_payload = COALESCE(NULLIF($2, ''), raw_payload), "
                         "user_id = COALESCE(NULLIF($3, ''), user_id) "
                         "WHERE id = $4;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(fields[0]))) return_defer(false);  // kind
    if (!sql_bind(&stmt, 2, SQL_SV(fields[1]))) return_defer(false);  // payload
    if (!sql_bind(&stmt, 3, pos_sv(fields[2]))) return_defer(false);  // user
    if (!sql_bind(&stmt, 4, SQL_SV(id)))        return_defer(false);
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool soft_delete_interaction(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE customer_interactions "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE customer_interactions SET deleted_at = UTC_TIMESTAMP() "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE customer_interactions SET deleted_at = now() "
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

static bool restore_interaction(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE customer_interactions "
                         "SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE customer_interactions "
                         "SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE customer_interactions "
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

static const char *itc_fields[] = { "interaction_type", "raw_payload" };
static const char *itc_opt_fields[] = { "user_id", "customer_id" };
SERVE_CREATE(pos_customer_interactions, interaction, itc_fields, itc_opt_fields)
SERVE_UPDATE(pos_customer_interactions, interaction, itc_fields, itc_opt_fields)
SERVE_SOFT_DELETE(pos_customer_interactions, interaction)
