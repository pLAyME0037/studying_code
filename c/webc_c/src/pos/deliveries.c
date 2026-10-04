#include "deliveries.h"

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "core/http/utils.h"

// =========================================================================
// deliveries rows: created/edited from the /pos/orders Delivery child tab
// (order_id rides the form's query string; UNIQUE(order_id) = one
// delivery per order). dispatched_at/delivered_at/delivery_metadata are
// not form columns (left NULL until a driver workflow sets them).
// =========================================================================

static bool create_delivery(db_t *db, String_View *fields, size_t count) {
    if (count < 10) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO deliveries "
                         "(order_id, driver_staff_id, delivery_status, "
                          "recipient_name, recipient_phone, delivery_address, "
                          "delivery_cost, dispatched_at, delivered_at, "
                          "delivery_metadata) "
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO deliveries "
                         "(order_id, driver_staff_id, delivery_status, "
                          "recipient_name, recipient_phone, delivery_address, "
                          "delivery_cost, dispatched_at, delivered_at, "
                          "delivery_metadata) "
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO deliveries "
                         "(order_id, driver_staff_id, delivery_status, "
                          "recipient_name, recipient_phone, delivery_address, "
                          "delivery_cost, dispatched_at, delivered_at, "
                          "delivery_metadata) "
                         "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, pos_sv(fields[6]))) return_defer(false);   // order
    if (!sql_bind(&stmt, 2, pos_sv(fields[5]))) return_defer(false);   // driver
    // delivery_status CHECKs pending/in_transit/delivered/cancelled: blank
    // input falls back to the column default.
    String_View status =
        fields[3].count ? fields[3] : sv_from_cstr("pending");
    if (!sql_bind(&stmt, 3, SQL_SV(status)))    return_defer(false);
    if (!sql_bind(&stmt, 4, SQL_SV(fields[0]))) return_defer(false);   // name
    if (!sql_bind(&stmt, 5, SQL_SV(fields[1]))) return_defer(false);   // phone
    if (!sql_bind(&stmt, 6, SQL_SV(fields[2]))) return_defer(false);   // address
    if (!sql_bind(&stmt, 7, pos_num(fields[4]))) return_defer(false);  // cost
    if (!sql_bind(&stmt, 8, pos_sv(fields[8]))) return_defer(false);   // dispatched
    if (!sql_bind(&stmt, 9, pos_sv(fields[9]))) return_defer(false);   // delivered
    if (!sql_bind(&stmt, 10, pos_sv(fields[7]))) return_defer(false);  // metadata
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

// dispatched_at/delivered_at/delivery_metadata are not form columns ->
// not UPDATEd.
static bool update_delivery(db_t *db, String_View *fields, size_t count,
                            String_View id)
{
    if (count < 10) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE deliveries SET "
                         "driver_staff_id = COALESCE(NULLIF(?, ''), driver_staff_id), "
                         "delivery_status = COALESCE(NULLIF(?, ''), delivery_status), "
                         "recipient_name = COALESCE(NULLIF(?, ''), recipient_name), "
                         "recipient_phone = COALESCE(NULLIF(?, ''), recipient_phone), "
                         "delivery_address = COALESCE(NULLIF(?, ''), delivery_address), "
                         "delivery_cost = COALESCE(NULLIF(?, ''), delivery_cost) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE deliveries SET "
                         "driver_staff_id = COALESCE(NULLIF(?, ''), driver_staff_id), "
                         "delivery_status = COALESCE(NULLIF(?, ''), delivery_status), "
                         "recipient_name = COALESCE(NULLIF(?, ''), recipient_name), "
                         "recipient_phone = COALESCE(NULLIF(?, ''), recipient_phone), "
                         "delivery_address = COALESCE(NULLIF(?, ''), delivery_address), "
                         "delivery_cost = COALESCE(NULLIF(?, ''), delivery_cost) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE deliveries SET "
                         "driver_staff_id = COALESCE(NULLIF($1, ''), driver_staff_id), "
                         "delivery_status = COALESCE(NULLIF($2, ''), delivery_status), "
                         "recipient_name = COALESCE(NULLIF($3, ''), recipient_name), "
                         "recipient_phone = COALESCE(NULLIF($4, ''), recipient_phone), "
                         "delivery_address = COALESCE(NULLIF($5, ''), delivery_address), "
                         "delivery_cost = COALESCE(NULLIF($6, ''), delivery_cost) "
                         "WHERE id = $7;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, pos_sv(fields[5]))) return_defer(false);   // driver
    if (!sql_bind(&stmt, 2, SQL_SV(fields[3]))) return_defer(false);   // status
    if (!sql_bind(&stmt, 3, SQL_SV(fields[0]))) return_defer(false);   // name
    if (!sql_bind(&stmt, 4, SQL_SV(fields[1]))) return_defer(false);   // phone
    if (!sql_bind(&stmt, 5, SQL_SV(fields[2]))) return_defer(false);   // address
    if (!sql_bind(&stmt, 6, SQL_SV(fields[4]))) return_defer(false);   // cost
    if (!sql_bind(&stmt, 7, SQL_SV(id)))        return_defer(false);
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool soft_delete_delivery(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE deliveries "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE deliveries SET deleted_at = NOW() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE deliveries SET deleted_at = now() WHERE id = $1;",
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

static bool restore_delivery(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE deliveries SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE deliveries SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE deliveries SET deleted_at = NULL WHERE id = $1;",
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

static const char *dl_fields[] = {
    "recipient_name", "recipient_phone", "delivery_address",
    "delivery_status", "delivery_cost", "driver_staff_id",
};
static const char *dl_opt_fields[] = {
    "order_id", "delivery_metadata", "dispatched_at", "delivered_at",
};
SERVE_CREATE(pos_deliveries, delivery, dl_fields, dl_opt_fields)
SERVE_UPDATE(pos_deliveries, delivery, dl_fields, dl_opt_fields)
SERVE_SOFT_DELETE(pos_deliveries, delivery)
