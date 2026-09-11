#include <stdio.h>

#include "route.h"
#include "notes_controller.h"
#include "version_controller.h"

#include "../src/user.h"

void serve_resource_route(Serve_Context *sc, String_View uri) {
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

        const char *content_type = "application/octet-stream";
        if (sv_ends_with(rest, sv_from_cstr(".png")))  content_type = "image/png";
        if (sv_ends_with(rest, sv_from_cstr(".jpg")))  content_type = "image/jpeg";
        if (sv_ends_with(rest, sv_from_cstr(".svg")))  content_type = "image/svg+xml";
        if (sv_ends_with(rest, sv_from_cstr(".css")))  content_type = "text/css; charset=utf-8";
        if (sv_ends_with(rest, sv_from_cstr(".js")))   content_type = "text/javascript; charset=utf-8";
        if (sv_ends_with(rest, sv_from_cstr(".html"))) content_type = "text/html; charset=utf-8";
        serve_resource(sc, path.items, content_type);
        return;
    }
    serve_error(sc, 404);
}

void route_request(Serve_Context *sc, String_View method, String_View uri) {
    if (sv_eq(uri, sv_from_cstr("/")) || sv_eq(uri, sv_from_cstr("/notes"))) {
        serve_notes(sc, method);
        return;
    }
    if (sv_eq(uri, sv_from_cstr("/version"))) {
        serve_version_page(sc);
        return;
    }
    if (sv_eq(uri, sv_from_cstr("/api/notes"))) {
        serve_notes_api(sc, method);
        return;
    }
    if (sv_eq(method, sv_from_cstr("POST")) &&
        sv_eq(uri, sv_from_cstr("/notes/create"))) {
        serve_notes_create(sc);
        return;
    }
    if (sv_eq(method, sv_from_cstr("GET"))       &&
        sv_ends_with(uri, sv_from_cstr("/edit")) &&
        sv_starts_with(uri, sv_from_cstr("/notes/"))) {
        serve_notes_edit(sc, uri);
        return;
    }
    if (sv_eq(method, sv_from_cstr("POST"))        &&
        sv_ends_with(uri, sv_from_cstr("/update")) &&
        sv_starts_with(uri, sv_from_cstr("/notes/"))) {
        serve_notes_update(sc, uri);
        return;
    }
    if (sv_eq(method, sv_from_cstr("POST"))        &&
        sv_ends_with(uri, sv_from_cstr("/delete")) &&
        sv_starts_with(uri, sv_from_cstr("/notes/"))) {
        serve_notes_delete(sc, uri);
        return;
    }
    if (sv_starts_with(uri, sv_from_cstr("/css/"))      ||
        sv_starts_with(uri, sv_from_cstr("/resource/")) ||
        sv_eq(uri, sv_from_cstr("/favicon.ico"))) {
        serve_resource_route(sc, uri);
        return;
    }

    serve_error(sc, 404);
}
