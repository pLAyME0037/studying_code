#include "module/nob.h"
#include "../auth/auth.h"
#include "../i18n/i18n.h"

// Sidebar identity HTML: the signed-in user's picture (ringed by account
// status) or an initials circle; nobody signed in -> generic workspace
// avatar. Temp-arena owned.
static const char *sidebar_avatar_html(const Auth_User *au) {
    const char *ring = "";
    if (au && au->status && au->status[0]) {
        if (strcmp(au->status, "ACTIVE") == 0)         ring = " ring-2 ring-blue";
        else if (strcmp(au->status, "INACTIVE") == 0)  ring = " ring-2 ring-peach";
        else if (strcmp(au->status, "SUSPENDED") == 0) ring = " ring-2 ring-red";
    }
    if (au && au->profile_pic && au->profile_pic[0]) {
        String_Builder sb = {0};
        sb_append_cstr(&sb, "<img src=\"");
        sb_append_html_escaped(&sb, au->profile_pic);
        sb_append_cstr(&sb, temp_sprintf(
            "\" alt=\"\" class=\"h-10 w-10 rounded-full object-cover shrink-0%s\">",
            ring));
        sb_append_null(&sb);
        return temp_strdup(sb.items);
    }
    const char *name = (au && au->name && au->name[0]) ? au->name : "";
    char initial[8] = {0};
    if (name[0]) {
        const unsigned char *p = (const unsigned char *) name;
        size_t n = 1;
        if ((p[0] & 0xE0) == 0xC0)      n = 2;
        else if ((p[0] & 0xF0) == 0xE0) n = 3;
        else if ((p[0] & 0xF8) == 0xF0) n = 4;
        memcpy(initial, name, n < sizeof(initial) ? n : sizeof(initial) - 1);
    }
    return temp_sprintf(
        "<span class=\"h-10 w-10 rounded-full shrink-0 inline-flex"
        " items-center justify-center text-sm font-semibold"
        " bg-surface1 text-subtext0%s\">%s</span>",
        ring, initial[0] ? initial : "?");
}

void render_page_header(String_Builder *sb,
                        const char     *page_title,
                        const char     *current_path)
{
    const Auth_User *au = auth_current_user();
    // Sidebar card: name + role (fallback email, then the workspace label).
    const char *u_name = (au && au->name && au->name[0]) ? au->name : "";
    const char *u_sub;
    if (au && au->role && au->role[0])       u_sub = au->role;
    else if (au && au->email && au->email[0]) u_sub = au->email;
    else                                      u_sub = "Staff workspace";
    const char *u_avatar = sidebar_avatar_html(au);
#define OUT(buf, size) sb_append_buf(sb, buf, size);
#define STR(x) sb_append_cstr(sb, (x) ? (x) : "");
#define CLS(cond, t, f) sb_append_cstr(sb, (cond) ? (t) : (f));
#define NAV_ACTIVE(prefix) (strncmp((current_path), (prefix), strlen(prefix)) == 0)
#define CURRENT_PATH current_path
#define PAGE_TITLE page_title
// Phase 12: <html lang> follows the active language, the top bar carries
// the language <select> (back = the page being rendered).
#define HTML_LANG i18n_html_lang()
#define LANG_FORM i18n_lang_form_html(current_path)
// Phase 13: sidebar card renders the signed-in session user (auth.c), not
// the first row of the users table.
#define SIDEBAR_AVATAR_RAW u_avatar
#define SIDEBAR_NAME u_name
#define SIDEBAR_SUB u_sub
#include "../../build/h_to_html/layout/header.h"
#undef SIDEBAR_SUB
#undef SIDEBAR_NAME
#undef SIDEBAR_AVATAR_RAW
#undef LANG_FORM
#undef HTML_LANG
#undef PAGE_TITLE
#undef CURRENT_PATH
#undef NAV_ACTIVE
#undef CLS
#undef STR
#undef OUT
}
