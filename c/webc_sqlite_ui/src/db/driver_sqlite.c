#include <stdio.h>
#include <string.h>

#define NOB_STRIP_PREFIX
#include "module/nob.h"

#include "sqlite3.h"
#include "sql.h"
#include "open_db.h"

static bool sqlite_drv_open(db_t *db, const char *dsn) {
    // User-selected files arrive as sqlite URIs ("file:..." from
    // user_db_dsn()); the application database is a plain path.
    bool user_file = strncmp(dsn, "file:", 5) == 0;

    // The db file lives in a directory that may not exist yet.
    const char *slash = strrchr(dsn, '/');
    if (!user_file && slash && slash != dsn) {
        const char *dir = temp_sprintf("%.*s", (int) (slash - dsn), dsn);
        if (!db_mkdir_recursive(dir)) {
            snprintf(db->err, sizeof(db->err), "cannot create directory %s",
                     dir);
            return false;
        }
    }

    sqlite3 *conn = NULL;
    int rc = user_file
             ? sqlite3_open_v2(dsn, &conn, user_db_open_flags(dsn), NULL)
             : sqlite3_open(dsn, &conn);
    if (rc != SQLITE_OK) {
        snprintf(db->err, sizeof(db->err), "%s",
                 conn ? sqlite3_errmsg(conn) : "sqlite3_open failed");
        if (conn) sqlite3_close(conn);
        return false;
    }
    db->conn = conn;

    if (user_file) {
        // Opening somebody's database must not modify it: no journal_mode /
        // synchronous (those rewrite the file header). foreign_keys is
        // per-connection, so it is safe even read-only.
        if (sqlite3_exec(conn, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL)
                != SQLITE_OK) {
            snprintf(db->err, sizeof(db->err), "%s", sqlite3_errmsg(conn));
            sqlite3_close(conn);
            db->conn = NULL;
            return false;
        }
        return true;
    }

    // journal_mode/synchronous must run outside transactions; foreign_keys
    // is per-connection, so all three belong to connection setup.
    if (sqlite3_exec(conn, "PRAGMA journal_mode = WAL;", NULL, NULL, NULL)
            != SQLITE_OK
        || sqlite3_exec(conn, "PRAGMA synchronous = NORMAL;", NULL, NULL, NULL)
            != SQLITE_OK
        || sqlite3_exec(conn, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL)
            != SQLITE_OK) {
        snprintf(db->err, sizeof(db->err), "%s", sqlite3_errmsg(conn));
        sqlite3_close(conn);
        db->conn = NULL;
        return false;
    }
    return true;
}

static void sqlite_drv_close(db_t *db) {
    sqlite3 *conn = db->conn;
    if (!conn) return;
    if (sqlite3_close(conn) != SQLITE_OK) {
        // A statement was leaked. Report it, then close_v2() defers the real
        // release until that statement gets finalized.
        snprintf(db->err, sizeof(db->err), "sqlite3_close: %s",
                 sqlite3_errmsg(conn));
        nob_log(NOB_ERROR, "%s", db->err);
        sqlite3_close_v2(conn);
    }
    db->conn = NULL;
}

static bool sqlite_drv_prepare(db_t *db, const char *query, sql_stmt *out) {
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2((sqlite3 *) db->conn, query, -1, &stmt, NULL)
        != SQLITE_OK) {
        if (stmt) sqlite3_finalize(stmt);  // v2 can leave a partial statement
        out->stmt = NULL;
        return false;
    }
    out->stmt = stmt;
    return true;
}

static Sql_Step sqlite_drv_step(sql_stmt *stmt) {
    switch (sqlite3_step((sqlite3_stmt *) stmt->stmt)) {
    case SQLITE_ROW:
        return SQL_ROW;
    case SQLITE_DONE:
        return SQL_DONE;
    default:
        return SQL_ERROR;  // message read via sqlite_drv_errmsg()
    }
}

static void sqlite_drv_finalize(sql_stmt *stmt) {
    sqlite3_finalize((sqlite3_stmt *) stmt->stmt);
}

static const char *sqlite_drv_column_text(sql_stmt *stmt, int column) {
    return (const char *) sqlite3_column_text((sqlite3_stmt *) stmt->stmt,
                                              column);
}

static long long sqlite_drv_column_int64(sql_stmt *stmt, int column) {
    return (long long) sqlite3_column_int64((sqlite3_stmt *) stmt->stmt,
                                            column);
}

static int sqlite_drv_column_count(sql_stmt *stmt) {
    return sqlite3_column_count((sqlite3_stmt *) stmt->stmt);
}

static bool sqlite_drv_bind(sql_stmt *stmt, int index, sql_val value) {
    sqlite3_stmt *st = stmt->stmt;
    int rc;
    switch (value.kind) {
    case SQL_VAL_NIL:
        rc = sqlite3_bind_null(st, index);
        break;
    case SQL_VAL_SV:
        // SQLITE_TRANSIENT copies the bytes: the source String_View often
        // points into the request body, which must not be required to outlive
        // this statement. data is passed through verbatim: a NULL data pointer
        // binds SQL NULL (the old sqlite3_bind_text() behavior the forms rely
        // on for "no file uploaded"), a non-NULL view of length 0 binds "".
        rc = sqlite3_bind_text(st, index, value.sv.data, (int) value.sv.count,
                               SQLITE_TRANSIENT);
        break;
    case SQL_VAL_I:
        rc = sqlite3_bind_int64(st, index, value.i);
        break;
    }
    return rc == SQLITE_OK;
}

static bool sqlite_drv_exec_script(db_t *db, const char *script) {
    char *err = NULL;
    if (sqlite3_exec((sqlite3 *) db->conn, script, NULL, NULL, &err)
        != SQLITE_OK) {
        snprintf(db->err, sizeof(db->err), "%s",
                 err ? err : "sqlite3_exec failed");
        sqlite3_free(err);
        return false;
    }
    return true;
}

static bool sqlite_drv_txn_begin(db_t *db) {
    return sqlite3_exec((sqlite3 *) db->conn, "BEGIN;", NULL, NULL, NULL)
           == SQLITE_OK;
}

static bool sqlite_drv_txn_commit(db_t *db) {
    return sqlite3_exec((sqlite3 *) db->conn, "COMMIT;", NULL, NULL, NULL)
           == SQLITE_OK;
}

static bool sqlite_drv_txn_rollback(db_t *db) {
    return sqlite3_exec((sqlite3 *) db->conn, "ROLLBACK;", NULL, NULL, NULL)
           == SQLITE_OK;
}

static const char *sqlite_drv_errmsg(db_t *db) {
    if (db->conn) return sqlite3_errmsg((sqlite3 *) db->conn);
    return db->err[0] ? db->err : "sqlite: no connection";
}

const Sql_Driver drv_sqlite = {
    .open         = sqlite_drv_open,
    .close        = sqlite_drv_close,
    .prepare      = sqlite_drv_prepare,
    .step         = sqlite_drv_step,
    .finalize     = sqlite_drv_finalize,
    .column_text  = sqlite_drv_column_text,
    .column_int64 = sqlite_drv_column_int64,
    .column_count = sqlite_drv_column_count,
    .bind         = sqlite_drv_bind,
    .exec_script  = sqlite_drv_exec_script,
    .txn_begin    = sqlite_drv_txn_begin,
    .txn_commit   = sqlite_drv_txn_commit,
    .txn_rollback = sqlite_drv_txn_rollback,
    .errmsg       = sqlite_drv_errmsg,
};
