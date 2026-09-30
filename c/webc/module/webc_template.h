// template_runtime.h
#pragma once

#ifndef WEBC_TEMPLATE
#define WEBC_TEMPLATE

#include "module/nob.h"
#include "core/http/utils.h"
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

// =========================================================================
// Helper macros for tt template
// =========================================================================

#define OUT(buf, size)     nob_sb_append_buf(sb, buf, size);
#define STR(x)             nob_sb_append_cstr(sb, (x) ? (x) : "");
#define INT(x)             nob_sb_appendf(sb, "%d", (x));
#define CLS(cond, t, f)    nob_sb_append_cstr(sb, (cond) ? (t) : (f));
#define NAV_ACTIVE(prefix) (strncmp((current_path), (prefix), strlen(prefix)) == 0)
#define ESCAPED(x)         sb_append_html_escaped(sb, (x) ? (x) : "");
#define CURRENT_PATH       current_path
#define PAGE_TITLE         page_title

#define PAGE_BEGIN(sc, title, route)                 \
    Nob_String_Builder *sb = &(sc)->body;            \
    const char *page_title = (title);                \
    const char *current_path = (route);              \
    render_page_header(sb, page_title, current_path)

#define PAGE_END(sc) \
    render_page_footer(&(sc)->body)

// =========================================================================
// Dynamic CRUD with Zero Limits (Loop-based)
// =========================================================================

#define SERVE_READ(plural, Plural_Type)                                   \
    void serve_##plural##_read(Serve_Context *sc) {                       \
        Plural_Type dt = {0};                                             \
        sqlite3 *db = open_webc_db();                                     \
        if (!db) { serve_error(sc, 500); return; }                        \
        if (!load_##plural(db, &dt)) {                                    \
            sqlite3_close(db);                                            \
            serve_error(sc, 500);                                         \
            return;                                                       \
        }                                                                 \
        sqlite3_close(db);                                                \
        sc->body.count = 0;                                               \
        render_##plural##_page(sc, dt);                                   \
        free(dt.items);                                                   \
        http_render_response(sc, 200, "text/html", sb_to_sv(sc->body));   \
    }

// Extract all listed form fields into `values` (urlencoded or multipart).
// File uploads are compressed/stored and values[i] is replaced with the URL
// the image will be served from. Aborts the handler with an appropriate
// error status when extraction or storing fails.
#define SERVE_EXTRACT_FIELDS(sc, body_sv, values, fields, field_count)                 \
    do {                                                                               \
        Nob_String_View req_sv_ = sb_to_sv((sc)->request);                             \
        for (size_t i_ = 0; i_ < (field_count); ++i_) {                                \
            (values)[i_] = (Nob_String_View){0};                                       \
            Form_Field field_ = {0};                                                   \
            if (!form_get(req_sv_, (body_sv), (fields)[i_], &field_)) {                \
                serve_error((sc), 400);                                                \
                return;                                                                \
            }                                                                          \
            (values)[i_] = field_.value;                                               \
            if (field_.kind == FIELD_FILE && field_.value.count > 0) {                 \
                switch (save_uploaded_image(&(values)[i_])) {                          \
                case UPLOAD_OK: break;                                                 \
                case UPLOAD_TOO_LARGE:                                                 \
                    fprintf(stderr, "ERROR: upload rejected: image exceeds %d bytes\n", \
                            MAX_UPLOAD_IMAGE_SIZE);                                    \
                    serve_error((sc), 413);                                            \
                    return;                                                            \
                case UPLOAD_NOT_IMAGE:                                                 \
                    fprintf(stderr, "ERROR: upload rejected: not a decodable "         \
                            "png/jpeg/gif/webp image\n");                              \
                    serve_error((sc), 400);                                            \
                    return;                                                            \
                case UPLOAD_IO_ERROR:                                                  \
                    serve_error((sc), 500);                                            \
                    return;                                                            \
                }                                                                      \
            }                                                                          \
        }                                                                              \
    } while (0)

#define SERVE_CREATE(plural, singular, fields)                            \
    void serve_##plural##_create(Serve_Context *sc) {                     \
        size_t field_count = ARRAY_LEN(fields);                           \
        Nob_String_View values[field_count];                              \
        Nob_String_View body_sv = sb_to_sv(sc->body);                     \
                                                                          \
        SERVE_EXTRACT_FIELDS(sc, body_sv, values, fields, field_count);   \
                                                                          \
        if (field_count > 0 && values[0].count == 0) {                    \
            serve_error(sc, 400);                                         \
            return;                                                       \
        }                                                                 \
                                                                          \
        sqlite3 *db = open_webc_db();                                     \
        if (!db) { serve_error(sc, 500); return; }                        \
        if (!txn_begin(db)) {                                             \
            sqlite3_close(db);                                            \
            serve_error(sc, 500);                                         \
            return;                                                       \
        }                                                                 \
                                                                          \
        bool ok = insert_##singular(db, values, field_count);             \
        if (ok) { txn_commit(db); }                                       \
        else    { txn_rollback(db); }                                     \
        sqlite3_close(db);                                                \
                                                                          \
        if (!ok) { serve_error(sc, 500); return; }                        \
        http_render_redirect(sc, 302, "/" #plural);                       \
    }

