#ifndef SRC_USER_H_
#define SRC_USER_H_

#include <stddef.h>
#include "module/nob.h"
#include "../db/sql.h"
#include "core/display/paging.h"

/* Demo /users + /people row shape. The sidebar no longer reads this -
 * it shows the signed-in session user via auth_current_user() (Phase 13).
 * phone is required (0007: the contact every account must carry). */
typedef struct {
    const char *id;
    const char *name;
    const char *username;
    const char *email;
    const char *phone;
    const char *profile_pic;
} User;

DA_NEW(User, Users)

bool read_users(db_t *db, Users *rows, const Page_Info *slice);
bool count_users(db_t *db, size_t *out);
bool create_user(db_t *db, String_View *fields, size_t count);
bool update_user(db_t *db, String_View *fields, size_t count, String_View id);
bool delete_user(db_t *db, String_View id);

#endif // SRC_USER_H_
