#include "org.h"

#include <string.h>

#include "core/display/master_child.h"
#include "core/display/pos.h"     // md_pos_users_columns (users child tab)
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "src/pos/staff.h"        // md_staff_child_org_columns (staff child)
#include "core/http/utils.h"

// =========================================================================
// /pos/org: ou_code + ou_name + ORG_TYPE dict FK + self-FK parent.
// metadata is not a form column (JSON, stays NULL).
// Children: users (fk org_unit_id) + staff (fk org_unit_id).
// =========================================================================

MD_Column md_org_columns[] = {
    { .name = "ou_code", .label = "Code", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "ou_name", .label = "Name", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "ou_type_dict_id", .label = "Type",
      .type = COL_TYPE_FK_SELECT, .nullable = true,
      .fk_table = "dictionaries", .fk_label = "label",
      .fk_where = "category = 'ORG_TYPE'" },
    { .name = "parent_id", .label = "Parent", .type = COL_TYPE_FK_SELECT,
      .nullable = true, .fk_table = "org_units", .fk_label = "ou_name" },
};
const size_t md_org_columns_count = ARRAY_LEN(md_org_columns);

// DB mutations only -- list loading lives in the master_child engine.
static bool create_org_unit(db_t *db, String_View *fields, size_t count) {
    if (count < 5) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO org_units "
                         "(parent_id, ou_code, ou_name, ou_type_dict_id, "
                          "metadata) VALUES (?, ?, ?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO org_units "
                         "(parent_id, ou_code, ou_name, ou_type_dict_id, "
                          "metadata) VALUES (?, ?, ?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO org_units "
                         "(parent_id, ou_code, ou_name, ou_type_dict_id, "
                          "metadata) VALUES ($1, $2, $3, $4, $5);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, pos_sv(fields[2])))  return_defer(false);  // parent
    if (!sql_bind(&stmt, 2, SQL_SV(fields[0])))  return_defer(false);  // code
    if (!sql_bind(&stmt, 3, SQL_SV(fields[1])))  return_defer(false);  // name
    if (!sql_bind(&stmt, 4, pos_sv(fields[3])))  return_defer(false);  // type
    if (!sql_bind(&stmt, 5, pos_sv(fields[4])))  return_defer(false);  // meta
    if (!sql_final_step(&stmt))                  return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

// metadata is not a form column -> not UPDATEd.
static bool update_org_unit(db_t *db, String_View *fields, size_t count,
                            String_View id)
{
    if (count < 5) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE org_units SET "
                         "ou_code = COALESCE(NULLIF(?, ''), ou_code), "
                         "ou_name = COALESCE(NULLIF(?, ''), ou_name), "
                         "parent_id = COALESCE(NULLIF(?, ''), parent_id), "
                         "ou_type_dict_id = COALESCE(NULLIF(?, ''), "
                         "ou_type_dict_id) WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE org_units SET "
                         "ou_code = COALESCE(NULLIF(?, ''), ou_code), "
                         "ou_name = COALESCE(NULLIF(?, ''), ou_name), "
                         "parent_id = COALESCE(NULLIF(?, ''), parent_id), "
                         "ou_type_dict_id = COALESCE(NULLIF(?, ''), "
                         "ou_type_dict_id) WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE org_units SET "
                         "ou_code = COALESCE(NULLIF($1, ''), ou_code), "
                         "ou_name = COALESCE(NULLIF($2, ''), ou_name), "
                         "parent_id = COALESCE(NULLIF($3, ''), parent_id), "
                         "ou_type_dict_id = COALESCE(NULLIF($4, ''), "
                         "ou_type_dict_id) WHERE id = $5;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(fields[0])))  return_defer(false);
    if (!sql_bind(&stmt, 2, SQL_SV(fields[1])))  return_defer(false);
    if (!sql_bind(&stmt, 3, pos_sv(fields[2])))  return_defer(false);
    if (!sql_bind(&stmt, 4, pos_sv(fields[3])))  return_defer(false);
    if (!sql_bind(&stmt, 5, SQL_SV(id)))         return_defer(false);
    if (!sql_final_step(&stmt))                  return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool soft_delete_org_unit(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE org_units "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE org_units SET deleted_at = NOW() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE org_units SET deleted_at = now() WHERE id = $1;",
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

static bool restore_org_unit(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE org_units SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE org_units SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE org_units SET deleted_at = NULL WHERE id = $1;",
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

void serve_pos_org(Serve_Context *sc) {
    MD_ChildTab children[] = {
        {
            .table        = "users",
            .title        = "Users",
            .fk_column    = "org_unit_id",
            .id_column    = "id",
            .crud_path    = "/pos/users",
            .columns      = md_pos_users_columns,
            .column_count = md_pos_users_columns_count,
            .soft_delete  = 1,
        },
        {
            .table        = "staff",
            .title        = "Staff",
            .fk_column    = "org_unit_id",
            .id_column    = "id",
            .crud_path    = "/pos/staff",
            .columns      = md_staff_child_org_columns,
            .column_count = md_staff_child_org_columns_count,
            .soft_delete  = 1,
        },
    };
    MD_MasterConfig config = {
        .table          = "org_units",
        .title          = "Org Units",
        .id_column      = "id",
        .crud_path      = "/pos/org",
        .columns        = md_org_columns,
        .column_count   = md_org_columns_count,
        .children       = children,
        .children_count = ARRAY_LEN(children),
        .soft_delete    = 1,
    };
    serve_master_child(sc, &config);
}

static const char *org_fields[] = { "ou_code", "ou_name" };
static const char *org_opt_fields[] = {
    "parent_id", "ou_type_dict_id", "metadata",
};
SERVE_CREATE(pos_org, org_unit, org_fields, org_opt_fields)
SERVE_UPDATE(pos_org, org_unit, org_fields, org_opt_fields)
SERVE_SOFT_DELETE(pos_org, org_unit)
