#include "shifts.h"

#include <string.h>

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "core/http/utils.h"

// =========================================================================
// /pos/shifts: org/staff FKs + status + cash range cell + notes.
// opened_at has a column default (binds never touch it); closed_at and
// the optional cash figures arrive as missing opts -> NULL/0 on create
// and keep their values on update.
// =========================================================================

static const char *shift_cash_parts[] = {
    "opening_cash", "closing_cash", "expected_cash",
};
static const char *shift_cash_labels[] = { "Opening", "Closing", "Expected" };
static const MD_Cell shift_cash_cell = {
    .parts       = shift_cash_parts,
    .part_labels = shift_cash_labels,
    .part_count  = 3,
    .style       = "stack",
};

MD_Column md_shifts_columns[] = {
    { .name = "org_unit_id", .label = "Org", .type = COL_TYPE_FK_SELECT,
      .nullable = false, .fk_table = "org_units", .fk_label = "ou_name" },
    { .name = "staff_id", .label = "Staff", .type = COL_TYPE_FK_SELECT,
      .nullable = false, .fk_table = "staff", .fk_label = "first_name" },
    { .name = "status", .label = "Status", .type = COL_TYPE_TEXT,
      .nullable = false },   // OPEN | CLOSED | AUDITED (CHECK)
    { .name = "opening_cash", .label = "Cash", .type = COL_TYPE_NUM,
      .nullable = false, .cell = &shift_cash_cell },
    { .name = "notes", .label = "Notes", .type = COL_TYPE_TEXT,
      .nullable = true },
};
const size_t md_shifts_columns_count = ARRAY_LEN(md_shifts_columns);

// DB mutations only -- list loading lives in the master_child engine.
static bool create_cash_shift(db_t *db, String_View *fields, size_t count) {
    if (count < 8) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO cash_shifts "
                         "(org_unit_id, staff_id, status, closed_at, "
                          "opening_cash, closing_cash, expected_cash, notes) "
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO cash_shifts "
                         "(org_unit_id, staff_id, status, closed_at, "
                          "opening_cash, closing_cash, expected_cash, notes) "
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO cash_shifts "
                         "(org_unit_id, staff_id, status, closed_at, "
                          "opening_cash, closing_cash, expected_cash, notes) "
                         "VALUES ($1, $2, $3, $4, $5, $6, $7, $8);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(fields[0]))) return_defer(false);  // org
    if (!sql_bind(&stmt, 2, SQL_SV(fields[1]))) return_defer(false);  // staff
    // status CHECKs OPEN/CLOSED/AUDITED: blank input -> column default.
    String_View status = fields[2].count ? fields[2] : sv_from_cstr("OPEN");
    if (!sql_bind(&stmt, 3, SQL_SV(status)))    return_defer(false);
    if (!sql_bind(&stmt, 4, pos_sv(fields[3])))  return_defer(false);  // closed_at
    if (!sql_bind(&stmt, 5, pos_num(fields[4]))) return_defer(false);  // opening
    if (!sql_bind(&stmt, 6, pos_sv(fields[5])))  return_defer(false);  // closing
    if (!sql_bind(&stmt, 7, pos_sv(fields[6])))  return_defer(false);  // expected
    if (!sql_bind(&stmt, 8, pos_sv(fields[7])))  return_defer(false);  // notes
    if (!sql_final_step(&stmt))                  return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool update_cash_shift(db_t *db, String_View *fields, size_t count,
                              String_View id)
{
    if (count < 8) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE cash_shifts SET "
                         "org_unit_id = COALESCE(NULLIF(?, ''), org_unit_id), "
                         "staff_id = COALESCE(NULLIF(?, ''), staff_id), "
                         "status = COALESCE(NULLIF(?, ''), status), "
                         "closed_at = COALESCE(NULLIF(?, ''), closed_at), "
                         "opening_cash = COALESCE(NULLIF(?, ''), opening_cash), "
                         "closing_cash = COALESCE(NULLIF(?, ''), closing_cash), "
                         "expected_cash = COALESCE(NULLIF(?, ''), expected_cash), "
                         "notes = COALESCE(NULLIF(?, ''), notes) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE cash_shifts SET "
                         "org_unit_id = COALESCE(NULLIF(?, ''), org_unit_id), "
                         "staff_id = COALESCE(NULLIF(?, ''), staff_id), "
                         "status = COALESCE(NULLIF(?, ''), status), "
                         "closed_at = COALESCE(NULLIF(?, ''), closed_at), "
                         "opening_cash = COALESCE(NULLIF(?, ''), opening_cash), "
                         "closing_cash = COALESCE(NULLIF(?, ''), closing_cash), "
                         "expected_cash = COALESCE(NULLIF(?, ''), expected_cash), "
                         "notes = COALESCE(NULLIF(?, ''), notes) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE cash_shifts SET "
                         "org_unit_id = COALESCE(NULLIF($1, ''), org_unit_id), "
                         "staff_id = COALESCE(NULLIF($2, ''), staff_id), "
                         "status = COALESCE(NULLIF($3, ''), status), "
                         "closed_at = COALESCE(NULLIF($4, ''), closed_at), "
                         "opening_cash = COALESCE(NULLIF($5, ''), opening_cash), "
                         "closing_cash = COALESCE(NULLIF($6, ''), closing_cash), "
                         "expected_cash = COALESCE(NULLIF($7, ''), expected_cash), "
                         "notes = COALESCE(NULLIF($8, ''), notes) "
                         "WHERE id = $9;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    for (int i = 1; i <= 8; ++i) {
        if (!sql_bind(&stmt, i, SQL_SV(fields[i - 1]))) return_defer(false);
    }
    if (!sql_bind(&stmt, 9, SQL_SV(id))) return_defer(false);
    if (!sql_final_step(&stmt))          return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool soft_delete_cash_shift(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE cash_shifts "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE cash_shifts SET deleted_at = NOW() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE cash_shifts SET deleted_at = now() WHERE id = $1;",
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

static bool restore_cash_shift(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE cash_shifts SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE cash_shifts SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE cash_shifts SET deleted_at = NULL WHERE id = $1;",
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

void serve_pos_shifts(Serve_Context *sc) {
    MD_MasterConfig config = {
        .table          = "cash_shifts",
        .title          = "Shifts",
        .id_column      = "id",
        .crud_path      = "/pos/shifts",
        .columns        = md_shifts_columns,
        .column_count   = md_shifts_columns_count,
        .soft_delete    = 1,
    };
    serve_master_child(sc, &config);
}

static const char *shf_fields[] = { "org_unit_id", "staff_id", "status" };
static const char *shf_opt_fields[] = {
    "closed_at", "opening_cash", "closing_cash", "expected_cash", "notes",
};
SERVE_CREATE(pos_shifts, cash_shift, shf_fields, shf_opt_fields)
SERVE_UPDATE(pos_shifts, cash_shift, shf_fields, shf_opt_fields)
SERVE_SOFT_DELETE(pos_shifts, cash_shift)
