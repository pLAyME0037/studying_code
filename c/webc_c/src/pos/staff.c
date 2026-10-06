#include "staff.h"

#include <string.h>

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "src/pos/shifts.h"
#include "core/http/utils.h"

// =========================================================================
// /pos/staff: staff_code + {first,last} name cell + user/org/location FKs
// + phone/hire_date/staff_type (all settable now - Phase 13 audit). Staff
// is a MASTER table: it renders only here, never as a child tab of
// /pos/users or /pos/org. The single create/update serves the one form
// body; user/org/type/hire/phone are still opt fields (body first, query
// fallback) so a pre-0013 API body without them keeps working.
// =========================================================================

static const char *staff_name_parts[] = { "first_name", "last_name" };
static const char *staff_name_labels[] = { "First", "Last" };
static const MD_Cell staff_name_cell = {
    .parts       = staff_name_parts,
    .part_labels = staff_name_labels,
    .part_count  = 2,
    .style       = "stack",
};

MD_Column md_staff_columns[] = {
    { .name = "staff_code", .label = "Code", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "first_name", .label = "Name", .type = COL_TYPE_TEXT,
      .nullable = false, .cell = &staff_name_cell },
    { .name = "user_id", .label = "User", .type = COL_TYPE_FK_SELECT,
      .nullable = false, .fk_table = "users", .fk_label = "name" },
    { .name = "org_unit_id", .label = "Org", .type = COL_TYPE_FK_SELECT,
      .nullable = false, .fk_table = "org_units", .fk_label = "ou_name" },
    { .name = "location_id", .label = "Location", .type = COL_TYPE_FK_SELECT,
      .nullable = false, .fk_table = "locations",
      .fk_label = "province" },
    { .name = "phone", .label = "Phone", .type = COL_TYPE_TEXT,
      .nullable = true },
    { .name = "staff_type_dict_id", .label = "Role", .type = COL_TYPE_FK_SELECT,
      .nullable = true, .fk_table = "dictionaries", .fk_label = "label",
      .fk_where = "category = 'STAFF_TYPE'" },
    { .name = "hire_date", .label = "Hired", .type = COL_TYPE_DATE,
      .nullable = true },
};
const size_t md_staff_columns_count = ARRAY_LEN(md_staff_columns);