#define SERVE_EDIT(plural, singular, Plural_Type, Singular_Type)        \
    void serve_##plural##_edit(Serve_Context *sc) {                     \
        String_View id = {0};                                           \
        if (!parse_id_from_uri(sc->uri, "/" #plural "/", "/edit", &id)) { \
            serve_error(sc, 404);                                       \
            return;                                                     \
        }                                                               \
        Plural_Type dt = {0};                                           \
        sqlite3 *db = open_webc_db();                                   \
        if (!db) { serve_error(sc, 500); return; }                      \
        if (!load_##plural(db, &dt)) {                                  \
            sqlite3_close(db);                                          \
            serve_error(sc, 500);                                       \
            return;                                                     \
        }                                                               \
        sqlite3_close(db);                                              \
        Singular_Type *target = NULL;                                   \
        for (size_t i = 0; i < dt.count; ++i) {                         \
            if (dt.items[i].id != NULL                                 \
                && sv_eq(sv_from_cstr(dt.items[i].id), id)) {           \
                target = &dt.items[i];                                  \
                break;                                                  \
            }                                                           \
        }                                                               \
        if (!target) {                                                  \
            free(dt.items);                                             \
            serve_error(sc, 404);                                       \
            return;                                                     \
        }                                                               \
        sc->body.count = 0;                                             \
        render_##plural##_edit_page(sc, *target);                       \
        free(dt.items);                                                 \
        http_render_response(sc, 200, "text/html", sb_to_sv(sc->body)); \
    }

#define SERVE_UPDATE(plural, singular, fields)                            \
    void serve_##plural##_update(Serve_Context *sc) {                     \
        String_View id = {0};                                               \
        if (!parse_id_from_uri(sc->uri, "/" #plural "/", "/update", &id)) { \
            serve_error(sc, 404);                                         \
            return;                                                       \
        }                                                                 \
                                                                          \
        size_t field_count = ARRAY_LEN(fields);                           \
        Nob_String_View values[field_count];                              \
        Nob_String_View body_sv = sb_to_sv(sc->body);                     \
                                                                          \
        SERVE_EXTRACT_FIELDS(sc, body_sv, values, fields, field_count);   \
                                                                          \
        if (field_count > 0 && values[0].count == 0) {                    \
            serve_error(sc, 400);                                         \
            return;                                                       \
        }                                                                 \
                                                                          \
        sqlite3 *db = open_webc_db();                                     \
        if (!db) { serve_error(sc, 500); return; }                        \
        if (!txn_begin(db)) {                                             \
            sqlite3_close(db);                                            \
            serve_error(sc, 404);                                         \
            return;                                                       \
        }                                                                 \
        bool ok = update_##singular(db, values, field_count, id);         \
        if (ok) { txn_commit(db); }                                       \
        else    { txn_rollback(db); }                                     \
        sqlite3_close(db);                                                \
                                                                          \
        if (!ok) { serve_error(sc, 500); return; }                        \
        http_render_redirect(sc, 302, "/" #plural);                       \
    }

#define SERVE_DELETE(plural, singular)                                    \
    void serve_##plural##_delete(Serve_Context *sc) {                     \
        String_View id = {0};                                               \
        if (!parse_id_from_uri(sc->uri, "/" #plural "/", "/delete", &id)) { \
            serve_error(sc, 404);                                         \
            return;                                                       \
        }                                                                 \
        sqlite3 *db = open_webc_db();                                     \
        if (!db) { serve_error(sc, 500); return; }                        \
        if (!txn_begin(db)) {                                             \
            sqlite3_close(db);                                            \
            serve_error(sc, 404);                                         \
            return;                                                       \
        }                                                                 \
        bool ok = delete_##singular(db, id);                              \
        if (ok) { txn_commit(db); }                                       \
        else    { txn_rollback(db); }                                     \
        sqlite3_close(db);                                                \
        if (!ok) { serve_error(sc, 500); return; }                        \
        http_render_redirect(sc, 302, "/" #plural);                       \
    }

#define SERVE_CRUD(plural, singular, Plural_Type, Singular_Type, ...)\
    SERVE_READ(plural, Plural_Type)                                  \
    SERVE_EDIT(plural, singular, Plural_Type, Singular_Type)         \
    SERVE_DELETE(plural, singular)                                   \
    SERVE_CREATE(plural, singular, __VA_ARGS__)                      \
    SERVE_UPDATE(plural, singular, __VA_ARGS__)


#endif // !WEBC_TEMPLATE

