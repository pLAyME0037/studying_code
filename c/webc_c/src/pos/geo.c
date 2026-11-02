#include "geo.h"

#include "src/layout/master_child.h"
#include "src/core/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "src/http/utils.h"

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
    {
        .name     = "prov_id",
        .label    = "Code",
        .type     = COL_TYPE_TEXT,
        .nullable = false
    }, {
        .name     = "name_kh",
        .label    = "Khmer Name",
        .type     = COL_TYPE_TEXT,
        .nullable = false
    }, {
        .name     = "name_en",
        .label    = "English Name",
        .type     = COL_TYPE_TEXT,
        .nullable = false
    }, {
        .name     = "created_at",
        .label    = "Created",
        .type     = COL_TYPE_DATE,
        .nullable = false,
        .computed = 1
    },
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
    {
        .name     = "dist_id",
        .label    = "Code",
        .type     = COL_TYPE_TEXT,
        .nullable = false
    }, {
        .name     = "name_kh",
        .label    = "Khmer Name",
        .type     = COL_TYPE_TEXT,
        .nullable = false
    }, {
        .name     = "name_en",
        .label    = "English Name",
        .type     = COL_TYPE_TEXT,
        .nullable = false
    }, {
        .name     = "type",
        .label    = "Type",
        .type     = COL_TYPE_TEXT,
        .nullable = false
    }, {
        .name     = "province_id",
        .label    = "Province",
        .type     = COL_TYPE_FK_SELECT,
        .nullable = false,
        .fk_table = "provinces",
        .fk_label = "name_kh"
    }, {
        .name     = "created_at",
        .label    = "Created",
        .type     = COL_TYPE_DATE,
        .nullable = false,
        .computed = 1
    },
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

static const char *dist_fields[] = {
    "dist_id", "name_kh", "name_en", "type"
};
static const char *dist_opt_fields[] = { "province_id" };
SERVE_CREATE(pos_districts, district, dist_fields, dist_opt_fields)
SERVE_UPDATE(pos_districts, district, dist_fields, dist_opt_fields)
SERVE_SOFT_DELETE(pos_districts, district)

// ---- /pos/communes ------------------------------------------------------

MD_Column md_communes_columns[] = {
    {
        .name     = "comm_id",
        .label    = "Code",
        .type     = COL_TYPE_TEXT,
        .nullable = false
    }, {
        .name     = "name_kh",
        .label    = "Khmer Name",
        .type     = COL_TYPE_TEXT,
        .nullable = false
    }, {
        .name     = "name_en",
        .label    = "English Name",
        .type     = COL_TYPE_TEXT,
        .nullable = false
    }, {
        .name     = "district_id",
        .label    = "District",
        .type     = COL_TYPE_FK_SELECT,
        .nullable = false,
        .fk_table = "districts",
        .fk_label = "name_kh"
    }, {
        .name     = "created_at",
        .label    = "Created",
        .type     = COL_TYPE_DATE,
        .nullable = false,
        .computed = 1
    },
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
    {
        .name     = "vill_id",
        .label    = "Code",
        .type     = COL_TYPE_TEXT,
        .nullable = false
    }, {
        .name     = "name_kh",
        .label    = "Khmer Name",
        .type     = COL_TYPE_TEXT,
        .nullable = false
    }, {
        .name     = "name_en",
        .label    = "English Name",
        .type     = COL_TYPE_TEXT,
        .nullable = false
    }, {
        .name     = "is_not_active",
        .label    = "Inactive",
        .type     = COL_TYPE_TEXT,
        .nullable = true
    }, {
        .name     = "commune_id",
        .label    = "Commune",
        .type     = COL_TYPE_FK_SELECT,
        .nullable = false,
        .fk_table = "communes",
        .fk_label = "name_kh"
    }, {
        .name     = "created_at",
        .label    = "Created",
        .type     = COL_TYPE_DATE,
        .nullable = false,
        .computed = 1
    },
};
const size_t md_villages_columns_count = ARRAY_LEN(md_villages_columns);

void serve_pos_villages(Serve_Context *sc) {
    MD_MasterConfig config = {
        .table        = "villages",
        .title        = "Villages",
        .id_column    = "id",
        .crud_path    = "/pos/villages",
        .columns      = md_villages_columns,
        .column_count = md_villages_columns_count,
        .read_only    = 1,
    };
    serve_master_child(sc, &config);
}

