#include <stdbool.h>

#include "../db/db.h"
#include "user.h"
#include "core/display/paging.h"
#include "module/nob.h"

bool read_users(db_t *db, Users *rows, const Page_Info *slice) {
    bool result = true;
    sql_stmt stmt = {0};
    // Table name case differs: MySQL on Linux stores `users` (from the
    // migration file) and is case-sensitive there.
    // ORDER BY id ASC is the single line that decides list order.
    static const char *const q_all[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "SELECT id, name, username, email, profile_pic "
                         "FROM Users "
                         "ORDER BY id ASC;",
        [SQL_MYSQL]    = "SELECT id, name, username, email, profile_pic "
                         "FROM users "
                         "ORDER BY id ASC;",
        [SQL_POSTGRES] = "SELECT id, name, username, email, profile_pic "
                         "FROM users "
                         "ORDER BY id ASC;",
    };
    // Same query with the page window appended (limit/offset bound below).
    static const char *const q_page[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "SELECT id, name, username, email, profile_pic "
                         "FROM Users "
                         "ORDER BY id ASC LIMIT ? OFFSET ?;",
        [SQL_MYSQL]    = "SELECT id, name, username, email, profile_pic "
                         "FROM users "
                         "ORDER BY id ASC LIMIT ? OFFSET ?;",
        [SQL_POSTGRES] = "SELECT id, name, username, email, profile_pic "
                         "FROM users "
                         "ORDER BY id ASC LIMIT $1 OFFSET $2;",
    };

    if (!sql_prepare(db, slice ? q_page[db->lang] : q_all[db->lang], &stmt)) {
        return_defer(false);
    }
    if (slice) {
        if (!sql_bind(&stmt, 1, SQL_I(slice->per_page))) return_defer(false);
        if (!sql_bind(&stmt, 2, SQL_I(slice->offset)))    return_defer(false);
    }

    Sql_Step ret = SQL_DONE;
    for (ret = sql_step(&stmt); ret == SQL_ROW; ret = sql_step(&stmt)) {
        int column = 0;
        const char *id          = sql_column_text(&stmt, column++);
        const char *name        = sql_column_text(&stmt, column++);
        const char *username    = sql_column_text(&stmt, column++);
        const char *email       = sql_column_text(&stmt, column++);
        const char *profile_pic = sql_column_text(&stmt, column++);
        Users_add(rows, ((User) {
            .id          = id          ? temp_strdup(id)          : NULL,
            .name        = name        ? temp_strdup(name)        : NULL,
            .username    = username    ? temp_strdup(username)    : NULL,
            .email       = email       ? temp_strdup(email)       : NULL,
            .profile_pic = profile_pic ? temp_strdup(profile_pic) : NULL,
        }));
    }

    if (ret != SQL_DONE) {
        nob_log(NOB_ERROR, "read_users: %s", db_errmsg(db));
        return_defer(false);
    }

defer:
    sql_finalize(&stmt);
    return result;
}

bool count_users(db_t *db, size_t *out) {
    sql_stmt stmt = {0};
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "SELECT count(*) FROM Users;",
        [SQL_MYSQL]    = "SELECT count(*) FROM users;",
        [SQL_POSTGRES] = "SELECT count(*) FROM users;",
    };
    if (!sql_prepare(db, q[db->lang], &stmt)) return false;
    bool ok = false;
    if (sql_step(&stmt) == SQL_ROW) {
        *out = (size_t)sql_column_int64(&stmt, 0);
        ok = true;
    } else {
        nob_log(NOB_ERROR, "count_users: %s", db_errmsg(db));
    }
    sql_finalize(&stmt);
    return ok;
}

bool create_user(db_t *db, String_View *fields, size_t count) {
    bool result = true;

    if (count < 4) return false;
    String_View name        = fields[0];
    String_View username    = fields[1];
    String_View email       = fields[2];
    String_View profile_pic = fields[3];
    sql_stmt stmt = {0};
    // Conflict handling is the dialect-specific part (uuid PKs never collide
    // in practice, so MySQL/Postgres just insert).
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT OR REPLACE INTO Users "
                         "(name, username, email, profile_pic) "
                         "VALUES (?, ?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO users "
                         "(name, username, email, profile_pic) "
                         "VALUES (?, ?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO users "
                         "(name, username, email, profile_pic) "
                         "VALUES ($1, $2, $3, $4);",
    };

    if (!sql_prepare(db, q[db->lang], &stmt))     return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(name)))        return_defer(false);
    if (!sql_bind(&stmt, 2, SQL_SV(username)))    return_defer(false);
    if (!sql_bind(&stmt, 3, SQL_SV(email)))       return_defer(false);
    if (!sql_bind(&stmt, 4, SQL_SV(profile_pic))) return_defer(false);
    if (!sql_final_step(&stmt))                   return_defer(false);

defer:
    sql_finalize(&stmt);
    return result;
}

bool update_user(db_t *db, String_View *fields, size_t count, String_View id) {
    bool result = true;
    if (count < 4) return false;
    String_View name        = fields[0];
    String_View username    = fields[1];
    String_View email       = fields[2];
    String_View profile_pic = fields[3];
    sql_stmt stmt = {0};
    // profile_pic is kept as-is when the form did not carry a new file
    // (value NULL or empty), so editing name/email never wipes the picture.
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE Users SET name = ?, username = ?, email = ?, "
                         "profile_pic = COALESCE(NULLIF(?, ''), profile_pic) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE users SET name = ?, username = ?, email = ?, "
                         "profile_pic = COALESCE(NULLIF(?, ''), profile_pic) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE users SET name = $1, username = $2, email = $3, "
                         "profile_pic = COALESCE(NULLIF($4, ''), profile_pic) "
                         "WHERE id = $5;",
    };

    if (!sql_prepare(db, q[db->lang], &stmt))     return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(name)))        return_defer(false);
    if (!sql_bind(&stmt, 2, SQL_SV(username)))    return_defer(false);
    if (!sql_bind(&stmt, 3, SQL_SV(email)))       return_defer(false);
    if (!sql_bind(&stmt, 4, SQL_SV(profile_pic))) return_defer(false);
    if (!sql_bind(&stmt, 5, SQL_SV(id)))          return_defer(false);
    if (!sql_final_step(&stmt))                   return_defer(false);

defer:
    sql_finalize(&stmt);
    return result;
}

bool delete_user(db_t *db, String_View id) {
    bool result = true;
    sql_stmt stmt = {0};
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "DELETE FROM Users WHERE id = ?;",
        [SQL_MYSQL]    = "DELETE FROM users WHERE id = ?;",
        [SQL_POSTGRES] = "DELETE FROM users WHERE id = $1;",
    };

    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(id)))      return_defer(false);
    if (!sql_final_step(&stmt))               return_defer(false);

defer:
    sql_finalize(&stmt);
    return result;
}
