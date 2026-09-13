// template_runtime.h
#pragma once

#ifndef WEBC_TEMPLATE
#define WEBC_TEMPLATE

#include "module/nob.h"
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

// =========================================================================
// Helper macros for tt template
// =========================================================================

#define OUT(buf, size)       nob_sb_append_buf(sb, buf, size);
#define STR(x)               nob_sb_append_cstr(sb, (x) ? (x) : "");
#define INT(x)               nob_sb_appendf(sb, "%d", (x));
#define CLS(cond, t, f)      nob_sb_append_cstr(sb, (cond) ? (t) : (f));
#define NAV_ACTIVE(prefix)   (strncmp((current_path), (prefix), strlen(prefix)) == 0)
#define ESCAPED(x)           sb_append_html_escaped(sb, (x) ? (x) : "");
#define CURRENT_PATH         current_path
#define PAGE_TITLE           page_title

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
    void serve_##plural##_read(Serve_Context *sc, Nob_String_View method) { \
        NOB_UNUSED(method);                                               \
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

#define SERVE_CREATE(plural, singular, ...)                                   \
    void serve_##plural##_create(Serve_Context *sc) {                         \
        const char *fields[] = { __VA_ARGS__ };                               \
        size_t field_count = sizeof(fields) / sizeof(fields[0]);              \
        Nob_String_View values[field_count];                                  \
        Nob_String_View body_sv = sb_to_sv(sc->body);                         \
                                                                              \
        for (size_t i = 0; i < field_count; ++i) {                            \
            values[i] = (Nob_String_View){0};                                 \
            form_find_sv(body_sv, fields[i], &values[i]);                     \
        }                                                                     \
                                                                              \
        /* Validate that the primary/first field is not empty */              \
        if (field_count > 0 && values[0].count == 0) {                        \
            serve_error(sc, 400);                                             \
            return;                                                           \
        }                                                                     \
                                                                              \
        sqlite3 *db = open_webc_db();                                         \
        if (!db) { serve_error(sc, 500); return; }                           \
        if (!txn_begin(db)) { sqlite3_close(db); serve_error(sc, 500); return; } \
                                                                              \
        bool ok = insert_##singular(db, values, field_count);                 \
        if (ok) { txn_commit(db); }                                           \
        else    { txn_rollback(db); }                                         \
        sqlite3_close(db);                                                    \
                                                                              \
        if (!ok) { serve_error(sc, 500); return; }                            \
        http_render_redirect(sc, 302, "/" #plural);                          \
    }

#define SERVE_EDIT(plural, singular, Plural_Type, Singular_Type)             \
    void serve_##plural##_edit(Serve_Context *sc, Nob_String_View uri) {     \
        int id = 0;                                                          \
        if (!parse_id_from_uri(uri, "/" #plural "/", "/edit", &id)) {        \
            serve_error(sc, 404);                                            \
            return;                                                          \
        }                                                                    \
        Plural_Type dt = {0};                                                \
        sqlite3 *db = open_webc_db();                                        \
        if (!db) { serve_error(sc, 500); return; }                          \
        if (!load_##plural(db, &dt)) {                                       \
            sqlite3_close(db);                                               \
            serve_error(sc, 500);                                            \
            return;                                                          \
        }                                                                    \
        sqlite3_close(db);                                                   \
        Singular_Type *target = NULL;                                        \
        for (size_t i = 0; i < dt.count; ++i) {                              \
            if (dt.items[i].id == id) {                                      \
                target = &dt.items[i];                                       \
                break;                                                       \
            }                                                                \
        }                                                                    \
        if (!target) {                                                       \
            free(dt.items);                                                  \
            serve_error(sc, 404);                                            \
            return;                                                          \
        }                                                                    \
        sc->body.count = 0;                                                  \
        render_##plural##_edit_page(sc, *target);                            \
        free(dt.items);                                                      \
        http_render_response(sc, 200, "text/html", sb_to_sv(sc->body));      \
    }