// =========================================================================
// Geo cascade: province -> district -> commune -> village
// =========================================================================
// A person lives in ONE village, but choosing it means walking down three
// levels. Rendering all 14,858 villages into one <select> costs ~870 KB of
// HTML and makes the browser lay out every option; the cascade keeps the
// widest list at 48 (the largest commune) and the typical one at ~9.
//
// The two data sources are split by size on purpose:
//   * the ANCESTORS (44 + 227 + 1,712 rows) embed once per form as compact
//     JSON, so province/district/commune filtering is instant, no round-trip;
//   * VILLAGES (14,858) are fetched per chosen commune from
//     serve_geo_villages_options below - one small response.
// The three ancestor selects are UI-only navigation: they are never
// posted. The form still carries exactly one field, village_id.

// Minimal JSON string escape. Khmer names are plain UTF-8 and need no
// escaping, but a name could still carry a quote or a backslash.
static void geo_json_str(String_Builder *sb, const char *s) {
    sb_append_cstr(sb, "\"");
    for (const char *p = s ? s : ""; *p; ++p) {
        if (*p == '"' || *p == '\\') {
            char esc[2] = { '\\', *p };
            sb_append_buf(sb, esc, 2);
        } else if ((unsigned char)*p < 0x20) {
            char esc[8];
            int n = snprintf(esc, sizeof esc, "\\u%04x", *p);
            sb_append_buf(sb, esc, (size_t)n);
        } else {
            sb_append_buf(sb, p, 1);
        }
    }
    sb_append_cstr(sb, "\"");
}

// One level as [["<id>","<parent_id>","<name_kh>"], ...]. A NULL
// parent_col emits "" so every row stays a 3-tuple.
static bool geo_cascade_level(db_t *db, String_Builder *sb,
                              const char *table, const char *parent_col) {
    char *sql = temp_sprintf(
        "SELECT id, %s, name_kh FROM %s WHERE deleted_at IS NULL "
        "ORDER BY id ASC;", parent_col ? parent_col : "''", table);
    sql_stmt stmt = {0};
    if (!sql_prepare(db, sql, &stmt)) return false;
    sb_append_cstr(sb, "[");
    Sql_Step ret;
    bool first = true, ok = true;
    for (ret = sql_step(&stmt); ret == SQL_ROW; ret = sql_step(&stmt)) {
        if (!first) sb_append_cstr(sb, ",");
        first = false;
        sb_append_cstr(sb, "[");
        geo_json_str(sb, sql_column_text(&stmt, 0));
        sb_append_cstr(sb, ",");
        geo_json_str(sb, sql_column_text(&stmt, 1));
        sb_append_cstr(sb, ",");
        geo_json_str(sb, sql_column_text(&stmt, 2));
        sb_append_cstr(sb, "]");
    }
    if (ret != SQL_DONE) ok = false;
    sql_finalize(&stmt);
    sb_append_cstr(sb, "]");
    return ok;
}

/* Compact dataset for GeoCascade.js. Returns NULL if any query fails so a
 * caller can fall back to the plain single-select markup. */
/* The villages already chosen on THIS page, as {"<village_id>":"<commune_id>"}.
 * The ancestor arrays cover provinces/districts/communes only - shipping all
 * 14,858 villages would cost ~870 KB - so an edit form has no way to walk a
 * stored village back up to its province on its own. Twenty rows need at most
 * twenty tiny lookups, done here in one pass. */
