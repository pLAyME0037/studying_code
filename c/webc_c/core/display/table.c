#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "table.h"

#include "module/webc_template.h"
#include "src/db/db.h"
#include "core/layout/header.h"
#include "core/layout/footer.h"
#include "core/http/utils.h"

void render_table_page(Serve_Context *sc) {
    PAGE_BEGIN(sc, "Table", "/table");
// #include "build/h_to_html/table.h"
    PAGE_END(sc);
}

void serve_table(Serve_Context *sc, String_View method) {
    UNUSED(method);

    sc->body.count = 0;
    render_table_page(sc);

    http_render_response(sc, 200, "text/html", sb_to_sv(sc->body));
}
