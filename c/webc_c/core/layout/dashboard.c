#include "module/nob.h"
#include "dashboard.h"
#include "../../core/layout/header.h"
#include "../../core/layout/footer.h"
#include "../../src/dashboard/dashboard.h"

static void render_dashboard_page(String_Builder *sb, Dashboard_Data *ws) {
#define OUT(buf, size) sb_append_buf(sb, buf, size);
#define INT(v) sb_append_cstr(sb, temp_sprintf("%d", v));
#define LLINT(v) sb_append_cstr(sb, temp_sprintf("%lld", v));
#define STR(s) sb_append_cstr(sb, s);
#define ESCAPED(s) sb_append_html_escaped(sb, s ? s : "");
#define PAGE_TITLE(s) sb_append_cstr(sb, s);
#define CLS(cond, t, f) sb_append_cstr(sb, (cond) ? (t) : (f));
#include "../../build/h_to_html/layout/dashboard.h"
#undef OUT
#undef INT
#undef LLINT
#undef STR
#undef ESCAPED
#undef PAGE_TITLE
#undef CLS
}

void serve_dashboard(Serve_Context *sc) {
    String_Builder *sb = &sc->body;

    // Phase 14: one template, three role centers (see src/dashboard/).
    Dashboard_Data ws = {0};
    dashboard_load(&ws, auth_current_user());

    render_page_header(sb, "Dashboard", "/dashboard");
    render_dashboard_page(sb, &ws);
    render_page_footer(sb);

    http_render_response(sc, 200, "text/html", sb_to_sv(sc->body));
}
