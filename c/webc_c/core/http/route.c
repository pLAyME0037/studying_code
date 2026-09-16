#include <stdbool.h>
#include <stdio.h>

#include "route.h"

#include "core/display/notes.h"
#include "core/display/table.h"
#include "core/display/version.h"
#include "core/display/crud.h"
#include "core/display/crud_modules.h"
#include "core/display/user.h"
#include "serve.h"

// Extract the integer id from "/<path>/<id>/<suffix>".
static bool parse_uri_id(String_View path,
                         String_View uri,
                         const char *suffix,
                         int        *id)
{
    char prefix_buf[128];
    snprintf(prefix_buf, sizeof(prefix_buf), "%.*s/", (int)path.count, path.data);
    String_View prefix = sv_from_cstr(prefix_buf);
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
    if (uri.count <= prefix.count + suffix_sv.count) return false;

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

void serve_resource_route(Serve_Context *sc) {
    if (sv_starts_with(sc->uri, sv_from_cstr("/js/"))) {
        String_View rest = {
            .data  = sc->uri.data + 4,
            .count = sc->uri.count - 4
        };
        String_Builder path = {0};
        sb_append_cstr(&path, "./js/");
        sb_append_sv(&path, rest);
        sb_append_null(&path);
        // determine content_type (text/javascript for .js)
        serve_resource(sc, path.items, "text/javascript; charset=utf-8");
        sb_free(path);
        return;
    }
    if (sv_eq(sc->uri, sv_from_cstr("/css/output.css"))) {
        serve_resource(sc, "./css/output.css", "text/css; charset=utf-8");
        return;
    }
    if (sv_eq(sc->uri, sv_from_cstr("/favicon.ico"))) {
        serve_resource(sc, "./resource/image/user1.png", "image/png");
        return;
    }
    if (sv_starts_with(sc->uri, sv_from_cstr("/resource/"))) {
        String_View resource_prefix = sv_from_cstr("/resource/");
        String_View rest = {
            .data  = sc->uri.data + resource_prefix.count,
            .count = sc->uri.count - resource_prefix.count,
        };
        String_Builder path = {0};
        sb_append_cstr(&path, "./resource/");
        sb_append_sv(&path, rest);
        sb_append_null(&path);

        int type_id = 0;
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
        sb_free(path);
        return;
    }
    serve_error(sc, 404);
}

void serve_dashboard(Serve_Context *sc);

#define CMP_URI(dst_uri, src_uri)   sv_eq((dst_uri), (sv_from_cstr(src_uri)))
#define START_URI(dst_uri, src_uri) sv_starts_with((dst_uri), (sv_from_cstr(src_uri)))

static void route_new(route_da     *routes,
                      const char   *prefix,
                      const char   *suffix,
                      const char   *method,
                      route_kind    kind,
                      route_handler handle)
{
    route_da_add(routes, (route_t) {
        .prefix = prefix,
        .suffix = suffix,
        .method = method,
        .kind   = kind,
        .handle = handle,
    });
}

static route_da routes = {0};
static bool route_init = false;

void route_initialize(void) {
    route_new(&routes, "/", NULL, "GET", ROUTE_EXACT, serve_dashboard);
    route_new(&routes, "/version", NULL, "GET", ROUTE_EXACT, serve_version_page);
    route_new(&routes, "/table", NULL, "GET", ROUTE_EXACT, serve_table);

    route_new(&routes, "/api/notes", NULL,  "GET",  ROUTE_EXACT,     serve_notes_api);
    route_new(&routes, "/notes", NULL,      "GET",  ROUTE_EXACT,     serve_notes_read);
    route_new(&routes, "/notes/create", NULL, "POST", ROUTE_EXACT,   serve_notes_create);
    route_new(&routes, "/notes", "/edit",   "GET",  ROUTE_ID_ACTION, serve_notes_edit);
    route_new(&routes, "/notes", "/update", "POST", ROUTE_ID_ACTION, serve_notes_update);
    route_new(&routes, "/notes", "/delete", "POST", ROUTE_ID_ACTION, serve_notes_delete);

    route_new(&routes, "/users", NULL,      "GET",  ROUTE_EXACT,     serve_users_read);
    route_new(&routes, "/users/create", NULL, "POST", ROUTE_EXACT,   serve_users_create);
    route_new(&routes, "/users", "/edit",   "GET",  ROUTE_ID_ACTION, serve_users_edit);
    route_new(&routes, "/users", "/update", "POST", ROUTE_ID_ACTION, serve_users_update);
    route_new(&routes, "/users", "/delete", "POST", ROUTE_ID_ACTION, serve_users_delete);

    route_new(&routes, "/css/",        NULL, NULL, ROUTE_PREFIX, serve_resource_route);
    route_new(&routes, "/js/",         NULL, NULL, ROUTE_PREFIX, serve_resource_route);
    route_new(&routes, "/resource/",   NULL, NULL, ROUTE_PREFIX, serve_resource_route);
    route_new(&routes, "/favicon.ico", NULL, NULL, ROUTE_EXACT,  serve_resource_route);

    route_init = true;
}

void route_request(Serve_Context *sc) {
    if (!route_init) {
        route_initialize();
    }

    for (size_t i = 0; i < routes.count; ++i) {
        const route_t *r = &routes.items[i];

        if (r->method && !CMP_URI(sc->method, r->method)) continue;
        switch (r->kind) {
        case ROUTE_EXACT:
            if (CMP_URI(sc->uri, r->prefix)) {
                r->handle(sc);
                return;
            }
        break;
        case ROUTE_PREFIX:
            if (START_URI(sc->uri, r->prefix)) {
                r->handle(sc);
                return;
            }
        break;
        case ROUTE_ID_ACTION:
            int id = 0;
            if (parse_uri_id(sv_from_cstr(r->prefix), sc->uri, r->suffix, &id)) {
                sc->route_id = id;
                r->handle(sc);
                return;
            }
        break;
        }
    }

    serve_error(sc, 404);
}
