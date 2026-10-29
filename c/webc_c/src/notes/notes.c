#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define NOB_STRIP_PREFIX
#include "third_party/nob/nob.h"

#include "notes.h"
#include "src/layout/master_child.h"
#include "src/layout/paging.h"
#include "src/layout/header.h"
#include "src/layout/footer.h"
#include "src/core/webc_template.h"
#include "src/http/id.h"
#include "src/http/utils.h"
#include "src/db/db.h"

// ---- /people child column shape (composed in people.c) -----------------
MD_Column md_notes_columns[] = {
    {
        .name     = "title",
        .label    = "Title",
        .type     = COL_TYPE_TEXT,
        .nullable = false
    }, {
        .name     = "body",
        .label    = "Body",
        .type     = COL_TYPE_TEXTAREA,
        .nullable = true
    }, {
        .name     = "created_at",
        .label    = "Created",
        .type     = COL_TYPE_DATE,
        .nullable = false,
        .computed = 1
    },
};
const size_t md_notes_columns_count = ARRAY_LEN(md_notes_columns);

void render_notes_page(Serve_Context *sc,
                       Notes          notes,
                       Page_Info      page_info,
                       bool           fragment)
{
    // PAGE_BEGIN's declarations, minus the header: ?fragment=all responses
    // are the bare pagination store (rows + per-page <template> pagers).
    String_Builder *sb         = &(sc)->body;
    const char *page_title     = "Note";
    const char *current_path   = "/notes";
    String_View page_query     = sc->query_string;
    const char *page_base      = current_path;
    const char *page_key       = "page";
    const char *page_container = "notes-tbody";
    if (!fragment) render_page_header(sb, page_title, current_path);
#include "build/h_to_html/notes.h"
    if (!fragment) render_page_footer(sb);
}

void render_notes_edit_page(Serve_Context *sc, Note note) {
    PAGE_BEGIN(sc, "Edit Note", "/notes");
#include "build/h_to_html/notes_edit.h"
    PAGE_END(sc);
}

static void serve_notes_json(Serve_Context *sc) {
    Notes notes = {0};
    db_t *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }

    if (!read_notes(db, &notes, NULL)) {
        db_close(db);
        serve_error(sc, 500);
        return;
    }
    db_close(db);

    String_Builder body = {0};
    sb_append_cstr(&body, "[");
    for (size_t i = 0; i < notes.count; ++i) {
        Note *note = &notes.items[i];
        if (i > 0) sb_append_cstr(&body, ",");
        sb_append_cstr(&body, "{\"id\":");
        sb_append_json_escaped(&body, note->id ? note->id : "");
        sb_append_cstr(&body, ",\"title\":");
        sb_append_json_escaped(&body, note->title);
        sb_append_cstr(&body, ",\"created_at\":");
        sb_append_json_escaped(&body, note->created_at);
        sb_append_cstr(&body, ",\"body\":");
        sb_append_json_escaped(&body, note->body);
        sb_append_cstr(&body, "}");
    }
    sb_append_cstr(&body, "]");

    free(notes.items);

    http_render_response(sc, 200, "application/json", sb_to_sv(body));
    sb_free(body);
}

void serve_notes_api(Serve_Context *sc) {
    String_View body_sv = sb_to_sv(sc->body);

    if (sv_eq(sc->method, sv_from_cstr("GET"))) {
        serve_notes_json(sc);
        return;
    }

    const char *fields[] = { "title", "body" };
    size_t field_count = sizeof(fields) / sizeof(fields[0]);
    String_View values[field_count];
    memset(values, 0, sizeof(values));
    // The "id" body field runs through the same parser as uri segments:
    // legacy numeric ids -> ID_INT, uuid defaults -> ID_STRING. TEXT tables
    // bind `raw`; has_id gates PUT/DELETE exactly as before.
    String_View id_sv = {0};
    Route_Id api_id = {0};
    bool has_id = json_find_string(body_sv, "id", &id_sv)
                  && route_id_parse(id_sv, &api_id);

    if (sv_eq(sc->method, sv_from_cstr("POST"))) {
        for (size_t i = 0; i < field_count; ++i) {
            if (!json_find_string(body_sv, fields[i], &values[i])) {
                serve_error(sc, 400);
                return;
            }
        }

        if (values[0].count == 0) {
            serve_error(sc, 400);
            return;
        }

        db_t *db = open_webc_db();
        if (!db) { serve_error(sc, 500); return; }
        if (!sql_txn_begin(db)) { 
            db_close(db); 
            serve_error(sc, 500);
            return;
        }
        bool ok = create_note(db, values, field_count);
        if (ok) sql_txn_commit(db);
        else sql_txn_rollback(db);
        db_close(db);
        if (!ok) { serve_error(sc, 500); return; }
        serve_ok(sc);
        return;
    }

    if (sv_eq(sc->method, sv_from_cstr("PUT"))) {
        if (!has_id) {
            serve_error(sc, 400);
            return;
        }

        for (size_t i = 0; i < field_count; ++i) {
            if (!json_find_string(body_sv, fields[i], &values[i])) {
                serve_error(sc, 400);
                return;
            }
        }

        if (values[0].count == 0) {
            serve_error(sc, 400);
            return;
        }

        db_t *db = open_webc_db();
        if (!db) { serve_error(sc, 500); return; }
        if (!sql_txn_begin(db)) { 
            db_close(db); 
            serve_error(sc, 500);
            return;
        }
        bool ok = update_note(db, values, field_count, api_id.raw);
        if (ok) sql_txn_commit(db);
        else sql_txn_rollback(db);
        db_close(db);
        if (!ok) { serve_error(sc, 500); return; }
        serve_ok(sc);
        return;
    }

    if (sv_eq(sc->method, sv_from_cstr("DELETE"))) {
        if (!has_id) {
            serve_error(sc, 400);
            return;
        }
        db_t *db = open_webc_db();
        if (!db) { serve_error(sc, 500); return; }
        if (!sql_txn_begin(db)) { 
            db_close(db); 
            serve_error(sc, 500);
            return;
        }
        bool ok = delete_note(db, api_id.raw);
        if (ok) sql_txn_commit(db);
        else sql_txn_rollback(db);
        db_close(db);
        if (!ok) { serve_error(sc, 500); return; }
        serve_ok(sc);
        return;
    }

    serve_error(sc, 405);
}

// =========================================================================
// Data layer
// =========================================================================

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

bool soft_delete_note(db_t *db, String_View id) {
    bool result = true;
    sql_stmt stmt = {0};
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE Notes "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE notes SET deleted_at = UTC_TIMESTAMP() "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE notes SET deleted_at = now() WHERE id = $1;",
    };

    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(id)))      return_defer(false);
    if (!sql_final_step(&stmt))               return_defer(false);

defer:
    sql_finalize(&stmt);
    return result;
}

static bool restore_note(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE notes SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE notes SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE notes SET deleted_at = NULL WHERE id = $1;",
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

static const char *fields[] = { "title", "body" };
// Optional: written only on create; the /people child form supplies it as
// ?user_id=<uuid>, the plain /notes form and the JSON API leave it NULL.
// Updates never touch user_id (update_note reads only title/body).
static const char *opt_fields[] = { "user_id" };
SERVE_CRUD(notes, note, Notes, Note, fields, opt_fields)
