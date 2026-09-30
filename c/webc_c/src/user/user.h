#ifndef SRC_USER_H_
#define SRC_USER_H_

#include <stddef.h>
#include "module/nob.h"
#include "../db/sql.h"

typedef struct {
    const char *id;
    const char *name;
    const char *username;
    const char *email;
    const char *profile_pic;
} User;

DA_NEW(User, Users)

static inline User user_data(void) {
    User u = {
        .name = "hello world",
        .username = "hello_world",
        .email = "helloworld1@gmail.com",
        .profile_pic = "/resource/image/know_me.png",
    };
    return u;
}

bool read_users(db_t *db, Users *rows);
bool create_user(db_t *db, String_View *fields, size_t count);
bool update_user(db_t *db, String_View *fields, size_t count, String_View id);
bool delete_user(db_t *db, String_View id);

#endif // SRC_USER_H_