#define SERVE_UPDATE(plural, singular, ...)                                   \
    void serve_##plural##_update(Serve_Context *sc, Nob_String_View uri) {    \
        int id = 0;                                                           \
        if (!parse_id_from_uri(uri, "/" #plural "/", "/update", &id)) {       \
            serve_error(sc, 404);                                             \
            return;                                                           \
        }                                                                     \
                                                                              \
        const char *fields[] = { __VA_ARGS__ };                               \
        size_t field_count = sizeof(fields) / sizeof(fields[0]);              \
        Nob_String_View values[field_count];                                  \
        Nob_String_View body_sv = sb_to_sv(sc->body);                         \
                                                                              \
        for (size_t i = 0; i < field_count; ++i) {                            \
            values[i] = (Nob_String_View){0};                                 \
            form_find_sv(body_sv, fields[i], &values[i]);                     \
        }                                                                     \
                                                                              \
        if (field_count > 0 && values[0].count == 0) {                        \
            serve_error(sc, 400);                                             \
            return;                                                           \
        }                                                                     \
                                                                              \
        sqlite3 *db = open_webc_db();                                         \
        if (!db) { serve_error(sc, 500); return; }                           \
        if (!txn_begin(db)) { sqlite3_close(db); serve_error(sc, 500); return; } \
                                                                              \
        bool ok = update_##singular(db, values, field_count, id);             \
        if (ok) { txn_commit(db); }                                           \
        else    { txn_rollback(db); }                                         \
        sqlite3_close(db);                                                    \
                                                                              \
        if (!ok) { serve_error(sc, 500); return; }                            \
        http_render_redirect(sc, 302, "/" #plural);                          \
    }

#define SERVE_DELETE(plural, singular)                                       \
    void serve_##plural##_delete(Serve_Context *sc, Nob_String_View uri) {   \
        int id = 0;                                                          \
        if (!parse_id_from_uri(uri, "/" #plural "/", "/delete", &id)) {      \
            serve_error(sc, 404);                                            \
            return;                                                          \
        }                                                                    \
        sqlite3 *db = open_webc_db();                                        \
        if (!db) { serve_error(sc, 500); return; }                          \
        if (!txn_begin(db)) { sqlite3_close(db); serve_error(sc, 500); return; } \
        bool ok = delete_##singular(db, id);                                 \
        if (ok) { txn_commit(db); }                                          \
        else    { txn_rollback(db); }                                        \
        sqlite3_close(db);                                                   \
        if (!ok) { serve_error(sc, 500); return; }                           \
        http_render_redirect(sc, 302, "/" #plural);                         \
    }

#define SERVE_CRUD(plural, singular, Plural_Type, Singular_Type, ...)        \
    SERVE_READ(plural, Plural_Type)                                          \
    SERVE_EDIT(plural, singular, Plural_Type, Singular_Type)                 \
    SERVE_DELETE(plural, singular)                                           \
    SERVE_CREATE(plural, singular, __VA_ARGS__)                              \
    SERVE_UPDATE(plural, singular, __VA_ARGS__)

static inline bool parse_id_from_uri(Nob_String_View uri,
                                     const char *prefix,
                                     const char *suffix,
                                     int *id)
{
    size_t plen = strlen(prefix);
    size_t slen = strlen(suffix);

    if (uri.count < plen + 1 + slen) return false;
    if (memcmp(uri.data, prefix, plen) != 0) return false;
    if (!nob_sv_ends_with(uri, nob_sv_from_cstr(suffix))) return false;

    Nob_String_View id_sv = {
        .data  = uri.data + plen,
        .count = uri.count - plen - slen,
    };

    if (id_sv.count == 0 || id_sv.count >= 32) return false;

    char buf[32] = {0};
    memcpy(buf, id_sv.data, id_sv.count);
    buf[id_sv.count] = '\0';

    char *end = NULL;
    long value = strtol(buf, &end, 10);
    if (end == buf || *end != '\0' || value < 0) return false;

    if (id) *id = (int)value;
    return true;
}

static inline bool form_find_sv(Nob_String_View body, const char *key, Nob_String_View *out) {
    Nob_String_View k = nob_sv_from_cstr(key);
    while (body.count > 0) {
        Nob_String_View pair = nob_sv_chop_by_delim(&body, '&');
        Nob_String_View name = nob_sv_chop_by_delim(&pair, '=');
        if (nob_sv_eq(name, k)) {
            if (out) *out = pair;
            return true;
        }
    }
    return false;
}
#endif // !WEBC_TEMPLATE

