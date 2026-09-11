#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "table.h"

#include "src/db/db.h"
#include "core/layout/header.h"
#include "core/layout/footer.h"
#include "core/http/utils.h"

void render_table_page(Serve_Context *sc) {
    String_Builder *sb = &sc->body;
    render_page_header(sb, "Table", "/table");
#define OUT(buf, size) sb_append_buf(sb, buf, size);
#define INT(x) sb_appendf(sb, "%d", (x));
#define STR(x) sb_append_cstr(sb, (x) ? (x) : "");
#define ESCAPED(x) sb_append_html_escaped(sb, (x) ? (x) : "");
#define PAGE_TITLE "Table"
#include "build/h_to_html/table.h"
#undef PAGE_TITLE
#undef ESCAPED
#undef STR
#undef INT
#undef OUT
    render_page_footer(sb);
}

void serve_table(Serve_Context *sc, String_View method) {
    UNUSED(method);

    sc->body.count = 0;
    render_table_page(sc);

    http_render_response(sc, 200, "text/html", sb_to_sv(sc->body));
}
