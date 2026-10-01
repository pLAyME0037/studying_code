// template_runtime.h
#pragma once

#ifndef WEBC_TEMPLATE
#define WEBC_TEMPLATE

#include "module/nob.h"
#include "core/http/utils.h"
#include "core/display/paging.h"
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

// List handler: parse ?page/?per_page, count, load just that window and
// render. ?fragment=all skips the shell and loads everything instead --
// that bare payload is the pagination store fetched in the background by
// js/PaginationSwitcher.js (page flips then happen client-side).
#define SERVE_READ(plural, Plural_Type)                                   \
    void serve_##plural##_read(Serve_Context *sc) {                       \
        Plural_Type dt = {0};                                             \
        Page_Info page_info = {0};                                        \
        bool fragment = page_fragment_requested(sc->query_string);        \
        page_info_parse(sc->query_string, "page", &page_info);            \
        db_t *db = open_webc_db();                                        \
        if (!db) { serve_error(sc, 500); return; }                        \
        bool ok;                                                          \
        if (fragment) {                                                   \
            ok = read_##plural(db, &dt, NULL);                            \
        } else if (count_##plural(db, &page_info.total)) {                \
            page_info_finish(&page_info, page_info.total);                \
            ok = read_##plural(db, &dt, &page_info);                      \
        } else {                                                          \
            ok = false;                                                   \
        }                                                                 \
        db_close(db);                                                     \
        if (!ok) { serve_error(sc, 500); return; }                        \
        if (fragment) page_info_finish(&page_info, dt.count);             \
        sc->body.count = 0;                                               \
        render_##plural##_page(sc, dt, page_info, fragment);              \
        free(dt.items);                                                   \
        http_render_response(sc, 200, "text/html", sb_to_sv(sc->body));   \
    }

// Extract one form field (urlencoded or multipart). File uploads are
// compressed/stored and values[i] is replaced with the URL the image will be
// served from. `on_miss` runs when the key is absent from the body; it may
// `return` (required fields answer 400) or fall back to another source.
#define SERVE_EXTRACT_ONE(sc, body_sv, values, i_, key_, on_miss)               \
    do {                                                                        \
        Nob_String_View req_sv_ = sb_to_sv((sc)->request);                      \
        (values)[i_] = (Nob_String_View){0};                                    \
        Form_Field field_ = {0};                                                \
        if (!form_get(req_sv_, (body_sv), (key_), &field_)) {                   \
            on_miss                                                             \
        } else {                                                                \
            (values)[i_] = field_.value;                                        \
            if (field_.kind == FIELD_FILE && field_.value.count > 0) {          \
                switch (save_uploaded_image(&(values)[i_])) {                   \
                case UPLOAD_OK: break;                                          \
                case UPLOAD_TOO_LARGE:                                          \
                    fprintf(stderr, "ERROR: upload rejected: image exceeds %d bytes\n", \
                            MAX_UPLOAD_IMAGE_SIZE);                             \
                    serve_error((sc), 413);                                     \
                    return;                                                     \
                case UPLOAD_NOT_IMAGE:                                          \
                    fprintf(stderr, "ERROR: upload rejected: not a decodable "  \
                            "png/jpeg/gif/webp image\n");                       \
                    serve_error((sc), 400);                                     \
                    return;                                                     \
                case UPLOAD_IO_ERROR:                                           \
                    serve_error((sc), 500);                                     \
                    return;                                                     \
                }                                                               \
            }                                                                   \
        }                                                                       \
    } while (0)

// Required fields: a key missing from the body is a 400.
#define SERVE_EXTRACT_FIELDS(sc, body_sv, values, fields, field_count)          \
    do {                                                                        \
        for (size_t i_ = 0; i_ < (field_count); ++i_) {                         \
            SERVE_EXTRACT_ONE(sc, body_sv, values, i_, (fields)[i_],            \
                { serve_error((sc), 400); return; });                           \
        }                                                                       \
    } while (0)

// Optional fields (appended after the required ones): body first, then the
// query string, then empty. This is how the master-detail forms pass the FK
// (`POST /notes/create?user_id=<uuid>`) while the plain /notes and /users
// forms keep working without those keys.
#define SERVE_EXTRACT_OPT_FIELDS(sc, body_sv, values, opt_fields, opt_count, base) \
    do {                                                                           \
        for (size_t i_ = 0; i_ < (opt_count); ++i_) {                              \
            SERVE_EXTRACT_ONE(sc, body_sv, values, (base) + i_, (opt_fields)[i_],  \
                { if (!form_find((sc)->query_string, (opt_fields)[i_],             \
                                 &(values)[(base) + i_]))                          \
                      (values)[(base) + i_] = (Nob_String_View){0}; });            \
        }                                                                          \
    } while (0)

