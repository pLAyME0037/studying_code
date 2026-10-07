#include "payments.h"

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "core/http/utils.h"

// =========================================================================
// payments rows: created/edited from the /pos/orders Payments child tab
// (order_id rides the form's query string). raw_payload is not a form
// column (stays on its DEFAULT NULL).
// =========================================================================

static bool create_payment(db_t *db, String_View *fields, size_t count) {
    if (count < 6) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO payments "
                         "(order_id, payment_method_dict_id, amount, "
                          "payment_status, transaction_ref, raw_payload) "
                         "VALUES (?, ?, ?, ?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO payments "
                         "(order_id, payment_method_dict_id, amount, "
                          "payment_status, transaction_ref, raw_payload) "
                         "VALUES (?, ?, ?, ?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO payments "
                         "(order_id, payment_method_dict_id, amount, "
                          "payment_status, transaction_ref, raw_payload) "
                         "VALUES ($1, $2, $3, $4, $5, $6);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, pos_sv(fields[5]))) return_defer(false);  // order
    if (!sql_bind(&stmt, 2, pos_sv(fields[3]))) return_defer(false);  // method
    if (!sql_bind(&stmt, 3, pos_num(fields[0]))) return_defer(false); // amount
    // payment_status CHECKs PENDING/COMPLETED/FAILED/REFUNDED: blank
    // input falls back to the column default.
    String_View status = fields[1].count ? fields[1] : sv_from_cstr("COMPLETED");
    if (!sql_bind(&stmt, 4, SQL_SV(status)))    return_defer(false);
    if (!sql_bind(&stmt, 5, pos_sv(fields[2]))) return_defer(false);  // ref
    if (!sql_bind(&stmt, 6, pos_sv(fields[4]))) return_defer(false);  // payload
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

// raw_payload is not a form column -> not UPDATEd.
static bool update_payment(db_t *db, String_View *fields, size_t count,
                           String_View id)
{
    if (count < 6) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE payments SET "
                         "payment_method_dict_id = COALESCE(NULLIF(?, ''), "
                         "payment_method_dict_id), "
                         "amount = COALESCE(NULLIF(?, ''), amount), "
                         "payment_status = COALESCE(NULLIF(?, ''), payment_status), "
                         "transaction_ref = COALESCE(NULLIF(?, ''), transaction_ref) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE payments SET "
                         "payment_method_dict_id = COALESCE(NULLIF(?, ''), "
                         "payment_method_dict_id), "
                         "amount = COALESCE(NULLIF(?, ''), amount), "
                         "payment_status = COALESCE(NULLIF(?, ''), payment_status), "
                         "transaction_ref = COALESCE(NULLIF(?, ''), transaction_ref) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE payments SET "
                         "payment_method_dict_id = COALESCE(NULLIF($1, ''), "
                         "payment_method_dict_id), "
                         "amount = COALESCE(NULLIF($2, ''), amount), "
                         "payment_status = COALESCE(NULLIF($3, ''), payment_status), "
                         "transaction_ref = COALESCE(NULLIF($4, ''), transaction_ref) "
                         "WHERE id = $5;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, pos_sv(fields[3]))) return_defer(false);  // method
    if (!sql_bind(&stmt, 2, SQL_SV(fields[0]))) return_defer(false);  // amount
    if (!sql_bind(&stmt, 3, SQL_SV(fields[1]))) return_defer(false);  // status
    if (!sql_bind(&stmt, 4, pos_sv(fields[2]))) return_defer(false);  // ref
    if (!sql_bind(&stmt, 5, SQL_SV(id)))        return_defer(false);
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool soft_delete_payment(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE payments "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE payments SET deleted_at = UTC_TIMESTAMP() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE payments SET deleted_at = now() WHERE id = $1;",
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

static bool restore_payment(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE payments SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE payments SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE payments SET deleted_at = NULL WHERE id = $1;",
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

static const char *pay_fields[] = { "amount", "payment_status", "transaction_ref" };
static const char *pay_opt_fields[] = {
    "payment_method_dict_id", "raw_payload", "order_id",
};
SERVE_CREATE(pos_payments, payment, pay_fields, pay_opt_fields)
SERVE_UPDATE(pos_payments, payment, pay_fields, pay_opt_fields)
SERVE_SOFT_DELETE(pos_payments, payment)
