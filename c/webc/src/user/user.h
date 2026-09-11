#ifndef SRC_USER_H_
#define SRC_USER_H_

#include <stddef.h>
#include <sqlite3.h>

typedef struct {
    int id;
    const char *name;
    const char *username;
    const char *email;
    const char *profile_pic;
} User;

typedef struct {
    User *items;
    size_t count;
    size_t capacity;
} Users;

static inline User user_data(void) {
    User u = {
        .name = "hello world",
        .username = "hello_world",
        .email = "helloworld1@gmail.com",
        .profile_pic = "/resource/image/know_me.png",
    };
    return u;
}

bool load_users(sqlite3 *db, Users *rows);
bool insert_user(sqlite3    *db,
                 const char *name,
                 const char *username,
                 const char *email,
                 const char *profile_pic);
bool update_user(sqlite3    *db,
                 const char *name,
                 const char *username,
                 const char *email,
                 const char *profile_pic,
                 int id);
bool delete_user(sqlite3 *db, int id);

#endif // SRC_USER_H_
