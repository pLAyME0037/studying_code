#include <stdbool.h>
#include <stdio.h>

#include "route.h"

#include "core/display/notes.h"
#include "core/display/version.h"
#include "core/display/people.h"
#include "core/display/user.h"
#include "core/display/pos.h"
#include "serve.h"

// Extract + classify the id segment from "/<path>/<id>/<suffix>".
// Ids are TEXT primary keys (legacy numeric strings or schema uuid defaults);
// route_id_parse() tags all-digits segments as ID_INT for INTEGER tables.
static bool parse_uri_id(String_View path,
                         String_View uri,
                         const char *suffix,
                         Route_Id   *id)
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
    return route_id_parse(id_sv, id);
}

// Reject ".." path segments anywhere in the URI (path traversal).
static bool uri_has_dotdot(String_View uri) {
    for (size_t i = 0; i + 1 < uri.count; ++i) {
        if (uri.data[i] != '.' || uri.data[i + 1] != '.') continue;
        bool seg_start = i == 0 || uri.data[i - 1] == '/';
        bool seg_end = i + 2 == uri.count || uri.data[i + 2] == '/';
        if (seg_start && seg_end) return true;
    }
    return false;
}

void serve_resource_route(Serve_Context *sc) {
    if (uri_has_dotdot(sc->uri)) {
        serve_error(sc, 404);
        return;
    }
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
        if (sv_ends_with(rest, sv_from_cstr(".webp"))) type_id = 7;
        if (sv_ends_with(rest, sv_from_cstr(".gif")))  type_id = 8;
        const char *content_type;
        switch (type_id) {
        case 1: content_type = "image/png";                      break;
        case 2: content_type = "image/jpeg";                     break;
        case 3: content_type = "image/svg+xml";                  break;
        case 4: content_type = "text/css; charset=utf-8";        break;
        case 5: content_type = "text/javascript; charset=utf-8"; break;
        case 6: content_type = "text/html; charset=utf-8";       break;
        case 7: content_type = "image/webp";                     break;
        case 8: content_type = "image/gif";                      break;
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
    // users master + their notes, one master-detail page
    route_new(&routes, "/people", NULL, "GET", ROUTE_EXACT, serve_people);

    // method NULL: serve_notes_api dispatches GET/POST/PUT/DELETE itself
    // and answers unsupported methods with 405.
    route_new(&routes, "/api/notes", NULL,  NULL,   ROUTE_EXACT,     serve_notes_api);
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

    // POS showcase pages (composite multi-field columns), same route shape
    route_new(&routes, "/pos/locations", NULL,       "GET",  ROUTE_EXACT,     serve_pos_locations);
    route_new(&routes, "/pos/locations/create", NULL, "POST", ROUTE_EXACT,    serve_pos_locations_create);
    route_new(&routes, "/pos/locations", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_locations_update);
    route_new(&routes, "/pos/locations", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_locations_delete);

    route_new(&routes, "/pos/users", NULL,       "GET",  ROUTE_EXACT,     serve_pos_users);
    route_new(&routes, "/pos/users/create", NULL, "POST", ROUTE_EXACT,    serve_pos_users_create);
    route_new(&routes, "/pos/users", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_users_update);
    route_new(&routes, "/pos/users", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_users_delete);

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
        case ROUTE_ID_ACTION: {
            Route_Id id = {0};
            if (parse_uri_id(sv_from_cstr(r->prefix), sc->uri, r->suffix, &id)) {
                sc->route_id = id;
                r->handle(sc);
                return;
            }
        }
        break;
        }
    }

    serve_error(sc, 404);
}
