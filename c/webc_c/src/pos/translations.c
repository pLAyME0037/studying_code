#include "translations.h"

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "core/http/utils.h"

// =========================================================================
// translations rows: created/edited from the /pos/i18n Strings child tab
// (language_id rides the form's query string). UNIQUE(language_id,
// trans_key) -- a duplicate key fails loudly (500). The language never
// moves (child fk, query echo on update).
// =========================================================================

static bool create_translation(db_t *db, String_View *fields, size_t count) {
    if (count < 3) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO translations "
                         "(language_id, trans_key, trans_value) "
                         "VALUES (?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO translations "
                         "(language_id, trans_key, trans_value) "
                         "VALUES (?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO translations "
                         "(language_id, trans_key, trans_value) "
                         "VALUES ($1, $2, $3);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt))  return_defer(false);
    if (!sql_bind(&stmt, 1, pos_sv(fields[2]))) return_defer(false);  // language
    if (!sql_bind(&stmt, 2, SQL_SV(fields[0]))) return_defer(false);  // key
    if (!sql_bind(&stmt, 3, SQL_SV(fields[1]))) return_defer(false);  // value
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool update_translation(db_t *db, String_View *fields, size_t count,
                               String_View id)
{
    if (count < 3) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE translations SET "
                         "trans_key = COALESCE(NULLIF(?, ''), trans_key), "
                         "trans_value = COALESCE(NULLIF(?, ''), trans_value) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE translations SET "
                         "trans_key = COALESCE(NULLIF(?, ''), trans_key), "
                         "trans_value = COALESCE(NULLIF(?, ''), trans_value) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE translations SET "
                         "trans_key = COALESCE(NULLIF($1, ''), trans_key), "
                         "trans_value = COALESCE(NULLIF($2, ''), trans_value) "
                         "WHERE id = $3;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt))  return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(fields[0]))) return_defer(false);
    if (!sql_bind(&stmt, 2, SQL_SV(fields[1]))) return_defer(false);
    if (!sql_bind(&stmt, 3, SQL_SV(id)))        return_defer(false);
    if (!sql_final_step(&stmt))                 return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool soft_delete_translation(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE translations "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE translations SET deleted_at = UTC_TIMESTAMP() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE translations SET deleted_at = now() WHERE id = $1;",
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

static bool restore_translation(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE translations SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE translations SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE translations SET deleted_at = NULL WHERE id = $1;",
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

static const char *trn_fields[] = { "trans_key", "trans_value" };
static const char *trn_opt_fields[] = { "language_id" };
SERVE_CREATE(pos_translations, translation, trn_fields, trn_opt_fields)
SERVE_UPDATE(pos_translations, translation, trn_fields, trn_opt_fields)
SERVE_SOFT_DELETE(pos_translations, translation)
