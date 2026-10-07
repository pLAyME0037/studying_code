#include "i18n.h"

#include <string.h>

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "core/http/utils.h"

// =========================================================================
// /pos/i18n: code + name + is_default/is_active (0/1 numerics; empty
// input -> 0 via pos_num). created_at rides its column default (not a
// form key). Child: translations (fk language_id via query).
// =========================================================================

MD_Column md_languages_columns[] = {
    { .name = "code", .label = "Code", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "name", .label = "Name", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "is_default", .label = "Default", .type = COL_TYPE_NUM,
      .nullable = false },
    { .name = "is_active", .label = "Active", .type = COL_TYPE_NUM,
      .nullable = false },
};
const size_t md_languages_columns_count = ARRAY_LEN(md_languages_columns);

// Child shape: key/value pairs under a language.
static MD_Column md_translation_columns[] = {
    { .name = "trans_key", .label = "Key", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "trans_value", .label = "Value", .type = COL_TYPE_TEXT,
      .nullable = false },
};
static const size_t md_translation_columns_count =
    ARRAY_LEN(md_translation_columns);

// DB mutations only -- list loading lives in the master_child engine.
static bool create_language(db_t *db, String_View *fields, size_t count) {
    if (count < 4) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO languages (code, name, is_default, "
                         "is_active) VALUES (?, ?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO languages (code, name, is_default, "
                         "is_active) VALUES (?, ?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO languages (code, name, is_default, "
                         "is_active) VALUES ($1, $2, $3, $4);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt))  return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(fields[0])))  return_defer(false);  // code
    if (!sql_bind(&stmt, 2, SQL_SV(fields[1])))  return_defer(false);  // name
    if (!sql_bind(&stmt, 3, pos_num(fields[2]))) return_defer(false);  // default
    if (!sql_bind(&stmt, 4, pos_num(fields[3]))) return_defer(false);  // active
    if (!sql_final_step(&stmt))                  return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool update_language(db_t *db, String_View *fields, size_t count,
                            String_View id)
{
    if (count < 4) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE languages SET "
                         "code = COALESCE(NULLIF(?, ''), code), "
                         "name = COALESCE(NULLIF(?, ''), name), "
                         "is_default = COALESCE(NULLIF(?, ''), is_default), "
                         "is_active = COALESCE(NULLIF(?, ''), is_active) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE languages SET "
                         "code = COALESCE(NULLIF(?, ''), code), "
                         "name = COALESCE(NULLIF(?, ''), name), "
                         "is_default = COALESCE(NULLIF(?, ''), is_default), "
                         "is_active = COALESCE(NULLIF(?, ''), is_active) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE languages SET "
                         "code = COALESCE(NULLIF($1, ''), code), "
                         "name = COALESCE(NULLIF($2, ''), name), "
                         "is_default = COALESCE(NULLIF($3, ''), is_default), "
                         "is_active = COALESCE(NULLIF($4, ''), is_active) "
                         "WHERE id = $5;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt))  return_defer(false);
    for (int i = 1; i <= 4; ++i) {
        if (!sql_bind(&stmt, i, SQL_SV(fields[i - 1]))) return_defer(false);
    }
    if (!sql_bind(&stmt, 5, SQL_SV(id)))        return_defer(false);
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool soft_delete_language(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE languages "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE languages SET deleted_at = UTC_TIMESTAMP() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE languages SET deleted_at = now() WHERE id = $1;",
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

static bool restore_language(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE languages SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE languages SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE languages SET deleted_at = NULL WHERE id = $1;",
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

void serve_pos_i18n(Serve_Context *sc) {
    MD_ChildTab children[] = {
        {
            .table        = "translations",
            .title        = "Strings",
            .fk_column    = "language_id",
            .id_column    = "id",
            .crud_path    = "/pos/translations",
            .columns      = md_translation_columns,
            .column_count = md_translation_columns_count,
            .soft_delete  = 1,
        },
    };
    MD_MasterConfig config = {
        .table          = "languages",
        .title          = "Languages",
        .id_column      = "id",
        .crud_path      = "/pos/i18n",
        .columns        = md_languages_columns,
        .column_count   = md_languages_columns_count,
        .children       = children,
        .children_count = ARRAY_LEN(children),
        .soft_delete    = 1,
    };
    serve_master_child(sc, &config);
}

static const char *lng_fields[] = { "code", "name" };
static const char *lng_opt_fields[] = { "is_default", "is_active" };
SERVE_CREATE(pos_i18n, language, lng_fields, lng_opt_fields)
SERVE_UPDATE(pos_i18n, language, lng_fields, lng_opt_fields)
SERVE_SOFT_DELETE(pos_i18n, language)
