#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOB_STRIP_PREFIX
#include "module/nob.h"

#include "notes.h"
#include "../db/db.h"
#include "core/display/paging.h"

bool read_notes(db_t *db, Notes *notes, const Page_Info *slice) {
    bool result = true;
    sql_stmt stmt = {0};
    // `datetime(..., 'localtime')` is SQLite-only; the other dialects
    // format/return the stored timestamp as-is.
    // ORDER BY created_at DESC is the single line that decides list order.
    static const char *const q_all[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "SELECT id, user_id, title, "
                         "datetime(created_at, 'localtime'), body "
                         "FROM Notes ORDER BY created_at DESC;",
        [SQL_MYSQL]    = "SELECT id, user_id, title, created_at, body "
                         "FROM notes ORDER BY created_at DESC;",
        [SQL_POSTGRES] = "SELECT id, user_id, title, "
                         "TO_CHAR(created_at, 'YYYY-MM-DD HH24:MI:SS'), body "
                         "FROM notes ORDER BY created_at DESC;",
    };
    // Same query with the page window appended (limit/offset bound below).
    static const char *const q_page[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "SELECT id, user_id, title, "
                         "datetime(created_at, 'localtime'), body "
                         "FROM Notes ORDER BY created_at DESC "
                         "LIMIT ? OFFSET ?;",
        [SQL_MYSQL]    = "SELECT id, user_id, title, created_at, body "
                         "FROM notes ORDER BY created_at DESC "
                         "LIMIT ? OFFSET ?;",
        [SQL_POSTGRES] = "SELECT id, user_id, title, "
                         "TO_CHAR(created_at, 'YYYY-MM-DD HH24:MI:SS'), body "
                         "FROM notes ORDER BY created_at DESC "
                         "LIMIT $1 OFFSET $2;",
    };

    if (!sql_prepare(db, slice ? q_page[db->lang] : q_all[db->lang], &stmt)) {
        return_defer(false);
    }
    if (slice) {
        if (!sql_bind(&stmt, 1, SQL_I(slice->per_page))) return_defer(false);
        if (!sql_bind(&stmt, 2, SQL_I(slice->offset)))    return_defer(false);
    }

    Sql_Step ret = SQL_DONE;
    for (ret = sql_step(&stmt); ret == SQL_ROW; ret = sql_step(&stmt)) {
        int column             = 0;
        const char *id         = sql_column_text(&stmt, column++);
        const char *user_id    = sql_column_text(&stmt, column++);
        const char *title      = sql_column_text(&stmt, column++);
        const char *created_at = sql_column_text(&stmt, column++);
        const char *body       = sql_column_text(&stmt, column++);
        Notes_add(notes, ((Note) {
            .id         = id         ? temp_strdup(id)         : NULL,
            .user_id    = user_id    ? temp_strdup(user_id)    : NULL,
            .title      = title      ? temp_strdup(title)      : NULL,
            .created_at = created_at ? temp_strdup(created_at) : NULL,
            .body       = body       ? temp_strdup(body)       : NULL,
        }));
    }

    if (ret != SQL_DONE) {
        nob_log(NOB_ERROR, "read_notes: %s", db_errmsg(db));
        return_defer(false);
    }

defer:
    sql_finalize(&stmt);
    return result;
}

bool count_notes(db_t *db, size_t *out) {
    sql_stmt stmt = {0};
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "SELECT count(*) FROM Notes;",
        [SQL_MYSQL]    = "SELECT count(*) FROM notes;",
        [SQL_POSTGRES] = "SELECT count(*) FROM notes;",
    };
    if (!sql_prepare(db, q[db->lang], &stmt)) return false;
    bool ok = false;
    if (sql_step(&stmt) == SQL_ROW) {
        *out = (size_t)sql_column_int64(&stmt, 0);
        ok = true;
    } else {
        nob_log(NOB_ERROR, "count_notes: %s", db_errmsg(db));
    }
    sql_finalize(&stmt);
    return ok;
}

bool create_note(db_t *db, String_View *values, size_t count) {
    bool result = true;

    if (count < 2) return false;
    String_View title  = values[0];
    String_View body   = values[1];
    // Optional third value: user_id (the /people child form passes it, the
    // plain /notes form and the JSON API do not -> SQL NULL). An empty
    // value normalizes to {0} so the driver binds NULL, never ''.
    String_View user_id = {0};
    if (count > 2) user_id = values[2];
    if (user_id.count == 0) user_id = (String_View){0};
    sql_stmt stmt = {0};
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO Notes (user_id, title, body) "
                         "VALUES (?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO notes (user_id, title, body) "
                         "VALUES (?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO notes (user_id, title, body) "
                         "VALUES ($1, $2, $3);",
    };

    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(user_id))) return_defer(false);
    if (!sql_bind(&stmt, 2, SQL_SV(title)))   return_defer(false);
    if (!sql_bind(&stmt, 3, SQL_SV(body)))    return_defer(false);
    if (!sql_final_step(&stmt)) return_defer(false);

defer:
    sql_finalize(&stmt);
    return result;
}

bool update_note(db_t *db, String_View *values, size_t count, String_View id) {
    bool result = true;

    if (count < 2) return false;
    String_View title = values[0];
    String_View body  = values[1];
    sql_stmt stmt = {0};
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE Notes SET title = ?, body = ? WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE notes SET title = ?, body = ? WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE notes SET title = $1, body = $2 WHERE id = $3;",
    };

    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(title))) return_defer(false);
    if (!sql_bind(&stmt, 2, SQL_SV(body)))  return_defer(false);
    if (!sql_bind(&stmt, 3, SQL_SV(id)))    return_defer(false);
    if (!sql_final_step(&stmt)) return_defer(false);

defer:
    sql_finalize(&stmt);
    return result;
}

bool delete_note(db_t *db, String_View id) {
    bool result = true;
    sql_stmt stmt = {0};
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "DELETE FROM Notes WHERE id = ?;",
        [SQL_MYSQL]    = "DELETE FROM notes WHERE id = ?;",
        [SQL_POSTGRES] = "DELETE FROM notes WHERE id = $1;",
    };

    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(id))) return_defer(false);
    if (!sql_final_step(&stmt)) return_defer(false);

defer:
    sql_finalize(&stmt);
    return result;
}