// DB mutations only -- list loading lives in the master_child engine.
// Column order: (user_id, org_unit_id, staff_code, staff_type_dict_id,
// first_name, last_name, phone, location_id, hire_date).
static bool create_staff_member(db_t *db, String_View *fields, size_t count) {
    if (count < 9) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO staff "
                         "(user_id, org_unit_id, staff_code, staff_type_dict_id, "
                          "first_name, last_name, phone, location_id, hire_date) "
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO staff "
                         "(user_id, org_unit_id, staff_code, staff_type_dict_id, "
                          "first_name, last_name, phone, location_id, hire_date) "
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO staff "
                         "(user_id, org_unit_id, staff_code, staff_type_dict_id, "
                          "first_name, last_name, phone, location_id, hire_date) "
                         "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, pos_sv(fields[4]))) return_defer(false);  // user
    if (!sql_bind(&stmt, 2, pos_sv(fields[5]))) return_defer(false);  // org
    if (!sql_bind(&stmt, 3, SQL_SV(fields[0]))) return_defer(false);  // code
    if (!sql_bind(&stmt, 4, pos_sv(fields[6]))) return_defer(false);  // type
    if (!sql_bind(&stmt, 5, SQL_SV(fields[1]))) return_defer(false);  // first
    if (!sql_bind(&stmt, 6, SQL_SV(fields[2]))) return_defer(false);  // last
    if (!sql_bind(&stmt, 7, pos_sv(fields[8]))) return_defer(false);  // phone
    if (!sql_bind(&stmt, 8, SQL_SV(fields[3]))) return_defer(false);  // location
    if (!sql_bind(&stmt, 9, pos_sv(fields[7]))) return_defer(false);  // hire
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool update_staff_member(db_t *db, String_View *fields, size_t count,
                                String_View id)
{
    if (count < 9) return false;
    // Every set is COALESCE: fields missing from a view's body (type,
    // hire_date, phone) keep their stored values; the two fk opts come
    // from body (master) or query (child) -- both carry the same value.
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE staff SET "
                         "staff_code = COALESCE(NULLIF(?, ''), staff_code), "
                         "first_name = COALESCE(NULLIF(?, ''), first_name), "
                         "last_name = COALESCE(NULLIF(?, ''), last_name), "
                         "location_id = COALESCE(NULLIF(?, ''), location_id), "
                         "org_unit_id = COALESCE(NULLIF(?, ''), org_unit_id), "
                         "user_id = COALESCE(NULLIF(?, ''), user_id), "
                         "staff_type_dict_id = COALESCE(NULLIF(?, ''), "
                         "staff_type_dict_id), "
                         "hire_date = COALESCE(NULLIF(?, ''), hire_date), "
                         "phone = COALESCE(NULLIF(?, ''), phone) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE staff SET "
                         "staff_code = COALESCE(NULLIF(?, ''), staff_code), "
                         "first_name = COALESCE(NULLIF(?, ''), first_name), "
                         "last_name = COALESCE(NULLIF(?, ''), last_name), "
                         "location_id = COALESCE(NULLIF(?, ''), location_id), "
                         "org_unit_id = COALESCE(NULLIF(?, ''), org_unit_id), "
                         "user_id = COALESCE(NULLIF(?, ''), user_id), "
                         "staff_type_dict_id = COALESCE(NULLIF(?, ''), "
                         "staff_type_dict_id), "
                         "hire_date = COALESCE(NULLIF(?, ''), hire_date), "
                         "phone = COALESCE(NULLIF(?, ''), phone) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE staff SET "
                         "staff_code = COALESCE(NULLIF($1, ''), staff_code), "
                         "first_name = COALESCE(NULLIF($2, ''), first_name), "
                         "last_name = COALESCE(NULLIF($3, ''), last_name), "
                         "location_id = COALESCE(NULLIF($4, ''), location_id), "
                         "org_unit_id = COALESCE(NULLIF($5, ''), org_unit_id), "
                         "user_id = COALESCE(NULLIF($6, ''), user_id), "
                         "staff_type_dict_id = COALESCE(NULLIF($7, ''), "
                         "staff_type_dict_id), "
                         "hire_date = COALESCE(NULLIF($8, ''), hire_date), "
                         "phone = COALESCE(NULLIF($9, ''), phone) "
                         "WHERE id = $10;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(fields[0]))) return_defer(false);  // code
    if (!sql_bind(&stmt, 2, SQL_SV(fields[1]))) return_defer(false);  // first
    if (!sql_bind(&stmt, 3, SQL_SV(fields[2]))) return_defer(false);  // last
    if (!sql_bind(&stmt, 4, SQL_SV(fields[3]))) return_defer(false);  // location
    if (!sql_bind(&stmt, 5, pos_sv(fields[5]))) return_defer(false);  // org
    if (!sql_bind(&stmt, 6, pos_sv(fields[4]))) return_defer(false);  // user
    if (!sql_bind(&stmt, 7, pos_sv(fields[6]))) return_defer(false);  // type
    if (!sql_bind(&stmt, 8, pos_sv(fields[7]))) return_defer(false);  // hire
    if (!sql_bind(&stmt, 9, pos_sv(fields[8]))) return_defer(false);  // phone
    if (!sql_bind(&stmt, 10, SQL_SV(id)))       return_defer(false);
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool soft_delete_staff_member(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE staff "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE staff SET deleted_at = NOW() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE staff SET deleted_at = now() WHERE id = $1;",
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

static bool restore_staff_member(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE staff SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE staff SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE staff SET deleted_at = NULL WHERE id = $1;",
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

void serve_pos_staff(Serve_Context *sc) {
    MD_ChildTab children[] = {
        {
            .table        = "cash_shifts",
            .title        = "Shifts",
            .fk_column    = "staff_id",
            .id_column    = "id",
            .crud_path    = "/pos/shifts",
            .columns      = md_shifts_child_columns,
            .column_count = md_shifts_child_columns_count,
            .soft_delete  = 1,
        },
    };
    MD_MasterConfig config = {
        .table          = "staff",
        .title          = "Staff",
        .id_column      = "id",
        .crud_path      = "/pos/staff",
        .columns        = md_staff_columns,
        .column_count   = md_staff_columns_count,
        .children       = children,
        .children_count = ARRAY_LEN(children),
        .soft_delete    = 1,
    };
    serve_master_child(sc, &config);
}

// body keys first (present in all three views), query-driven fks after.
static const char *stf_fields[] = {
    "staff_code", "first_name", "last_name", "location_id",
};
static const char *stf_opt_fields[] = {
    "user_id", "org_unit_id", "staff_type_dict_id", "hire_date", "phone",
};
SERVE_CREATE(pos_staff, staff_member, stf_fields, stf_opt_fields)
SERVE_UPDATE(pos_staff, staff_member, stf_fields, stf_opt_fields)
SERVE_SOFT_DELETE(pos_staff, staff_member)
