#include <string.h>

#include "pos.h"
#include "master_child.h"

#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"   // pos_sv/pos_num binders (fk opts)
#include "core/layout/header.h"
#include "core/layout/footer.h"
#include "core/http/utils.h"

// =========================================================================
// /pos/locations: one 4-part stack cell (province -> village).
// /pos/users: avatar cell (profile_pic, name, username) + email.
// Column shapes are separate from the demo md_users_columns so the demo
// pages do not move.
// =========================================================================

// ---- /pos/locations -----------------------------------------------------

static const char *loc_parts[]  = { "province", "district", "commune", "village" };
static const char *loc_labels[] = { "Province", "District", "Commune", "Village" };
static const MD_Cell loc_cell = {
    .parts       = loc_parts,
    .part_labels = loc_labels,
    .part_count  = 4,
    .style       = "stack",
};
MD_Column md_locations_columns[] = {
    { .name = "province", .label = "Location", .type = COL_TYPE_TEXT,
      .nullable = false, .cell = &loc_cell },
    { .name = "created_at", .label = "Created", .type = COL_TYPE_DATE,
      .nullable = false },
};
const size_t md_locations_columns_count = ARRAY_LEN(md_locations_columns);

// DB mutations only -- list loading lives in the master_child engine.
static bool create_location(db_t *db, String_View *fields, size_t count) {
    if (count < 4) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO locations "
                         "(province, district, commune, village) "
                         "VALUES (?, ?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO locations "
                         "(province, district, commune, village) "
                         "VALUES (?, ?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO locations "
                         "(province, district, commune, village) "
                         "VALUES ($1, $2, $3, $4);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    for (int i = 1; i <= 4; ++i) {
        if (!sql_bind(&stmt, i, SQL_SV(fields[i - 1]))) return_defer(false);
    }
    if (!sql_final_step(&stmt)) return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool update_location(db_t *db, String_View *fields, size_t count,
                            String_View id)
{
    if (count < 4) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE locations SET province = ?, district = ?, "
                         "commune = ?, village = ? WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE locations SET province = ?, district = ?, "
                         "commune = ?, village = ? WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE locations SET province = $1, district = $2, "
                         "commune = $3, village = $4 WHERE id = $5;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt))    return_defer(false);
    for (int i = 1; i <= 4; ++i) {
        if (!sql_bind(&stmt, i, SQL_SV(fields[i - 1]))) return_defer(false);
    }
    if (!sql_bind(&stmt, 5, SQL_SV(id)))         return_defer(false);
    if (!sql_final_step(&stmt))                  return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

// Soft delete: the row stays, deleted_at is stamped (migration cascade
// triggers fire on this UPDATE) and the live list filters it out; the
// /restore route clears the column.
static bool soft_delete_location(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE locations "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE locations SET deleted_at = NOW() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE locations SET deleted_at = now() WHERE id = $1;",
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

static bool restore_location(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE locations SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE locations SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE locations SET deleted_at = NULL WHERE id = $1;",
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

void serve_pos_locations(Serve_Context *sc) {
    MD_MasterConfig config = {
        .table          = "locations",
        .title          = "Locations",
        .id_column      = "id",
        .crud_path      = "/pos/locations",
        .columns        = md_locations_columns,
        .column_count   = md_locations_columns_count,
        .soft_delete    = 1,
    };
    serve_master_child(sc, &config);
}

// created_at/date columns are display-only: not bound, so create keeps the
// DB default and update never touches them (same as the demo pages).
// The SERVE_* macros require an opt array; each page gets one real column
// slot to stay non-empty under -pedantic, and the handlers never bind it.
static const char *loc_fields[] = { "province", "district", "commune", "village" };
static const char *loc_opt_fields[] = { "created_at" };
SERVE_CREATE(pos_locations, location, loc_fields, loc_opt_fields)
SERVE_UPDATE(pos_locations, location, loc_fields, loc_opt_fields)
SERVE_SOFT_DELETE(pos_locations, location)

// ---- /pos/users ---------------------------------------------------------

// Avatar cell: picture + name + username + account status. Status prints
// as a colored line and rings the picture (ACTIVE blue / INACTIVE peach /
// SUSPENDED red); part_choices[3] makes its form input a <select>.
// The activity cell is display-only (computed): human joined/edited
// stamps plus a DELETED marker in the trash view.
static const char *usr_parts[]  = { "profile_pic", "name", "username", "status" };
static const char *usr_labels[] = { "Picture URL", "Name", "Username", "Status" };
static const char *usr_status_opts[] = { "ACTIVE", "INACTIVE", "SUSPENDED", NULL };
static const char **usr_cell_choices[] = { NULL, NULL, NULL, usr_status_opts };
static const MD_Cell usr_cell = {
    .parts        = usr_parts,
    .part_labels  = usr_labels,
    .part_count   = 4,
    .style        = "avatar",
    .part_choices = usr_cell_choices,
};
static const char *usr_ts_parts[]  = { "created_at", "updated_at", "deleted_at" };
static const char *usr_ts_labels[] = { "Joined", "Edited", "Deleted" };
static const MD_Cell usr_ts_cell = {
    .parts       = usr_ts_parts,
    .part_labels = usr_ts_labels,
    .part_count  = 3,
    .style       = "activity",
};
// Master fields + every FK the account carries; the child tabs hold the
// editable relations (Roles - user_roles). Staff is a master table and
// lives only on /pos/staff.
MD_Column md_pos_users_columns[] = {
    { .name = "profile_pic", .label = "User", .type = COL_TYPE_TEXT,
      .nullable = true, .cell = &usr_cell },
    { .name = "phone", .label = "Phone", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "email", .label = "Email", .type = COL_TYPE_TEXT,
      .nullable = true },
    { .name = "user_type_dict_id", .label = "Type", .type = COL_TYPE_FK_SELECT,
      .nullable = true, .fk_table = "dictionaries", .fk_label = "label",
      .fk_where = "category = 'USER_TYPE'" },
    { .name = "org_unit_id", .label = "Org", .type = COL_TYPE_FK_SELECT,
      .nullable = true, .fk_table = "org_units", .fk_label = "ou_name" },
    { .name = "location_id", .label = "Location", .type = COL_TYPE_FK_SELECT,
      .nullable = true, .fk_table = "locations", .fk_label = "province" },
    { .name = "customer_id", .label = "Customer", .type = COL_TYPE_FK_SELECT,
      .nullable = true, .fk_table = "customers",
      .fk_label = "COALESCE((SELECT name FROM users WHERE users.customer_id "
                  "= customers.id AND users.deleted_at IS NULL LIMIT 1), id)" },
    { .name = "created_at", .label = "Activity", .type = COL_TYPE_DATE,
      .nullable = false, .computed = 1, .cell = &usr_ts_cell },
};
const size_t md_pos_users_columns_count = ARRAY_LEN(md_pos_users_columns);

static bool create_pos_user(db_t *db, String_View *fields, size_t count) {
    if (count < 10) return false;
    // Plain INSERT (not OR REPLACE): a colliding username/phone must fail
    // loudly instead of silently dropping the other row. Field order is
    // usr_fields {phone, name, username, email, profile_pic} then
    // usr_opt_fields {status, customer_id, org_unit_id, location_id,
    // user_type_dict_id}. phone (values[0]) must be non-empty - the
    // SERVE_* 400 guard enforces it. email/profile_pic/fks store NULL
    // when blank (0007: email is optional, only filled on deliberate
    // account creation); status/user_type fall back to their defaults.
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO users "
                         "(phone, name, username, email, profile_pic, "
                          "status, customer_id, org_unit_id, location_id, "
                          "user_type_dict_id) "
                         "VALUES (?, ?, ?, NULLIF(?, ''), NULLIF(?, ''), "
                          "COALESCE(NULLIF(?, ''), 'ACTIVE'), "
                          "NULLIF(?, ''), NULLIF(?, ''), NULLIF(?, ''), "
                          "COALESCE(NULLIF(?, ''), 'CUSTOMER'));",
        [SQL_MYSQL]    = "INSERT INTO users "
                         "(phone, name, username, email, profile_pic, "
                          "status, customer_id, org_unit_id, location_id, "
                          "user_type_dict_id) "
                         "VALUES (?, ?, ?, NULLIF(?, ''), NULLIF(?, ''), "
                          "COALESCE(NULLIF(?, ''), 'ACTIVE'), "
                          "NULLIF(?, ''), NULLIF(?, ''), NULLIF(?, ''), "
                          "COALESCE(NULLIF(?, ''), 'CUSTOMER'));",
        [SQL_POSTGRES] = "INSERT INTO users "
                         "(phone, name, username, email, profile_pic, "
                          "status, customer_id, org_unit_id, location_id, "
                          "user_type_dict_id) "
                         "VALUES ($1, $2, $3, NULLIF($4, ''), NULLIF($5, ''), "
                          "COALESCE(NULLIF($6, ''), 'ACTIVE'), "
                          "NULLIF($7, ''), NULLIF($8, ''), NULLIF($9, ''), "
                          "COALESCE(NULLIF($10, ''), 'CUSTOMER'));",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt))    return_defer(false);
    for (int i = 1; i <= 10; ++i) {
        if (!sql_bind(&stmt, i, SQL_SV(fields[i - 1]))) return_defer(false);
    }
    if (!sql_final_step(&stmt))                  return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool update_pos_user(db_t *db, String_View *fields, size_t count,
                            String_View id)
{
    if (count < 10) return false;
    // Empty phone/profile_pic/status/fks keep the stored one (same
    // COALESCE rule as /users): a child edit form carries only the shared
    // columns, its fk rides the query string, while a master edit sends
    // both. email is the one contact an edit may clear - it stores NULL
    // (0007: optional, only filled on deliberate account creation).
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE users SET phone = COALESCE(NULLIF(?, ''), phone), "
                         "name = ?, username = ?, email = NULLIF(?, ''), "
                         "profile_pic = COALESCE(NULLIF(?, ''), profile_pic), "
                         "status = COALESCE(NULLIF(?, ''), status), "
                         "customer_id = COALESCE(NULLIF(?, ''), customer_id), "
                         "org_unit_id = COALESCE(NULLIF(?, ''), org_unit_id), "
                         "location_id = COALESCE(NULLIF(?, ''), location_id), "
                         "user_type_dict_id = COALESCE(NULLIF(?, ''), user_type_dict_id) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE users SET phone = COALESCE(NULLIF(?, ''), phone), "
                         "name = ?, username = ?, email = NULLIF(?, ''), "
                         "profile_pic = COALESCE(NULLIF(?, ''), profile_pic), "
                         "status = COALESCE(NULLIF(?, ''), status), "
                         "customer_id = COALESCE(NULLIF(?, ''), customer_id), "
                         "org_unit_id = COALESCE(NULLIF(?, ''), org_unit_id), "
                         "location_id = COALESCE(NULLIF(?, ''), location_id), "
                         "user_type_dict_id = COALESCE(NULLIF(?, ''), user_type_dict_id) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE users SET phone = COALESCE(NULLIF($1, ''), phone), "
                         "name = $2, username = $3, email = NULLIF($4, ''), "
                         "profile_pic = COALESCE(NULLIF($5, ''), profile_pic), "
                         "status = COALESCE(NULLIF($6, ''), status), "
                         "customer_id = COALESCE(NULLIF($7, ''), customer_id), "
                         "org_unit_id = COALESCE(NULLIF($8, ''), org_unit_id), "
                         "location_id = COALESCE(NULLIF($9, ''), location_id), "
                         "user_type_dict_id = COALESCE(NULLIF($10, ''), user_type_dict_id) "
                         "WHERE id = $11;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt))    return_defer(false);
    for (int i = 1; i <= 10; ++i) {
        if (!sql_bind(&stmt, i, SQL_SV(fields[i - 1]))) return_defer(false);
    }
    if (!sql_bind(&stmt, 11, SQL_SV(id)))        return_defer(false);
    if (!sql_final_step(&stmt))                  return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

// Rows referenced by roles/staff hit FK RESTRICT on a hard delete; with
// soft_delete the UPDATE only stamps deleted_at (cascade trigger covers
// user_roles/system_alerts/customer_interactions).
static bool soft_delete_pos_user(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE users "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE users SET deleted_at = NOW() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE users SET deleted_at = now() WHERE id = $1;",
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

static bool restore_pos_user(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE users SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE users SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE users SET deleted_at = NULL WHERE id = $1;",
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

// role links under a user: role_id FK + link timestamp.
static MD_Column md_pos_user_role_columns[] = {
    { .name = "role_id", .label = "Role", .type = COL_TYPE_FK_SELECT,
      .nullable = false, .fk_table = "roles", .fk_label = "role_name" },
    { .name = "created_at", .label = "Linked", .type = COL_TYPE_DATE,
      .nullable = false },
};
static const size_t md_pos_user_role_columns_count =
    ARRAY_LEN(md_pos_user_role_columns);

void serve_pos_users(Serve_Context *sc) {
    // Children: the editable relations of an account (role links). Staff
    // is a master table - it renders only on /pos/staff, never as another
    // table's child (Phase 13).
    MD_ChildTab children[] = {
        {
            .table        = "user_roles",
            .title        = "Roles",
            .fk_column    = "user_id",
            .id_column    = "id",
            .crud_path    = "/pos/user_roles",
            .columns      = md_pos_user_role_columns,
            .column_count = md_pos_user_role_columns_count,
            .soft_delete  = 1,
        },
    };
    MD_MasterConfig config = {
        .table          = "users",
        .title          = "Users",
        .id_column      = "id",
        .crud_path      = "/pos/users",
        .columns        = md_pos_users_columns,
        .column_count   = md_pos_users_columns_count,
        .children       = children,
        .children_count = ARRAY_LEN(children),
        .soft_delete    = 1,
    };
    serve_master_child(sc, &config);
}

// Required keys first: phone MUST be present and non-empty (values[0]
// drives the SERVE_* 400), then the identity fields the avatar cell and
// email input always send. Optional keys ride body first, query second -
// the /pos/customers and /pos/org child tabs pass their fk in the query
// string; status/location/user_type default when absent (see SQL above).
static const char *usr_fields[] = {
    "phone", "name", "username", "email", "profile_pic",
};
static const char *usr_opt_fields[] = {
    "status", "customer_id", "org_unit_id", "location_id", "user_type_dict_id",
};
SERVE_CREATE(pos_users, pos_user, usr_fields, usr_opt_fields)
SERVE_UPDATE(pos_users, pos_user, usr_fields, usr_opt_fields)
SERVE_SOFT_DELETE(pos_users, pos_user)

// ---- /pos/dictionaries (read_only showcase) -----------------------------
// Seeded reference data: the page is a pure view -- no create form, no
// edit/delete UI, and the module registers only the GET route.

MD_Column md_dictionaries_columns[] = {
    { .name = "category",   .label = "Category",   .type = COL_TYPE_TEXT, .nullable = false },
    { .name = "code",       .label = "Code",       .type = COL_TYPE_TEXT, .nullable = false },
    { .name = "label",      .label = "Label",      .type = COL_TYPE_TEXT, .nullable = false },
    { .name = "sort_order", .label = "Sort",       .type = COL_TYPE_NUM,  .nullable = false },
};
const size_t md_dictionaries_columns_count = ARRAY_LEN(md_dictionaries_columns);

void serve_pos_dictionaries(Serve_Context *sc) {
    MD_MasterConfig config = {
        .table          = "dictionaries",
        .title          = "Dictionaries",
        .id_column      = "id",
        .crud_path      = "/pos/dictionaries",  /* never used: no write routes */
        .columns        = md_dictionaries_columns,
        .column_count   = md_dictionaries_columns_count,
        .read_only      = 1,
    };
    serve_master_child(sc, &config);
}
