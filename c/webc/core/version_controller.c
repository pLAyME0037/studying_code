#include "version_controller.h"
#include "../build/bundle.h"

void render_version_page(Serve_Context *sc) {
    String_Builder *sb = &sc->body;
    render_page_header(sb, "Version", "/version");
#define OUT(buf, size) sb_append_buf(sb, buf, size);
#define STR(x) sb_append_cstr(sb, (x) ? (x) : "");
#include "../auto_ctrl/cttochtml/version.h"
#undef STR
#undef OUT
    render_page_footer(sb);
}

void serve_version_page(Serve_Context *sc) {
    sc->body.count = 0;
    render_version_page(sc);
    http_render_response(sc, 200, "text/html", sb_to_sv(sc->body));
}
