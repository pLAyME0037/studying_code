#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "user.h"
#include "master_child.h"

#include "module/webc_template.h"
#include "src/user/user.h"
#include "src/db/db.h"
#include "core/layout/header.h"
#include "core/layout/footer.h"
#include "core/http/utils.h"

// ---- /people master column shape (composed in people.c) ----------------
MD_Column md_users_columns[] = {
    { .name = "name",        .label = "Name",     .type = COL_TYPE_TEXT, .nullable = false },
    { .name = "username",    .label = "Username", .type = COL_TYPE_TEXT, .nullable = false },
    { .name = "email",       .label = "Email",    .type = COL_TYPE_TEXT, .nullable = false },
    { .name = "phone",       .label = "Phone",    .type = COL_TYPE_TEXT, .nullable = false },
    { .name = "profile_pic", .label = "Picture",  .type = COL_TYPE_BLOB, .nullable = true },
};
const size_t md_users_columns_count = ARRAY_LEN(md_users_columns);

void render_users_page(Serve_Context *sc,
                       Users          users,
                       Page_Info      page_info,
                       bool           fragment)
{
    // PAGE_BEGIN's declarations, minus the header: ?fragment=all responses
    // are the bare pagination store (rows + per-page <template> pagers).
    String_Builder *sb         = &(sc)->body;
    const char *page_title     = "Users";
    const char *current_path   = "/users";
    String_View page_query     = sc->query_string;
    const char *page_base      = current_path;
    const char *page_key       = "page";
    const char *page_container = "users-tbody";
    if (!fragment) render_page_header(sb, page_title, current_path);
#include "build/h_to_html/user.h"
    if (!fragment) render_page_footer(sb);
}

void render_users_edit_page(Serve_Context *sc, User user) {
    PAGE_BEGIN(sc, "Edit User",  "/users");
#include "build/h_to_html/user_edit.h"
    PAGE_END(sc);
}
// profile_pic is optional: the /people master form has no file input, so a
// missing key binds NULL (fresh pic); the real upload forms still carry it.
// phone is required (0007) - every users form carries the input.
static const char *fields[] = { "name", "username", "email", "phone" };
static const char *opt_fields[] = { "profile_pic" };
SERVE_CRUD(users, user, Users, User, fields, opt_fields)

