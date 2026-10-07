#include "ledger.h"

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "core/http/utils.h"

// =========================================================================
// stock_ledger rows: movement log entries created from the /pos/stocks
// child tab (stock_id rides the form's query string). reference_type is
// the free enum ORDER | PURCHASE | ADJUST | RETURN; note stays on its
// DEFAULT NULL (not a form column).
// =========================================================================

static bool create_pos_ledger(db_t *db, String_View *fields, size_t count) {
    if (count < 5) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO stock_ledger "
                         "(stock_id, reference_type, reference_id, "
                          "quantity_change, balance_after) "
                         "VALUES (?, ?, ?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO stock_ledger "
                         "(stock_id, reference_type, reference_id, "
                          "quantity_change, balance_after) "
                         "VALUES (?, ?, ?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO stock_ledger "
                         "(stock_id, reference_type, reference_id, "
                          "quantity_change, balance_after) "
                         "VALUES ($1, $2, $3, $4, $5);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, pos_sv(fields[4])))  return_defer(false);  // stock
    if (!sql_bind(&stmt, 2, SQL_SV(fields[0])))  return_defer(false);  // type
    if (!sql_bind(&stmt, 3, pos_sv(fields[1])))  return_defer(false);  // ref id
    if (!sql_bind(&stmt, 4, pos_num(fields[2]))) return_defer(false);  // change
    if (!sql_bind(&stmt, 5, pos_num(fields[3]))) return_defer(false);  // balance
    if (!sql_final_step(&stmt))                  return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

// stock_id is never UPDATEd (a log entry belongs to its stock row).
static bool update_pos_ledger(db_t *db, String_View *fields, size_t count,
                              String_View id)
{
    if (count < 5) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE stock_ledger SET "
                         "reference_type = COALESCE(NULLIF(?, ''), reference_type), "
                         "reference_id = COALESCE(NULLIF(?, ''), reference_id), "
                         "quantity_change = COALESCE(NULLIF(?, ''), quantity_change), "
                         "balance_after = COALESCE(NULLIF(?, ''), balance_after) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE stock_ledger SET "
                         "reference_type = COALESCE(NULLIF(?, ''), reference_type), "
                         "reference_id = COALESCE(NULLIF(?, ''), reference_id), "
                         "quantity_change = COALESCE(NULLIF(?, ''), quantity_change), "
                         "balance_after = COALESCE(NULLIF(?, ''), balance_after) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE stock_ledger SET "
                         "reference_type = COALESCE(NULLIF($1, ''), reference_type), "
                         "reference_id = COALESCE(NULLIF($2, ''), reference_id), "
                         "quantity_change = COALESCE(NULLIF($3, ''), quantity_change), "
                         "balance_after = COALESCE(NULLIF($4, ''), balance_after) "
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

static bool soft_delete_pos_ledger(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE stock_ledger "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE stock_ledger SET deleted_at = UTC_TIMESTAMP() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE stock_ledger SET deleted_at = now() WHERE id = $1;",
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

static bool restore_pos_ledger(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE stock_ledger SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE stock_ledger SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE stock_ledger SET deleted_at = NULL WHERE id = $1;",
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

static const char *ldg_fields[] = {
    "reference_type", "reference_id", "quantity_change", "balance_after",
};
static const char *ldg_opt_fields[] = { "stock_id" };
SERVE_CREATE(pos_ledger, pos_ledger, ldg_fields, ldg_opt_fields)
SERVE_UPDATE(pos_ledger, pos_ledger, ldg_fields, ldg_opt_fields)
SERVE_SOFT_DELETE(pos_ledger, pos_ledger)
