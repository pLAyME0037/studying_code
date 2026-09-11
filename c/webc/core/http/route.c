#include <stdio.h>

#include "route.h"
#include "../display/notes.h"
#include "../display/table.h"
#include "../display/version.h"
#include "../display/crud.h"
#include "../display/crud_modules.h"
#include "../display/user.h"

static const Crud_Module *crud_find_module(String_View uri) {
    const Crud_Module *best = NULL;
    for (size_t i = 0; i < crud_modules_count; ++i) {
        String_View p = sv_from_cstr(crud_modules[i]->path);
        if (!sv_starts_with(uri, p)) continue;
        if (!best || strlen(crud_modules[i]->path) > strlen(best->path)) best = crud_modules[i];
    }
    return best;
}

// Extract the integer id from "/<path>/<id>/<suffix>".
static bool parse_uri_id(String_View path,
                         String_View uri,
                         const char *suffix,
                         int        *id)
{
    String_View prefix;
    prefix = sv_from_cstr(temp_sprintf("%.*s/", (int)path.count, path.data));
    if (uri.count <= prefix.count
        || memcmp(uri.data, prefix.data, prefix.count) != 0) {
        return false;
    }
    String_View suffix_sv;
    suffix_sv = sv_from_cstr(suffix);
    if (uri.count <= suffix_sv.count) return false;
    if (memcmp(uri.data + uri.count - suffix_sv.count, suffix_sv.data, suffix_sv.count) != 0) {
        return false;
    }

    String_View id_sv = {
        .data  = uri.data + prefix.count,
        .count = uri.count - prefix.count - suffix_sv.count,
    };
    char buf[32] = {0};
    snprintf(buf, sizeof(buf), "%.*s", (int)id_sv.count, id_sv.data);
    char *end = NULL;
    long value = strtol(buf, &end, 10);
    if (end == buf || *end != '\0') return false;
    *id = (int)value;
    return true;
}

static bool crud_parse_id(const Crud_Module *mod,
                          String_View        uri,
                          const char        *suffix,
                          int               *id)
{
    return parse_uri_id(sv_from_cstr(mod->path), uri, suffix, id);
}

void serve_resource_route(Serve_Context *sc, String_View uri) {
    if (sv_starts_with(uri, sv_from_cstr("/js/"))) {
        String_View rest = { .data = uri.data + 4, .count = uri.count - 4 };
        String_Builder path = {0};
        sb_append_cstr(&path, "./js/");
        sb_append_sv(&path, rest);
        sb_append_null(&path);
        // determine content_type (text/javascript for .js)
        serve_resource(sc, path.items, "text/javascript; charset=utf-8");
        return;
    }
    if (sv_eq(uri, sv_from_cstr("/css/output.css"))) {
        serve_resource(sc, "./css/output.css", "text/css; charset=utf-8");
        return;
    }
    if (sv_eq(uri, sv_from_cstr("/favicon.ico"))) {
        serve_resource(sc, "./resource/image/user1.png", "image/png");
        return;
    }
    if (sv_starts_with(uri, sv_from_cstr("/resource/"))) {
        String_View resource_prefix = sv_from_cstr("/resource/");
        String_View rest = {
            .data  = uri.data + resource_prefix.count,
            .count = uri.count - resource_prefix.count,
        };
        String_Builder path = {0};
        sb_append_cstr(&path, "./resource/");
        sb_append_sv(&path, rest);
        sb_append_null(&path);

        int type_id;
        if (sv_ends_with(rest, sv_from_cstr(".png")))  type_id = 1;
        if (sv_ends_with(rest, sv_from_cstr(".jpg")))  type_id = 2;
        if (sv_ends_with(rest, sv_from_cstr(".svg")))  type_id = 3;
        if (sv_ends_with(rest, sv_from_cstr(".css")))  type_id = 4;
        if (sv_ends_with(rest, sv_from_cstr(".js")))   type_id = 5;
        if (sv_ends_with(rest, sv_from_cstr(".html"))) type_id = 6;
        const char *content_type;
        switch (type_id) {
        case 1: content_type = "image/png";                      break;
        case 2: content_type = "image/jpeg";                     break;
        case 3: content_type = "image/svg+xml";                  break;
        case 4: content_type = "text/css; charset=utf-8";        break;
        case 5: content_type = "text/javascript; charset=utf-8"; break;
        case 6: content_type = "text/html; charset=utf-8";       break;
        default: content_type = "application/octet-stream";      break;
        }
        serve_resource(sc, path.items, content_type);
        return;
    }
    serve_error(sc, 404);
}