#define SERVE_CREATE(plural, singular, fields, opt_fields)                \
    void serve_##plural##_create(Serve_Context *sc) {                     \
        size_t field_count = ARRAY_LEN(fields) + ARRAY_LEN(opt_fields);   \
        Nob_String_View values[field_count];                              \
        Nob_String_View body_sv = sb_to_sv(sc->body);                     \
                                                                          \
        SERVE_EXTRACT_FIELDS(sc, body_sv, values, fields, ARRAY_LEN(fields)); \
        SERVE_EXTRACT_OPT_FIELDS(sc, body_sv, values, opt_fields,         \
                                 ARRAY_LEN(opt_fields), ARRAY_LEN(fields)); \
                                                                          \
        if (ARRAY_LEN(fields) > 0 && values[0].count == 0) {              \
            serve_error(sc, 400);                                         \
            return;                                                       \
        }                                                                 \
                                                                          \
        db_t *db = open_webc_db();                                        \
        if (!db) { serve_error(sc, 500); return; }                        \
        if (!sql_txn_begin(db)) {                                         \
            db_close(db);                                                 \
            serve_error(sc, 500);                                         \
            return;                                                       \
        }                                                                 \
                                                                          \
        bool ok = create_##singular(db, values, field_count);             \
        if (ok) { sql_txn_commit(db); }                                   \
        else    { sql_txn_rollback(db); }                                 \
        db_close(db);                                                     \
                                                                          \
        if (!ok) { serve_error(sc, 500); return; }                        \
        http_render_redirect(sc, 302,                                     \
                             http_redirect_target(sc, "/" #plural));      \
    }

#define SERVE_EDIT(plural, singular, Plural_Type, Singular_Type)        \
    void serve_##plural##_edit(Serve_Context *sc) {                     \
        Route_Id id = sc->route_id;   /* parsed once at the route gate */ \
        if (id.kind == ID_NONE) {                                       \
            serve_error(sc, 404);                                       \
            return;                                                     \
        }                                                               \
        Plural_Type dt = {0};                                           \
        db_t *db = open_webc_db();                                      \
        if (!db) { serve_error(sc, 500); return; }                      \
        if (!read_##plural(db, &dt, NULL)) {                            \
            db_close(db);                                               \
            serve_error(sc, 500);                                       \
            return;                                                     \
        }                                                               \
        db_close(db);                                                   \
        Singular_Type *target = NULL;                                   \
        for (size_t i = 0; i < dt.count; ++i) {                         \
            if (dt.items[i].id != NULL                                  \
                && sv_eq(sv_from_cstr(dt.items[i].id), id.raw)) {       \
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

#define SERVE_UPDATE(plural, singular, fields, opt_fields)                \
    void serve_##plural##_update(Serve_Context *sc) {                     \
        Route_Id id = sc->route_id;   /* parsed once at the route gate */ \
        if (id.kind == ID_NONE) {                                         \
            serve_error(sc, 404);                                         \
            return;                                                       \
        }                                                                 \
                                                                          \
        size_t field_count = ARRAY_LEN(fields) + ARRAY_LEN(opt_fields);   \
        Nob_String_View values[field_count];                              \
        Nob_String_View body_sv = sb_to_sv(sc->body);                     \
                                                                          \
        SERVE_EXTRACT_FIELDS(sc, body_sv, values, fields, ARRAY_LEN(fields)); \
        SERVE_EXTRACT_OPT_FIELDS(sc, body_sv, values, opt_fields,         \
                                 ARRAY_LEN(opt_fields), ARRAY_LEN(fields)); \
                                                                          \
        if (ARRAY_LEN(fields) > 0 && values[0].count == 0) {              \
            serve_error(sc, 400);                                         \
            return;                                                       \
        }                                                                 \
                                                                          \
        db_t *db = open_webc_db();                                        \
        if (!db) { serve_error(sc, 500); return; }                        \
        if (!sql_txn_begin(db)) {                                         \
            db_close(db);                                                 \
            serve_error(sc, 404);                                         \
            return;                                                       \
        }                                                                 \
        bool ok = update_##singular(db, values, field_count, id.raw);     \
        if (ok) { sql_txn_commit(db); }                                   \
        else    { sql_txn_rollback(db); }                                 \
        db_close(db);                                                     \
                                                                          \
        if (!ok) { serve_error(sc, 500); return; }                        \
        http_render_redirect(sc, 302,                                     \
                             http_redirect_target(sc, "/" #plural));      \
    }

#define SERVE_DELETE(plural, singular)                                    \
    void serve_##plural##_delete(Serve_Context *sc) {                     \
        Route_Id id = sc->route_id;   /* parsed once at the route gate */ \
        if (id.kind == ID_NONE) {                                         \
            serve_error(sc, 404);                                         \
            return;                                                       \
        }                                                                 \
        db_t *db = open_webc_db();                                        \
        if (!db) { serve_error(sc, 500); return; }                        \
        if (!sql_txn_begin(db)) {                                         \
            db_close(db);                                                 \
            serve_error(sc, 404);                                         \
            return;                                                       \
        }                                                                 \
        bool ok = delete_##singular(db, id.raw);                          \
        if (ok) { sql_txn_commit(db); }                                   \
        else    { sql_txn_rollback(db); }                                 \
        db_close(db);                                                     \
        if (!ok) { serve_error(sc, 500); return; }                        \
        http_render_redirect(sc, 302,                                     \
                             http_redirect_target(sc, "/" #plural));      \
    }

#define SERVE_CRUD(plural, singular, Plural_Type, Singular_Type, fields, opt_fields) \
    SERVE_READ(plural, Plural_Type)                                                  \
    SERVE_EDIT(plural, singular, Plural_Type, Singular_Type)                         \
    SERVE_DELETE(plural, singular)                                                   \
    SERVE_CREATE(plural, singular, fields, opt_fields)                               \
    SERVE_UPDATE(plural, singular, fields, opt_fields)


#endif // !WEBC_TEMPLATE