static bool geo_cascade_chosen(db_t *db, String_Builder *sb,
                               const MD_MasterRows *rows,
                               const MD_MasterConfig *config) {
    size_t leaf_ci = config->column_count;
    for (size_t ci = 0; ci < config->column_count; ++ci) {
        if (config->columns[ci].fk_cascade) { leaf_ci = ci; break; }
    }
    if (leaf_ci == config->column_count) { sb_append_cstr(sb, "{}"); return true; }
    size_t slot = md_col_slot(config->columns, leaf_ci);

    sb_append_cstr(sb, "{");
    bool first = true, ok = true;
    for (size_t ri = 0; ri < rows->count && ok; ++ri) {
        const char *vill = rows->items[ri].values[slot];
        if (!vill || !*vill) continue;
        sql_stmt stmt = {0};
        if (!sql_prepare(db, "SELECT commune_id FROM villages WHERE id = ?;",
                         &stmt)) return false;
        bool bound = sql_bind(&stmt, 1, SQL_SV(sv_from_cstr(vill)));
        Sql_Step ret = bound ? sql_step(&stmt) : SQL_ERROR;
        if (ret == SQL_ROW) {
            if (!first) sb_append_cstr(sb, ",");
            first = false;
            geo_json_str(sb, vill);   /* quotes + escapes the key too */
            sb_append_cstr(sb, ":");
            geo_json_str(sb, sql_column_text(&stmt, 0));
        } else if (ret != SQL_DONE) {
            ok = false;
        }
        sql_finalize(&stmt);
    }
    sb_append_cstr(sb, "}");
    return ok;
}

const char *md_cascade_json(db_t *db, const MD_MasterRows *rows,
                            const MD_MasterConfig *config) {
    if (!db) return NULL;
    String_Builder sb = {0};
    bool ok = true;
    sb_append_cstr(&sb, "{\"p\":");
    ok = ok && geo_cascade_level(db, &sb, "provinces", NULL);
    sb_append_cstr(&sb, ",\"d\":");
    ok = ok && geo_cascade_level(db, &sb, "districts", "province_id");
    sb_append_cstr(&sb, ",\"c\":");
    ok = ok && geo_cascade_level(db, &sb, "communes", "district_id");
    sb_append_cstr(&sb, ",\"s\":");
    ok = ok && (rows != NULL) && (config != NULL)
           && geo_cascade_chosen(db, &sb, rows, config);
    sb_append_cstr(&sb, "}");
    if (!ok) { sb_free(sb); return NULL; }
    // A String_Builder is NOT NUL-terminated (nob.h:679). temp_strdup()
    // takes a cstr, so without this it reads past sb.count into whatever
    // heap follows - which here held the migration script text, injecting
    // raw "INSERT INTO ..." bytes into the page and breaking UTF-8.
    sb_append_null(&sb);
    return temp_strdup(sb.items ? sb.items
                                : "{\"p\":[],\"d\":[],\"c\":[],\"s\":{}}");
}

/* GET /geo/villages?commune=<id> -- the lazy half of the cascade. Answers
 * bare <option> lines for one commune so the caller drops them straight
 * into the village select. A missing or over-long commune answers an empty
 * list rather than an error: a half-built form just shows no villages. */
void serve_geo_villages_options(Serve_Context *sc) {
    String_View commune = sv_from_cstr("");
    form_find(sc->query_string, "commune", &commune);
    if (commune.count == 0 || commune.count > 32) {
        http_render_response(sc, 200, "text/html; charset=utf-8",
                             sv_from_cstr(""));
        return;
    }
    db_t *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }

    String_Builder sb = {0};
    sql_stmt stmt = {0};
    // Bound, never interpolated: the commune id comes off the URL.
    if (!sql_prepare(db, "SELECT id, name_kh FROM villages "
                         "WHERE commune_id = ? AND deleted_at IS NULL "
                         "ORDER BY name_kh ASC;", &stmt)) {
        db_close(db);
        serve_error(sc, 500);
        return;
    }
    bool bound = sql_bind(&stmt, 1, SQL_SV(commune));
    Sql_Step ret = bound ? sql_step(&stmt) : SQL_ERROR;
    for (; ret == SQL_ROW; ret = sql_step(&stmt)) {
        sb_append_cstr(&sb, "<option value=\"");
        sb_append_html_escaped(&sb, sql_column_text(&stmt, 0));
        sb_append_cstr(&sb, "\">");
        sb_append_html_escaped(&sb, sql_column_text(&stmt, 1));
        sb_append_cstr(&sb, "</option>\n");
    }
    sql_finalize(&stmt);
    db_close(db);
    if (!bound) { sb_free(sb); serve_error(sc, 500); return; }
    http_render_response(sc, 200, "text/html; charset=utf-8", sb_to_sv(sb));
}
