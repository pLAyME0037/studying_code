// Postgres driver stub: the dialect is first-class in every q[] table and
// migration path, but the wire protocol is not implemented - db_open() fails
// with a clear message instead of pretending to connect.
#include <stdio.h>

#include "sql.h"

static bool pg_drv_open(db_t *db, const char *dsn) {
    (void) dsn;
    if (db) {
        snprintf(db->err, sizeof(db->err), "postgres driver is not implemented yet");
    }
    return false;
}

static void pg_drv_close(db_t *db) {
    (void) db;
}

static bool pg_drv_prepare(db_t *db, const char *query, sql_stmt *out) {
    (void) db;
    (void) query;
    (void) out;
    return false;
}

static Sql_Step pg_drv_step(sql_stmt *stmt) {
    (void) stmt;
    return SQL_ERROR;
}

static void pg_drv_finalize(sql_stmt *stmt) {
    (void) stmt;
}

static const char *pg_drv_column_text(sql_stmt *stmt, int column) {
    (void) stmt;
    (void) column;
    return NULL;
}

static long long pg_drv_column_int64(sql_stmt *stmt, int column) {
    (void) stmt;
    (void) column;
    return 0;
}

static int pg_drv_column_count(sql_stmt *stmt) {
    (void) stmt;
    return 0;
}

static bool pg_drv_bind(sql_stmt *stmt, int index, sql_val value) {
    (void) stmt;
    (void) index;
    (void) value;
    return false;
}

static bool pg_drv_exec_script(db_t *db, const char *script) {
    (void) db;
    (void) script;
    return false;
}

static bool pg_drv_txn_begin(db_t *db) {
    (void) db;
    return false;
}

static bool pg_drv_txn_commit(db_t *db) {
    (void) db;
    return false;
}

static bool pg_drv_txn_rollback(db_t *db) {
    (void) db;
    return false;
}

static const char *pg_drv_errmsg(db_t *db) {
    if (db && db->err[0]) return db->err;
    return "postgres driver is not implemented yet";
}

const Sql_Driver drv_postgres = {
    .open         = pg_drv_open,
    .close        = pg_drv_close,
    .prepare      = pg_drv_prepare,
    .step         = pg_drv_step,
    .finalize     = pg_drv_finalize,
    .column_text  = pg_drv_column_text,
    .column_int64 = pg_drv_column_int64,
    .column_count = pg_drv_column_count,
    .bind         = pg_drv_bind,
    .exec_script  = pg_drv_exec_script,
    .txn_begin    = pg_drv_txn_begin,
    .txn_commit   = pg_drv_txn_commit,
    .txn_rollback = pg_drv_txn_rollback,
    .errmsg       = pg_drv_errmsg,
};
