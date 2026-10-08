#include "module/nob.h"

#include "core/http/serve.h"
#include "src/db/open_db.h"

// Page chrome profile. The demo app's users table is gone; static identity.
typedef struct {
    const char *name;
    const char *email;
    const char *profile_pic;
} Header_Profile;

void render_page_header(String_Builder *sb,
                        const char     *page_title,
                        const char     *current_path)
{
    Header_Profile u = {
        .name        = "webc",
        .email       = "SQLite Admin",
        .profile_pic = "/resource/image/know_me.png",
    };

    // Active database pill: shown while a user-selected file is open
    // (basename + read-only marker, full path in the tooltip).
    const char *active_pill = NULL;
    if (WEBC_ACTIVE_DB_PATH) {
        const char *base = strrchr(WEBC_ACTIVE_DB_PATH, '/');
        base = (base && base[1]) ? base + 1 : WEBC_ACTIVE_DB_PATH;
        String_Builder pill = {0};
        sb_append_cstr(&pill,
            "<span title=\"");
        sb_append_html_escaped(&pill, WEBC_ACTIVE_DB_PATH);
        sb_append_cstr(&pill,
            "\" class=\"db-pill\">");
        sb_append_html_escaped(&pill, base);
        if (WEBC_ACTIVE_READONLY) {
            sb_append_cstr(&pill,
                "<span class=\"ro\">ro</span>");
        }
        sb_append_cstr(&pill, "</span>");
        sb_append_null(&pill);
        active_pill = temp_sprintf("%.*s", (int) pill.count, pill.items);
        sb_free(pill);
    }

#define OUT(buf, size) sb_append_buf(sb, buf, size);
#define STR(x) sb_append_cstr(sb, (x) ? (x) : "");
#define CLS(cond, t, f) sb_append_cstr(sb, (cond) ? (t) : (f));
#define NAV_ACTIVE(prefix) (strncmp((current_path), (prefix), strlen(prefix)) == 0)
#define CURRENT_PATH current_path
#define PAGE_TITLE page_title
#define ACTIVE_PILL active_pill
#include "../../build/h_to_html/layout/header.h"
#undef ACTIVE_PILL
#undef PAGE_TITLE
#undef CURRENT_PATH
#undef NAV_ACTIVE
#undef CLS
#undef STR
#undef OUT
}
