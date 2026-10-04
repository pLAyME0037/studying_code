#include "config.h"

#include <string.h>

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "core/http/utils.h"

// =========================================================================
// /pos/config: system_configs master-only. created_at is the
// real-but-unbound opt slot (SERVE_* needs a non-empty opt array under
// -pedantic); it rides its column default.
// =========================================================================

MD_Column md_configs_columns[] = {
    { .name = "config_key", .label = "Key", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "config_value", .label = "Value", .type = COL_TYPE_TEXT,
      .nullable = true },
    { .name = "json_payload", .label = "JSON", .type = COL_TYPE_TEXT,
      .nullable = true },
    { .name = "is_encrypted", .label = "Encrypted", .type = COL_TYPE_NUM,
      .nullable = false },
    { .name = "created_at", .label = "Since", .type = COL_TYPE_DATE,
      .nullable = false },
};
const size_t md_configs_columns_count = ARRAY_LEN(md_configs_columns);

// DB mutations only -- list loading lives in the master_child engine.
static bool create_system_config(db_t *db, String_View *fields, size_t count) {
    if (count < 5) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO system_configs (config_key, "
                         "config_value, json_payload, is_encrypted) "
                         "VALUES (?, ?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO system_configs (config_key, "
                         "config_value, json_payload, is_encrypted) "
                         "VALUES (?, ?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO system_configs (config_key, "
                         "config_value, json_payload, is_encrypted) "
                         "VALUES ($1, $2, $3, $4);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt))   return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(fields[0])))   return_defer(false);  // key
    if (!sql_bind(&stmt, 2, pos_sv(fields[1])))   return_defer(false);  // value
    if (!sql_bind(&stmt, 3, pos_sv(fields[2])))   return_defer(false);  // json
    if (!sql_bind(&stmt, 4, pos_num(fields[3])))  return_defer(false);  // flag
    if (!sql_final_step(&stmt))                   return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool update_system_config(db_t *db, String_View *fields, size_t count,
                                 String_View id)
{
    if (count < 5) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE system_configs SET "
                         "config_key = COALESCE(NULLIF(?, ''), config_key), "
                         "config_value = COALESCE(NULLIF(?, ''), config_value), "
                         "json_payload = COALESCE(NULLIF(?, ''), json_payload), "
                         "is_encrypted = COALESCE(NULLIF(?, ''), is_encrypted) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE system_configs SET "
                         "config_key = COALESCE(NULLIF(?, ''), config_key), "
                         "config_value = COALESCE(NULLIF(?, ''), config_value), "
                         "json_payload = COALESCE(NULLIF(?, ''), json_payload), "
                         "is_encrypted = COALESCE(NULLIF(?, ''), is_encrypted) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE system_configs SET "
                         "config_key = COALESCE(NULLIF($1, ''), config_key), "
                         "config_value = COALESCE(NULLIF($2, ''), config_value), "
                         "json_payload = COALESCE(NULLIF($3, ''), json_payload), "
                         "is_encrypted = COALESCE(NULLIF($4, ''), is_encrypted) "
                         "WHERE id = $5;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt))   return_defer(false);
    for (int i = 1; i <= 4; ++i) {
        if (!sql_bind(&stmt, i, SQL_SV(fields[i - 1]))) return_defer(false);
    }
    if (!sql_bind(&stmt, 5, SQL_SV(id)))        return_defer(false);
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool soft_delete_system_config(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE system_configs "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE system_configs SET deleted_at = NOW() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE system_configs SET deleted_at = now() WHERE id = $1;",
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

static bool restore_system_config(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE system_configs SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE system_configs SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE system_configs SET deleted_at = NULL WHERE id = $1;",
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

void serve_pos_config(Serve_Context *sc) {
    MD_MasterConfig config = {
        .table          = "system_configs",
        .title          = "Configs",
        .id_column      = "id",
        .crud_path      = "/pos/config",
        .columns        = md_configs_columns,
        .column_count   = md_configs_columns_count,
        .soft_delete    = 1,
    };
    serve_master_child(sc, &config);
}

static const char *cfg_fields[] = {
    "config_key", "config_value", "json_payload", "is_encrypted",
};
static const char *cfg_opt_fields[] = { "created_at" };
SERVE_CREATE(pos_config, system_config, cfg_fields, cfg_opt_fields)
SERVE_UPDATE(pos_config, system_config, cfg_fields, cfg_opt_fields)
SERVE_SOFT_DELETE(pos_config, system_config)
