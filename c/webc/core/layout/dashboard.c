#include "../../nob.h"
#include "dashboard.h"
#include "../../core/display/crud_modules.h"
#include "../../core/layout/header.h"
#include "../../core/layout/footer.h"
#include "../../src/dashboard/dashboard.h"

static void render_dashboard_page(String_Builder  *sb,
                                  Dashboard_Cards *custom) {
#define OUT(buf, size) sb_append_buf(sb, buf, size);
#define INT(v) sb_append_cstr(sb, temp_sprintf("%d", v));
#define LLINT(v) sb_append_cstr(sb, temp_sprintf("%lld", v));
#define STR(s) sb_append_cstr(sb, s);
#define ESCAPED(s) sb_append_html_escaped(sb, s ? s : "");
#define PAGE_TITLE(s) sb_append_cstr(sb, s);
#include "../../build/h_to_html/layout/dashboard.h"
#undef OUT
#undef INT
#undef LLINT
#undef STR
#undef ESCAPED
#undef PAGE_TITLE
}

void serve_dashboard(Serve_Context *sc) {
    String_Builder *sb = &sc->body;

    Dashboard_Cards custom = {0};
    dashboard_custom_cards(&custom);

    render_page_header(sb, "Main Form", "/");
    render_dashboard_page(sb, &custom);
    render_page_footer(sb);

    http_render_response(sc, 200, "text/html", sb_to_sv(sc->body));
}
