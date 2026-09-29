#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "user.h"

#include "module/webc_template.h"
#include "src/user/user.h"
#include "src/db/db.h"
#include "core/layout/header.h"
#include "core/layout/footer.h"
#include "core/http/utils.h"

void render_users_page(Serve_Context *sc, Users users) {
    PAGE_BEGIN(sc, "Users",  "/users");
#include "build/h_to_html/user.h"
    PAGE_END(sc);
}

void render_users_edit_page(Serve_Context *sc, User user) {
    PAGE_BEGIN(sc, "Edit User",  "/users");
#include "build/h_to_html/user_edit.h"
    PAGE_END(sc);
}
static const char *fields[] = { "name", "username", "email", "profile_pic" };
SERVE_CRUD(users, user, Users, User, fields)

