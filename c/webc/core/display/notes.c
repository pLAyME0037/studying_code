#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "notes.h"

#include "src/db/db.h"
#include "src/notes/notes.h"
#include "core/layout/header.h"
#include "core/layout/footer.h"
#include "core/http/utils.h"

#define DEFINE_PAGE(name, data_t, title, route)                       \
    void render_##name##_page(Serve_Context *sc, data_t da) {         \
        String_Builder *sb = &sc->body;                               \
        render_page_header(sb, title, route);                         \
        include_body_##name(sb, da);                                  \
        render_page_footer(sb);                                       \
    }

void include_body_notes(String_Builder *sb, note_da notes) {
#include "build/h_to_html/notes.h"
}
// 1-line clean declarations:
DEFINE_PAGE(notes, note_da, "Note", "/notes");
DEFINE_PAGE(notes_edit, note_t, "Edit Note", "/notes");

// void render_notes_edit_page(Serve_Context *sc, note_t note) {
//     String_Builder *sb = &sc->body;
//     render_page_header(sb, "Edit Note", "/notes");
// #define OUT(buf, size) sb_append_buf(sb, buf, size);
// #define INT(x) sb_appendf(sb, "%d", (x));
// #define STR(x) sb_append_cstr(sb, (x) ? (x) : "");
// #define ESCAPED(x) sb_append_html_escaped(sb, (x) ? (x) : "");
// #define PAGE_TITLE "Edit Note"
// #include "build/h_to_html/notes_edit.h"
// #undef PAGE_TITLE
// #undef ESCAPED
// #undef STR
// #undef INT
// #undef OUT
//     render_page_footer(sb);
// }

void serve_notes(Serve_Context *sc, String_View method) {
    UNUSED(method);

    note_da notes = {0};
    sqlite3 *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }
    if (!load_notes(db, &notes)) {
        sqlite3_close(db);
        serve_error(sc, 500);
        return;
    }
    sqlite3_close(db);

    sc->body.count = 0;
    render_notes_page(sc, notes);
    free(notes.items);

    http_render_response(sc, 200, "text/html", sb_to_sv(sc->body));
}

void serve_notes_create(Serve_Context *sc) {
    char title[512] = {0};
    char body[4096] = {0};

    if (!form_find(sb_to_sv(sc->body), "title", title, sizeof(title)) || title[0] == '\0') {
        serve_error(sc, 400);
        return;
    }
    form_find(sb_to_sv(sc->body), "body", body, sizeof(body));

    sqlite3 *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }
    if (!txn_begin(db)) { sqlite3_close(db); serve_error(sc, 500); return; }
    bool ok = insert_note(db, title, body);
    txn_commit(db);
    sqlite3_close(db);

    if (!ok) { serve_error(sc, 500); return; }
    http_render_redirect(sc, 302, "/notes");
}

static bool parse_note_id_from_uri(String_View uri, const char *suffix, int *id) {
    // /notes/<id>/edit, /notes/<id>/update, /notes/<id>/delete
    const char *prefix = "/notes/";
    size_t plen = strlen(prefix);
    if (uri.count <= plen || memcmp(uri.data, prefix, plen) != 0) return false;
    if (!sv_ends_with(uri, sv_from_cstr(suffix))) return false;

    String_View id_sv = {
        .data  = uri.data + plen,
        .count = uri.count - plen - strlen(suffix),
    };
    char buf[32] = {0};
    snprintf(buf, sizeof(buf), "%.*s", (int)id_sv.count, id_sv.data);
    char *end = NULL;
    long value = strtol(buf, &end, 10);
    if (end == buf || *end != '\0') return false;
    *id = (int)value;
    return true;
}

void serve_notes_edit(Serve_Context *sc, String_View uri) {
    int id = 0;
    if (!parse_note_id_from_uri(uri, "/edit", &id)) {
        serve_error(sc, 404);
        return;
    }

    note_da notes = {0};
    sqlite3 *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }
    if (!load_notes(db, &notes)) {
        sqlite3_close(db);
        serve_error(sc, 500);
        return;
    }
    sqlite3_close(db);

    note_t *target = NULL;
    for (size_t i = 0; i < notes.count; ++i) {
        if (notes.items[i].id == id) {
            target = &notes.items[i];
            break;
        }
    }
    if (!target) {
        free(notes.items);
        serve_error(sc, 404);
        return;
    }

    sc->body.count = 0;
    render_notes_edit_page(sc, *target);
    free(notes.items);

    http_render_response(sc, 200, "text/html", sb_to_sv(sc->body));
}

