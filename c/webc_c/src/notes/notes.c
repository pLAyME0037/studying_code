#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOB_STRIP_PREFIX
#include "module/nob.h"

#include "sqlite3.h"
#include "notes.h"
#include "../db/db.h"

bool load_notes(sqlite3 *db, Notes *notes) {
    bool result = true;
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT id, user_id, title, datetime(created_at, 'localtime'), body FROM Notes ORDER BY created_at DESC;";

    int ret = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }

    for (ret = sqlite3_step(stmt); ret == SQLITE_ROW; ret = sqlite3_step(stmt)) {
        int column             = 0;
        const char *id         = (const char *)sqlite3_column_text(stmt, column++);
        const char *user_id    = (const char *)sqlite3_column_text(stmt, column++);
        const char *title      = (const char *)sqlite3_column_text(stmt, column++);
        const char *created_at = (const char *)sqlite3_column_text(stmt, column++);
        const char *body       = (const char *)sqlite3_column_text(stmt, column++);
        Notes_add(notes, ((Note) {
            .id         = id         ? temp_strdup(id)         : NULL,
            .user_id    = user_id    ? temp_strdup(user_id)    : NULL,
            .title      = title      ? temp_strdup(title)      : NULL,
            .created_at = created_at ? temp_strdup(created_at) : NULL,
            .body       = body       ? temp_strdup(body)       : NULL,
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

bool insert_note(sqlite3 *db, String_View *values, size_t count) {
    bool result = true;

    if (count < 2) return false;
    String_View title  = values[0];
    String_View body   = values[1];
    sqlite3_stmt *stmt = NULL;
    const char *sql =  "INSERT INTO Notes (title, body) VALUES (?, ?);";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_bind_text(stmt, 1, title.data, (int)title.count, SQLITE_TRANSIENT) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_bind_text(stmt, 2, body.data, (int)body.count, SQLITE_TRANSIENT) != SQLITE_OK) {
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

bool update_note(sqlite3 *db, String_View *values, size_t count, String_View id) {
    bool result = true;

    if (count < 2) return false;
    String_View title = values[0];
    String_View body  = values[1];
    sqlite3_stmt *stmt = NULL;
    const char *sql = "UPDATE Notes SET title = ?, body = ? WHERE id = ?;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_bind_text(stmt, 1, title.data, (int)title.count, SQLITE_TRANSIENT) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_bind_text(stmt, 2, body.data, (int)body.count, SQLITE_TRANSIENT) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_bind_text(stmt, 3, id.data, (int)id.count, SQLITE_TRANSIENT) != SQLITE_OK) {
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

bool delete_note(sqlite3 *db, String_View id) {
    bool result = true;
    sqlite3_stmt *stmt = NULL;
    const char *sql = "DELETE FROM Notes WHERE id = ?;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_bind_text(stmt, 1, id.data, (int)id.count, SQLITE_TRANSIENT) != SQLITE_OK) {
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
