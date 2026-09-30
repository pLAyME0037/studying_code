#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "notes.h"

#include "module/webc_template.h"
#include "core/http/id.h"
#include "src/db/db.h"
#include "src/notes/notes.h"
#include "core/layout/header.h"
#include "core/layout/footer.h"
#include "core/http/utils.h"

void render_notes_page(Serve_Context *sc, Notes notes) {
    PAGE_BEGIN(sc, "Note", "/notes");
    #include "build/h_to_html/notes.h"
    PAGE_END(sc);
}

void render_notes_edit_page(Serve_Context *sc, Note note) {
    PAGE_BEGIN(sc, "Edit Note", "/notes");
    #include "build/h_to_html/notes_edit.h"
    PAGE_END(sc);
}

static const char *fields[] = { "title", "body" };
SERVE_CRUD(notes, note, Notes, Note, fields)

static void serve_notes_json(Serve_Context *sc) {
    Notes notes = {0};
    db_t *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }

    if (!read_notes(db, &notes)) {
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