void serve_dashboard(Serve_Context *sc);

#define CMP_URI(dst_uri, src_uri) sv_eq((dst_uri), (sv_from_cstr(src_uri)))

void route_request(Serve_Context *sc, String_View method, String_View uri) {
    if (CMP_URI(uri, "/")) {
        serve_dashboard(sc);
        return;
    }
    if (CMP_URI(uri, "/table")) {
        serve_table(sc, method);
        return;
    }
    if (CMP_URI(uri, "/notes")) {
        serve_notes(sc, method);
        return;
    }
    if (CMP_URI(uri, "/version")) {
        serve_version_page(sc);
        return;
    }
    if (CMP_URI(uri, "/api/notes")) {
        serve_notes_api(sc, method);
        return;
    }
    if (CMP_URI(method, "POST") && CMP_URI(uri, "/notes/create")) {
        serve_notes_create(sc);
        return;
    }
    if (CMP_URI(uri, "/users")) {
        serve_users(sc, method);
        return;
    }
    if (CMP_URI(method, "POST") && CMP_URI(uri, "/users/create")) {
        serve_users_create(sc);
        return;
    }
    int user_id = 0;
    if (CMP_URI(method, "GET")
        && parse_uri_id(sv_from_cstr("/users"), uri, "/edit", &user_id)) {
        serve_users_edit(sc, uri);
        return;
    }
    if (CMP_URI(method, "POST")
        && parse_uri_id(sv_from_cstr("/users"), uri, "/update", &user_id)) {
        serve_users_update(sc, uri);
        return;
    }
    if (CMP_URI(method, "POST")
        && parse_uri_id(sv_from_cstr("/users"), uri, "/delete", &user_id)) {
        serve_users_delete(sc, uri);
        return;
    }

    // CRUD modules routing
    const Crud_Module *crud_mod = crud_find_module(uri);
    if (crud_mod) {
        String_View path = sv_from_cstr(crud_mod->path);
        if (sv_eq(uri, path)) {
            if (CMP_URI(method, "GET")) {
                serve_crud_list(sc, crud_mod);
                return;
            }
            serve_error(sc, 405);
            return;
        }

        int id = 0;
        if (CMP_URI(method, "POST")
            && sv_ends_with(uri, sv_from_cstr("/create"))
            && sv_starts_with(uri, path)) {
            serve_crud_create(sc, crud_mod);
            return;
        }
        if (CMP_URI(method, "GET")
            && crud_parse_id(crud_mod, uri, "/edit", &id)) {
            serve_crud_edit(sc, crud_mod, id);
            return;
        }
        if (CMP_URI(method, "POST")
            && crud_parse_id(crud_mod, uri, "/update", &id)) {
            serve_crud_update(sc, crud_mod, id);
            return;
        }
        if (CMP_URI(method, "POST")
            && crud_parse_id(crud_mod, uri, "/delete", &id)) {
            serve_crud_delete(sc, crud_mod, id);
            return;
        }
    }

    if (sv_starts_with(uri, sv_from_cstr("/css/"))
        || sv_starts_with(uri, sv_from_cstr("/js/"))
        || sv_starts_with(uri, sv_from_cstr("/resource/"))
        || CMP_URI(uri, "/favicon.ico")) {
        serve_resource_route(sc, uri);
        return;
    }

    serve_error(sc, 404);
}
