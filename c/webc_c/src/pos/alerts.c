#include "alerts.h"

#include <string.h>

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "core/http/utils.h"

// =========================================================================
// /pos/alerts: user/order FKs (both nullable) + alert_type + raw_payload
// + a {is_seen,is_sent} flags stack cell. created_at rides its column
// default (not a form key).
// =========================================================================

static const char *alert_flag_parts[] = { "is_seen", "is_sent" };
static const char *alert_flag_labels[] = { "Seen", "Sent" };
static const MD_Cell alert_flag_cell = {
    .parts       = alert_flag_parts,
    .part_labels = alert_flag_labels,
    .part_count  = 2,
    .style       = "stack",
};

MD_Column md_alerts_columns[] = {
    { .name = "user_id", .label = "User", .type = COL_TYPE_FK_SELECT,
      .nullable = true, .fk_table = "users", .fk_label = "name" },
    { .name = "order_id", .label = "Order", .type = COL_TYPE_FK_SELECT,
      .nullable = true, .fk_table = "orders", .fk_label = "order_number" },
    { .name = "alert_type", .label = "Type", .type = COL_TYPE_TEXT,
      .nullable = false },   // MAIL | POPUP | ORDER_DELAY
    { .name = "is_seen", .label = "Flags", .type = COL_TYPE_NUM,
      .nullable = false, .cell = &alert_flag_cell },
    { .name = "raw_payload", .label = "Payload", .type = COL_TYPE_TEXT,
      .nullable = false },
};
const size_t md_alerts_columns_count = ARRAY_LEN(md_alerts_columns);

// DB mutations only -- list loading lives in the master_child engine.
static bool create_alert(db_t *db, String_View *fields, size_t count) {
    if (count < 6) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO system_alerts (user_id, order_id, "
                         "alert_type, raw_payload, is_seen, is_sent) "
                         "VALUES (?, ?, ?, ?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO system_alerts (user_id, order_id, "
                         "alert_type, raw_payload, is_seen, is_sent) "
                         "VALUES (?, ?, ?, ?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO system_alerts (user_id, order_id, "
                         "alert_type, raw_payload, is_seen, is_sent) "
                         "VALUES ($1, $2, $3, $4, $5, $6);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt))   return_defer(false);
    if (!sql_bind(&stmt, 1, pos_sv(fields[2])))   return_defer(false);  // user
    if (!sql_bind(&stmt, 2, pos_sv(fields[3])))   return_defer(false);  // order
    if (!sql_bind(&stmt, 3, SQL_SV(fields[0])))   return_defer(false);  // type
    if (!sql_bind(&stmt, 4, SQL_SV(fields[1])))   return_defer(false);  // payload
    if (!sql_bind(&stmt, 5, pos_num(fields[4])))  return_defer(false);  // seen
    if (!sql_bind(&stmt, 6, pos_num(fields[5])))  return_defer(false);  // sent
    if (!sql_final_step(&stmt))                   return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool update_alert(db_t *db, String_View *fields, size_t count,
                         String_View id)
{
    if (count < 6) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE system_alerts SET "
                         "alert_type = COALESCE(NULLIF(?, ''), alert_type), "
                         "raw_payload = COALESCE(NULLIF(?, ''), raw_payload), "
                         "user_id = COALESCE(NULLIF(?, ''), user_id), "
                         "order_id = COALESCE(NULLIF(?, ''), order_id), "
                         "is_seen = COALESCE(NULLIF(?, ''), is_seen), "
                         "is_sent = COALESCE(NULLIF(?, ''), is_sent) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE system_alerts SET "
                         "alert_type = COALESCE(NULLIF(?, ''), alert_type), "
                         "raw_payload = COALESCE(NULLIF(?, ''), raw_payload), "
                         "user_id = COALESCE(NULLIF(?, ''), user_id), "
                         "order_id = COALESCE(NULLIF(?, ''), order_id), "
                         "is_seen = COALESCE(NULLIF(?, ''), is_seen), "
                         "is_sent = COALESCE(NULLIF(?, ''), is_sent) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE system_alerts SET "
                         "alert_type = COALESCE(NULLIF($1, ''), alert_type), "
                         "raw_payload = COALESCE(NULLIF($2, ''), raw_payload), "
                         "user_id = COALESCE(NULLIF($3, ''), user_id), "
                         "order_id = COALESCE(NULLIF($4, ''), order_id), "
                         "is_seen = COALESCE(NULLIF($5, ''), is_seen), "
                         "is_sent = COALESCE(NULLIF($6, ''), is_sent) "
                         "WHERE id = $7;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt))   return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(fields[0])))   return_defer(false);  // type
    if (!sql_bind(&stmt, 2, SQL_SV(fields[1])))   return_defer(false);  // payload
    if (!sql_bind(&stmt, 3, pos_sv(fields[2])))   return_defer(false);  // user
    if (!sql_bind(&stmt, 4, pos_sv(fields[3])))   return_defer(false);  // order
    if (!sql_bind(&stmt, 5, SQL_SV(fields[4])))   return_defer(false);  // seen
    if (!sql_bind(&stmt, 6, SQL_SV(fields[5])))   return_defer(false);  // sent
    if (!sql_bind(&stmt, 7, SQL_SV(id)))          return_defer(false);
    if (!sql_final_step(&stmt))                   return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool soft_delete_alert(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE system_alerts "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE system_alerts SET deleted_at = NOW() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE system_alerts SET deleted_at = now() WHERE id = $1;",
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

static bool restore_alert(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE system_alerts SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE system_alerts SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE system_alerts SET deleted_at = NULL WHERE id = $1;",
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

void serve_pos_alerts(Serve_Context *sc) {
    MD_MasterConfig config = {
        .table          = "system_alerts",
        .title          = "Alerts",
        .id_column      = "id",
        .crud_path      = "/pos/alerts",
        .columns        = md_alerts_columns,
        .column_count   = md_alerts_columns_count,
        .soft_delete    = 1,
    };
    serve_master_child(sc, &config);
}

static const char *alrt_fields[] = { "alert_type", "raw_payload" };
static const char *alrt_opt_fields[] = {
    "user_id", "order_id", "is_seen", "is_sent",
};
SERVE_CREATE(pos_alerts, alert, alrt_fields, alrt_opt_fields)
SERVE_UPDATE(pos_alerts, alert, alrt_fields, alrt_opt_fields)
SERVE_SOFT_DELETE(pos_alerts, alert)
