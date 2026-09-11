#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "user.h"

#include "src/user/user.h"
#include "src/db/db.h"
#include "core/layout/header.h"
#include "core/layout/footer.h"
#include "core/http/utils.h"

static bool parse_user_id_from_uri(String_View uri, const char *suffix, int *id) {
    const char *prefix = "/users/";
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

void render_users_page(Serve_Context *sc, Users users) {
    String_Builder *sb = &sc->body;
    render_page_header(sb, "Users", "/users");
#define OUT(buf, size) sb_append_buf(sb, buf, size);
#define INT(x) sb_appendf(sb, "%d", (x));
#define STR(x) sb_append_cstr(sb, (x) ? (x) : "");
#define ESCAPED(x) sb_append_html_escaped(sb, (x) ? (x) : "");
#define PAGE_TITLE "Users"
#include "build/h_to_html/user.h"
#undef PAGE_TITLE
#undef ESCAPED
#undef STR
#undef INT
#undef OUT
    render_page_footer(sb);
}

void render_user_edit_page(Serve_Context *sc, User user) {
    String_Builder *sb = &sc->body;
    render_page_header(sb, "Edit User", "/users");
#define OUT(buf, size) sb_append_buf(sb, buf, size);
#define INT(x) sb_appendf(sb, "%d", (x));
#define STR(x) sb_append_cstr(sb, (x) ? (x) : "");
#define ESCAPED(x) sb_append_html_escaped(sb, (x) ? (x) : "");
#define PAGE_TITLE "Edit User"
#include "build/h_to_html/user_edit.h"
#undef PAGE_TITLE
#undef ESCAPED
#undef STR
#undef INT
#undef OUT
    render_page_footer(sb);
}

void serve_users(Serve_Context *sc, String_View method) {
    UNUSED(method);

    Users users = {0};
    sqlite3 *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }
    if (!load_users(db, &users)) {
        sqlite3_close(db);
        serve_error(sc, 500);
        return;
    }
    sqlite3_close(db);

    sc->body.count = 0;
    render_users_page(sc, users);
    free(users.items);

    http_render_response(sc, 200, "text/html", sb_to_sv(sc->body));
}

void serve_users_create(Serve_Context *sc) {
    char name[512] = {0};
    char username[512] = {0};
    char email[512] = {0};
    char profile_pic[512] = {0};

    if (!form_find(sb_to_sv(sc->body), "name", name, sizeof(name)) || name[0] == '\0') {
        serve_error(sc, 400);
        return;
    }
    form_find(sb_to_sv(sc->body), "username", username, sizeof(username));
    form_find(sb_to_sv(sc->body), "email", email, sizeof(email));
    form_find(sb_to_sv(sc->body), "profile_pic", profile_pic, sizeof(profile_pic));

    sqlite3 *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }
    if (!txn_begin(db)) { sqlite3_close(db); serve_error(sc, 500); return; }
    bool ok = insert_user(db, name, username, email, profile_pic);
    txn_commit(db);
    sqlite3_close(db);

    if (!ok) { serve_error(sc, 500); return; }
    http_render_redirect(sc, 302, "/users");
}

void serve_users_edit(Serve_Context *sc, String_View uri) {
    int id = 0;
    if (!parse_user_id_from_uri(uri, "/edit", &id)) {
        serve_error(sc, 404);
        return;
    }

    Users users = {0};
    sqlite3 *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }
    if (!load_users(db, &users)) {
        sqlite3_close(db);
        serve_error(sc, 500);
        return;
    }
    sqlite3_close(db);

    User *target = NULL;
    for (size_t i = 0; i < users.count; ++i) {
        if (users.items[i].id == id) {
            target = &users.items[i];
            break;
        }
    }
    if (!target) {
        free(users.items);
        serve_error(sc, 404);
        return;
    }

    sc->body.count = 0;
    render_user_edit_page(sc, *target);
    free(users.items);

    http_render_response(sc, 200, "text/html", sb_to_sv(sc->body));
}

void serve_users_update(Serve_Context *sc, String_View uri) {
    int id = 0;
    if (!parse_user_id_from_uri(uri, "/update", &id)) {
        serve_error(sc, 404);
        return;
    }

    char name[512] = {0};
    char username[512] = {0};
    char email[512] = {0};
    char profile_pic[512] = {0};

    if (!form_find(sb_to_sv(sc->body), "name", name, sizeof(name)) || name[0] == '\0') {
        serve_error(sc, 400);
        return;
    }
    form_find(sb_to_sv(sc->body), "username", username, sizeof(username));
    form_find(sb_to_sv(sc->body), "email", email, sizeof(email));
    form_find(sb_to_sv(sc->body), "profile_pic", profile_pic, sizeof(profile_pic));

    sqlite3 *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }
    if (!txn_begin(db)) { sqlite3_close(db); serve_error(sc, 500); return; }
    bool ok = update_user(db, name, username, email, profile_pic, id);
    txn_commit(db);
    sqlite3_close(db);

    if (!ok) { serve_error(sc, 500); return; }
    http_render_redirect(sc, 302, "/users");
}

void serve_users_delete(Serve_Context *sc, String_View uri) {
    int id = 0;
    if (!parse_user_id_from_uri(uri, "/delete", &id)) {
        serve_error(sc, 404);
        return;
    }

    sqlite3 *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }
    if (!txn_begin(db)) { sqlite3_close(db); serve_error(sc, 500); return; }
    bool ok = delete_user(db, id);
    txn_commit(db);
    sqlite3_close(db);

    if (!ok) { serve_error(sc, 500); return; }
    http_render_redirect(sc, 302, "/users");
}
