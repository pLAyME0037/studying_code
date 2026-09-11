#include "../../nob.h"
#include "../../src/db/db.h"
#include "../../src/user/user.h"

void render_page_header(String_Builder *sb,
                        const char     *page_title,
                        const char     *current_path)
{
    User u = user_data();
    Users users = {0};
    sqlite3 *db = open_webc_db();
    if (db) {
        if (load_users(db, &users) && users.count > 0) {
            User row = users.items[0];
            if (row.name        && row.name[0])        u.name        = row.name;
            if (row.username    && row.username[0])    u.username    = row.username;
            if (row.email       && row.email[0])       u.email       = row.email;
            if (row.profile_pic && row.profile_pic[0]) u.profile_pic = row.profile_pic;
        }
        sqlite3_close(db);
        free(users.items);
    }
#define OUT(buf, size) sb_append_buf(sb, buf, size);
#define STR(x) sb_append_cstr(sb, (x) ? (x) : "");
#define CLS(cond, t, f) sb_append_cstr(sb, (cond) ? (t) : (f));
#define NAV_ACTIVE(prefix) (strncmp((current_path), (prefix), strlen(prefix)) == 0)
#define CURRENT_PATH current_path
#define PAGE_TITLE page_title
#include "../../build/h_to_html/layout/header.h"
#undef PAGE_TITLE
#undef CURRENT_PATH
#undef NAV_ACTIVE
#undef CLS
#undef STR
#undef OUT
}
