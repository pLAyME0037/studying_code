#include <stdbool.h>
#include <sqlite3.h>

#include "../db/db.h"
#include "user.h"
#include "../../nob.h"

bool load_users(sqlite3 *db, Users *rows) {
    bool result = true;
    sqlite3_stmt *stmt = NULL;

    String_View sql = sv_from_cstr("SELECT id, name, username, email, profile_pic "
          "FROM Users "
          "ORDER BY id ASC;");

    if (sqlite3_prepare_v2(db, sql.data, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }

    int ret = SQLITE_DONE;
    for (ret = sqlite3_step(stmt); ret == SQLITE_ROW; ret = sqlite3_step(stmt)) {
        int column = 0;
        int id           = sqlite3_column_int(stmt, column++);
        const char *name        = (const char *)sqlite3_column_text(stmt, column++);
        const char *username    = (const char *)sqlite3_column_text(stmt, column++);
        const char *email       = (const char *)sqlite3_column_text(stmt, column++);
        const char *profile_pic = (const char *)sqlite3_column_text(stmt, column++);
        da_append(rows, ((User) {
            .id          = id,
            .name        = name        ? temp_strdup(name)      : NULL,
            .username    = username    ? temp_strdup(username)  : NULL,
            .email       = email       ? temp_strdup(email)     : NULL,
            .profile_pic = profile_pic ? temp_strdup(profile_pic) : NULL,
        }));
    }

    if (ret != SQLITE_DONE) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }

defer:
    if (stmt) sqlite3_finalize(stmt);
    return result;
}

bool insert_user(sqlite3    *db,
                 const char *name,
                 const char *username,
                 const char *email,
                 const char *profile_pic)
{
    bool result = true;
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO Users (name, username, email, profile_pic) VALUES (?, ?, ?, ?);",
                           -1, &stmt, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_bind_text(stmt, 2, username, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_bind_text(stmt, 3, email, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_bind_text(stmt, 4, profile_pic, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }

defer:
    if (stmt) sqlite3_finalize(stmt);
    return result;
}

bool update_user(sqlite3    *db,
                 const char *name,
                 const char *username,
                 const char *email,
                 const char *profile_pic,
                 int id)
{
    bool result = true;
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(db, "UPDATE Users SET name = ?, username = ?, email = ?, profile_pic = ? WHERE id = ?;",
                           -1, &stmt, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_bind_text(stmt, 2, username, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_bind_text(stmt, 3, email, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_bind_text(stmt, 4, profile_pic, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_bind_int(stmt, 5, id) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }

defer:
    if (stmt) sqlite3_finalize(stmt);
    return result;
}

bool delete_user(sqlite3 *db, int id) {
    bool result = true;
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(db, "DELETE FROM Users WHERE id = ?;", -1, &stmt, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_bind_int(stmt, 1, id) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }

defer:
    if (stmt) sqlite3_finalize(stmt);
    return result;
}
