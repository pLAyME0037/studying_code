#include "geo.h"

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "core/http/utils.h"

// =========================================================================
// Phase 16 geo redo: Cambodia admin areas as four individual masters,
// seeded by migration 0009_geo_admin from the cam-geo locations.json
// export (44 provinces, 227 districts, 1,712 communes, 14,858 villages).
// Provinces/districts/communes are editable with the standard soft-delete
// + trash flow; villages is a read_only catalog - the windowed list
// browses it, while inline editors would have to carry the full
// 1,712-option commune select on every row.
// =========================================================================

// ---- /pos/provinces -----------------------------------------------------

MD_Column md_provinces_columns[] = {
    { .name = "prov_id",  .label = "Code",         .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "name_kh",  .label = "Khmer Name",   .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "name_en",  .label = "English Name", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "created_at", .label = "Created", .type = COL_TYPE_DATE,
      .nullable = false, .computed = 1 },
};
const size_t md_provinces_columns_count = ARRAY_LEN(md_provinces_columns);

static bool create_province(db_t *db, String_View *fields, size_t count) {
    if (count < 3) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO provinces (prov_id, name_kh, name_en) "
                         "VALUES (?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO provinces (prov_id, name_kh, name_en) "
                         "VALUES (?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO provinces (prov_id, name_kh, name_en) "
                         "VALUES ($1, $2, $3);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    for (int i = 1; i <= 3; ++i) {
        if (!sql_bind(&stmt, i, SQL_SV(fields[i - 1]))) return_defer(false);
    }
    if (!sql_final_step(&stmt)) return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool update_province(db_t *db, String_View *fields, size_t count,
                            String_View id)
{
    if (count < 3) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE provinces SET "
                         "prov_id = COALESCE(NULLIF(?, ''), prov_id), "
                         "name_kh = COALESCE(NULLIF(?, ''), name_kh), "
                         "name_en = COALESCE(NULLIF(?, ''), name_en) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE provinces SET "
                         "prov_id = COALESCE(NULLIF(?, ''), prov_id), "
                         "name_kh = COALESCE(NULLIF(?, ''), name_kh), "
                         "name_en = COALESCE(NULLIF(?, ''), name_en) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE provinces SET "
                         "prov_id = COALESCE(NULLIF($1, ''), prov_id), "
                         "name_kh = COALESCE(NULLIF($2, ''), name_kh), "
                         "name_en = COALESCE(NULLIF($3, ''), name_en) "
                         "WHERE id = $4;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt))    return_defer(false);
    for (int i = 1; i <= 3; ++i) {
        if (!sql_bind(&stmt, i, SQL_SV(fields[i - 1]))) return_defer(false);
    }
    if (!sql_bind(&stmt, 4, SQL_SV(id)))         return_defer(false);
    if (!sql_final_step(&stmt))                  return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool soft_delete_province(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE provinces "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE provinces SET deleted_at = UTC_TIMESTAMP() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE provinces SET deleted_at = now() WHERE id = $1;",
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

static bool restore_province(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE provinces SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE provinces SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE provinces SET deleted_at = NULL WHERE id = $1;",
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

void serve_pos_provinces(Serve_Context *sc) {
    MD_MasterConfig config = {
        .table          = "provinces",
        .title          = "Provinces",
        .id_column      = "id",
        .crud_path      = "/pos/provinces",
        .columns        = md_provinces_columns,
        .column_count   = md_provinces_columns_count,
        .soft_delete    = 1,
    };
    serve_master_child(sc, &config);
}

static const char *prov_fields[]     = { "prov_id", "name_kh", "name_en" };
static const char *prov_opt_fields[] = { "created_at" };
SERVE_CREATE(pos_provinces, province, prov_fields, prov_opt_fields)
SERVE_UPDATE(pos_provinces, province, prov_fields, prov_opt_fields)
SERVE_SOFT_DELETE(pos_provinces, province)

// ---- /pos/districts -----------------------------------------------------

MD_Column md_districts_columns[] = {
    { .name = "dist_id",  .label = "Code",         .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "name_kh",  .label = "Khmer Name",   .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "name_en",  .label = "English Name", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "type",     .label = "Type",         .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "province_id", .label = "Province", .type = COL_TYPE_FK_SELECT,
      .nullable = false, .fk_table = "provinces", .fk_label = "name_kh" },
    { .name = "created_at", .label = "Created", .type = COL_TYPE_DATE,
      .nullable = false, .computed = 1 },
};
const size_t md_districts_columns_count = ARRAY_LEN(md_districts_columns);

static bool create_district(db_t *db, String_View *fields, size_t count) {
    if (count < 5) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO districts "
                         "(dist_id, name_kh, name_en, type, province_id) "
                         "VALUES (?, ?, ?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO districts "
                         "(dist_id, name_kh, name_en, type, province_id) "
                         "VALUES (?, ?, ?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO districts "
                         "(dist_id, name_kh, name_en, type, province_id) "
                         "VALUES ($1, $2, $3, $4, $5);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    for (int i = 1; i <= 5; ++i) {
        if (!sql_bind(&stmt, i, SQL_SV(fields[i - 1]))) return_defer(false);
    }
    if (!sql_final_step(&stmt)) return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool update_district(db_t *db, String_View *fields, size_t count,
                            String_View id)
{
    if (count < 5) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE districts SET "
                         "dist_id = COALESCE(NULLIF(?, ''), dist_id), "
                         "name_kh = COALESCE(NULLIF(?, ''), name_kh), "
                         "name_en = COALESCE(NULLIF(?, ''), name_en), "
                         "type = COALESCE(NULLIF(?, ''), type), "
                         "province_id = COALESCE(NULLIF(?, ''), province_id) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE districts SET "
                         "dist_id = COALESCE(NULLIF(?, ''), dist_id), "
                         "name_kh = COALESCE(NULLIF(?, ''), name_kh), "
                         "name_en = COALESCE(NULLIF(?, ''), name_en), "
                         "type = COALESCE(NULLIF(?, ''), type), "
                         "province_id = COALESCE(NULLIF(?, ''), province_id) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE districts SET "
                         "dist_id = COALESCE(NULLIF($1, ''), dist_id), "
                         "name_kh = COALESCE(NULLIF($2, ''), name_kh), "
                         "name_en = COALESCE(NULLIF($3, ''), name_en), "
                         "type = COALESCE(NULLIF($4, ''), type), "
                         "province_id = COALESCE(NULLIF($5, ''), province_id) "
                         "WHERE id = $6;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt))    return_defer(false);
    for (int i = 1; i <= 5; ++i) {
        if (!sql_bind(&stmt, i, SQL_SV(fields[i - 1]))) return_defer(false);
    }
    if (!sql_bind(&stmt, 6, SQL_SV(id)))         return_defer(false);
    if (!sql_final_step(&stmt))                  return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool soft_delete_district(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE districts "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE districts SET deleted_at = UTC_TIMESTAMP() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE districts SET deleted_at = now() WHERE id = $1;",
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

static bool restore_district(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE districts SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE districts SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE districts SET deleted_at = NULL WHERE id = $1;",
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

void serve_pos_districts(Serve_Context *sc) {
    MD_MasterConfig config = {
        .table          = "districts",
        .title          = "Districts",
        .id_column      = "id",
        .crud_path      = "/pos/districts",
        .columns        = md_districts_columns,
        .column_count   = md_districts_columns_count,
        .soft_delete    = 1,
    };
    serve_master_child(sc, &config);
}

static const char *dist_fields[]     = { "dist_id", "name_kh", "name_en",
                                         "type" };
static const char *dist_opt_fields[] = { "province_id" };
SERVE_CREATE(pos_districts, district, dist_fields, dist_opt_fields)
SERVE_UPDATE(pos_districts, district, dist_fields, dist_opt_fields)
SERVE_SOFT_DELETE(pos_districts, district)

// ---- /pos/communes ------------------------------------------------------

MD_Column md_communes_columns[] = {
    { .name = "comm_id",  .label = "Code",         .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "name_kh",  .label = "Khmer Name",   .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "name_en",  .label = "English Name", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "district_id", .label = "District", .type = COL_TYPE_FK_SELECT,
      .nullable = false, .fk_table = "districts", .fk_label = "name_kh" },
    { .name = "created_at", .label = "Created", .type = COL_TYPE_DATE,
      .nullable = false, .computed = 1 },
};
const size_t md_communes_columns_count = ARRAY_LEN(md_communes_columns);

static bool create_commune(db_t *db, String_View *fields, size_t count) {
    if (count < 4) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO communes (comm_id, name_kh, name_en, "
                         "district_id) VALUES (?, ?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO communes (comm_id, name_kh, name_en, "
                         "district_id) VALUES (?, ?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO communes (comm_id, name_kh, name_en, "
                         "district_id) VALUES ($1, $2, $3, $4);",
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

static bool update_commune(db_t *db, String_View *fields, size_t count,
                           String_View id)
{
    if (count < 4) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE communes SET "
                         "comm_id = COALESCE(NULLIF(?, ''), comm_id), "
                         "name_kh = COALESCE(NULLIF(?, ''), name_kh), "
                         "name_en = COALESCE(NULLIF(?, ''), name_en), "
                         "district_id = COALESCE(NULLIF(?, ''), district_id) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE communes SET "
                         "comm_id = COALESCE(NULLIF(?, ''), comm_id), "
                         "name_kh = COALESCE(NULLIF(?, ''), name_kh), "
                         "name_en = COALESCE(NULLIF(?, ''), name_en), "
                         "district_id = COALESCE(NULLIF(?, ''), district_id) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE communes SET "
                         "comm_id = COALESCE(NULLIF($1, ''), comm_id), "
                         "name_kh = COALESCE(NULLIF($2, ''), name_kh), "
                         "name_en = COALESCE(NULLIF($3, ''), name_en), "
                         "district_id = COALESCE(NULLIF($4, ''), district_id) "
                         "WHERE id = $5;",
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

static bool soft_delete_commune(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE communes "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE communes SET deleted_at = UTC_TIMESTAMP() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE communes SET deleted_at = now() WHERE id = $1;",
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

static bool restore_commune(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE communes SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE communes SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE communes SET deleted_at = NULL WHERE id = $1;",
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

void serve_pos_communes(Serve_Context *sc) {
    MD_MasterConfig config = {
        .table          = "communes",
        .title          = "Communes",
        .id_column      = "id",
        .crud_path      = "/pos/communes",
        .columns        = md_communes_columns,
        .column_count   = md_communes_columns_count,
        .soft_delete    = 1,
    };
    serve_master_child(sc, &config);
}

static const char *comm_fields[]     = { "comm_id", "name_kh", "name_en" };
static const char *comm_opt_fields[] = { "district_id" };
SERVE_CREATE(pos_communes, commune, comm_fields, comm_opt_fields)
SERVE_UPDATE(pos_communes, commune, comm_fields, comm_opt_fields)
SERVE_SOFT_DELETE(pos_communes, commune)

// ---- /pos/villages (read_only catalog) ----------------------------------
// 14,858 seeded rows: browse + search the windowed list only, the module
// registers the GET route alone (same contract as /pos/dictionaries).

MD_Column md_villages_columns[] = {
    { .name = "vill_id",  .label = "Code",         .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "name_kh",  .label = "Khmer Name",   .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "name_en",  .label = "English Name", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "is_not_active", .label = "Inactive", .type = COL_TYPE_TEXT,
      .nullable = true },
    { .name = "commune_id", .label = "Commune", .type = COL_TYPE_FK_SELECT,
      .nullable = false, .fk_table = "communes", .fk_label = "name_kh" },
    { .name = "created_at", .label = "Created", .type = COL_TYPE_DATE,
      .nullable = false, .computed = 1 },
};
const size_t md_villages_columns_count = ARRAY_LEN(md_villages_columns);

void serve_pos_villages(Serve_Context *sc) {
    MD_MasterConfig config = {
        .table          = "villages",
        .title          = "Villages",
        .id_column      = "id",
        .crud_path      = "/pos/villages",
        .columns        = md_villages_columns,
        .column_count   = md_villages_columns_count,
        .read_only      = 1,
    };
    serve_master_child(sc, &config);
}
