#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOB_STRIP_PREFIX
#include "../nob.h"

#include "sqlite3.h"
#include "notes.h"
#include "db.h"

bool load_notes(sqlite3 *db, Notes *notes) {
    bool result = true;
    sqlite3_stmt *stmt = NULL;

    int ret = sqlite3_prepare_v2(db,
        "SELECT id, title, datetime(created_at, 'localtime'), body FROM Notes ORDER BY created_at DESC;",
        -1, &stmt, NULL);
    if (ret != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }

    for (ret = sqlite3_step(stmt); ret == SQLITE_ROW; ret = sqlite3_step(stmt)) {
        int column = 0;
        int id = sqlite3_column_int(stmt, column++);
        const char *title = (const char *)sqlite3_column_text(stmt, column++);
        const char *created_at = (const char *)sqlite3_column_text(stmt, column++);
        const char *body = (const char *)sqlite3_column_text(stmt, column++);
        da_append(notes, ((Note) {
            .id         = id,
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

bool insert_note(sqlite3 *db, const char *title, const char *body)
{
    bool result = true;
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(db, "INSERT INTO Notes (title, body) VALUES (?, ?);", -1, &stmt, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_bind_text(stmt, 1, title, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_bind_text(stmt, 2, body, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
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

bool update_note(sqlite3 *db, int id, const char *title, const char *body)
{
    bool result = true;
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(db, "UPDATE Notes SET title = ?, body = ? WHERE id = ?;", -1, &stmt, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_bind_text(stmt, 1, title, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_bind_text(stmt, 2, body, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_bind_int(stmt, 3, id) != SQLITE_OK) {
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

bool delete_note(sqlite3 *db, int id)
{
    bool result = true;
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(db, "DELETE FROM Notes WHERE id = ?;", -1, &stmt, NULL) != SQLITE_OK) {
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