void serve_notes_update(Serve_Context *sc, String_View uri) {
    int id = 0;
    if (!parse_note_id_from_uri(uri, "/update", &id)) {
        serve_error(sc, 404);
        return;
    }

    char title[512] = {0};
    char body[4096] = {0};

    if (!form_find(sb_to_sv(sc->body), "title", title, sizeof(title)) || title[0] == '\0') {
        serve_error(sc, 400);
        return;
    }
    form_find(sb_to_sv(sc->body), "body", body, sizeof(body));

    sqlite3 *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }
    if (!txn_begin(db)) { sqlite3_close(db); serve_error(sc, 500); return; }
    bool ok = update_note(db, id, title, body);
    txn_commit(db);
    sqlite3_close(db);

    if (!ok) { serve_error(sc, 500); return; }
    http_render_redirect(sc, 302, "/notes");
}

void serve_notes_delete(Serve_Context *sc, String_View uri) {
    int id = 0;
    if (!parse_note_id_from_uri(uri, "/delete", &id)) {
        serve_error(sc, 404);
        return;
    }

    sqlite3 *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }
    if (!txn_begin(db)) { sqlite3_close(db); serve_error(sc, 500); return; }
    bool ok = delete_note(db, id);
    txn_commit(db);
    sqlite3_close(db);

    if (!ok) { serve_error(sc, 500); return; }
    http_render_redirect(sc, 302, "/notes");
}

static void serve_notes_json(Serve_Context *sc) {
    note_da notes = {0};
    sqlite3 *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }

    if (!load_notes(db, &notes)) {
        sqlite3_close(db);
        serve_error(sc, 500);
        return;
    }
    sqlite3_close(db);

    String_Builder body = {0};
    sb_append_cstr(&body, "[");
    for (size_t i = 0; i < notes.count; ++i) {
        note_t *note = &notes.items[i];
        if (i > 0) sb_append_cstr(&body, ",");
        sb_append_cstr(&body, "{\"id\":");
        sb_appendf(&body, "%d", note->id);
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
}

void serve_notes_api(Serve_Context *sc, String_View method) {
    String_View body_sv = sb_to_sv(sc->body);

    if (sv_eq(method, sv_from_cstr("GET"))) {
        serve_notes_json(sc);
        return;
    }

    char title[512] = {0};
    char body[4096] = {0};
    long long id = 0;
    bool has_id = json_find_int(body_sv, "id", &id);

    if (sv_eq(method, sv_from_cstr("POST"))) {
        String_View title_sv = {0}, body_sv2 = {0};
        if (!json_find_string(body_sv, "title", &title_sv)) {
            serve_error(sc, 400);
            return;
        }
        json_find_string(body_sv, "body", &body_sv2);
        snprintf(title, sizeof(title), "%.*s", (int)title_sv.count, title_sv.data);
        snprintf(body, sizeof(body), "%.*s", (int)body_sv2.count, body_sv2.data);

        sqlite3 *db = open_webc_db();
        if (!db) { serve_error(sc, 500); return; }
        if (!txn_begin(db)) { sqlite3_close(db); serve_error(sc, 500); return; }
        bool ok = insert_note(db, title, body);
        txn_commit(db);
        sqlite3_close(db);
        if (!ok) { serve_error(sc, 500); return; }
        serve_ok(sc);
        return;
    }

    if (sv_eq(method, sv_from_cstr("PUT"))) {
        String_View title_sv = {0}, body_sv2 = {0};
        if (!has_id || !json_find_string(body_sv, "title", &title_sv)) {
            serve_error(sc, 400);
            return;
        }
        json_find_string(body_sv, "body", &body_sv2);
        snprintf(title, sizeof(title), "%.*s", (int)title_sv.count, title_sv.data);
        snprintf(body, sizeof(body), "%.*s", (int)body_sv2.count, body_sv2.data);

        sqlite3 *db = open_webc_db();
        if (!db) { serve_error(sc, 500); return; }
        if (!txn_begin(db)) { sqlite3_close(db); serve_error(sc, 500); return; }
        bool ok = update_note(db, (int)id, title, body);
        txn_commit(db);
        sqlite3_close(db);
        if (!ok) { serve_error(sc, 500); return; }
        serve_ok(sc);
        return;
    }

    if (sv_eq(method, sv_from_cstr("DELETE"))) {
        if (!has_id) {
            serve_error(sc, 400);
            return;
        }
        sqlite3 *db = open_webc_db();
        if (!db) { serve_error(sc, 500); return; }
        if (!txn_begin(db)) { sqlite3_close(db); serve_error(sc, 500); return; }
        bool ok = delete_note(db, (int)id);
        txn_commit(db);
        sqlite3_close(db);
        if (!ok) { serve_error(sc, 500); return; }
        serve_ok(sc);
        return;
    }

    serve_error(sc, 405);
}
