#ifndef SRC_DB_SQL_H_
#define SRC_DB_SQL_H_

#include <stdbool.h>
#include "module/nob.h"

/* Portable statement layer.
 *
 * One code path for every dialect: prepare/bind/step/columns are identical,
 * only the SQL *text* differs. Call sites pick text with a per-function
 * dialect table indexed by `db->lang`:
 *
 *     static const char *const q[SQL_LANG_COUNT] = {
 *         [SQL_SQLITE]  = "INSERT OR REPLACE INTO ...",
 *         [SQL_MYSQL]   = "INSERT ... ON DUPLICATE KEY UPDATE ...",
 *         [SQL_POSTGRES]= "INSERT ... ON CONFLICT ...",
 *     };
 *     sql_prepare(db, q[db->lang], &stmt);
 */

typedef enum {
    SQL_SQLITE = 0,
    SQL_MYSQL,
    SQL_POSTGRES,
    SQL_LANG_COUNT,
} sql_lang_t;

// Result of sql_step().
typedef enum {
    SQL_ERROR = -1,
    SQL_DONE = 0,
    SQL_ROW = 1,
} Sql_Step;

// Value handed to sql_bind(): placeholders are positional, indexes are
// 1-based in every dialect. SQL_NIL binds SQL NULL. SQL_SV binds text -
// except a view with data == NULL, which also binds SQL NULL (mirrors
// sqlite3_bind_text and keeps the form handlers' NULL-vs-"" distinction).
typedef enum {
    SQL_VAL_NIL,
    SQL_VAL_SV,
    SQL_VAL_I,
} Sql_Val_Kind;

typedef struct {
    Sql_Val_Kind kind;
    String_View  sv;  // SQL_VAL_SV
    long long    i;   // SQL_VAL_I
} sql_val;

#define SQL_SV(x) ((sql_val) { .kind = SQL_VAL_SV, .sv = (x) })
#define SQL_I(x)  ((sql_val) { .kind = SQL_VAL_I, .i = (long long) (x) })
#define SQL_NIL() ((sql_val) { .kind = SQL_VAL_NIL })

typedef struct db_t     db_t;
typedef struct sql_stmt sql_stmt;

// Driver vtable: one instance per dialect, chosen at db_open() time. Every
// function behaves the same across dialects or the driver reports failure.
typedef struct {
    bool        (*open)(db_t *db, const char *dsn);
    void        (*close)(db_t *db);
    bool        (*prepare)(db_t *db, const char *query, sql_stmt *out);
    Sql_Step    (*step)(sql_stmt *stmt);
    void        (*finalize)(sql_stmt *stmt);
    // NULL means SQL NULL. The pointer is valid only until the next
    // step()/finalize() on the same statement - copy it (temp_strdup) before
    // storing a row beyond that point.
    const char *(*column_text)(sql_stmt *stmt, int column);
    long long   (*column_int64)(sql_stmt *stmt, int column);
    int         (*column_count)(sql_stmt *stmt);
    bool        (*bind)(sql_stmt *stmt, int index /* 1-based */, sql_val value);
    bool        (*exec_script)(db_t *db, const char *script); // whole script, no params
    bool        (*txn_begin)(db_t *db);
    bool        (*txn_commit)(db_t *db);
    bool        (*txn_rollback)(db_t *db);
    const char *(*errmsg)(db_t *db);
} Sql_Driver;

struct db_t {
    sql_lang_t        lang;
    const Sql_Driver *drv;
    void             *conn;      // driver-private connection (sqlite3*, ...)
    char              err[256];  // fallback message while conn is unavailable
};

struct sql_stmt {
    db_t *db;
    void *stmt;                  // driver-private statement
};

/* -- lifecycle ------------------------------------------------------------
 * dsn: file path for sqlite (mysql/pg connection strings come with their
 * drivers). db is heap-allocated; db_close() closes the connection and frees
 * it. Returns NULL on failure (message already logged). */
db_t       *db_open(sql_lang_t lang, const char *dsn);
void        db_close(db_t *db);
const char *db_errmsg(db_t *db);

const Sql_Driver *sql_driver_for(sql_lang_t lang); // NULL when not built

// Creates `path` and every missing parent directory (mkdir(2) is not
// recursive). Shared by drivers and the legacy open path.
bool db_mkdir_recursive(const char *path);

/* -- statements ----------------------------------------------------------- */

static inline bool sql_prepare(db_t *db, const char *query, sql_stmt *out) {
    if (!db || !db->drv || !out) return false;
    out->db = db;
    out->stmt = NULL;
    if (!db->drv->prepare(db, query, out)) {
        nob_log(NOB_ERROR, "sql_prepare: %s: %s", query, db_errmsg(db));
        return false;
    }
    return true;
}

static inline Sql_Step sql_step(sql_stmt *stmt) {
    if (!stmt || !stmt->db || !stmt->db->drv || !stmt->stmt) return SQL_ERROR;
    return stmt->db->drv->step(stmt);
}

static inline void sql_finalize(sql_stmt *stmt) {
    if (!stmt || !stmt->db || !stmt->db->drv || !stmt->stmt) return;
    stmt->db->drv->finalize(stmt);
    stmt->stmt = NULL;
}

static inline const char *sql_column_text(sql_stmt *stmt, int column) {
    if (!stmt || !stmt->db || !stmt->db->drv || !stmt->stmt) return NULL;
    return stmt->db->drv->column_text(stmt, column);
}

static inline long long sql_column_int64(sql_stmt *stmt, int column) {
    if (!stmt || !stmt->db || !stmt->db->drv || !stmt->stmt) return 0;
    return stmt->db->drv->column_int64(stmt, column);
}

static inline int sql_column_count(sql_stmt *stmt) {
    if (!stmt || !stmt->db || !stmt->db->drv || !stmt->stmt) return 0;
    return stmt->db->drv->column_count(stmt);
}

// Short aliases (what call-site loops read best).
#define sql_col_text(stmt, column)  sql_column_text((stmt), (column))
#define sql_col_int64(stmt, column) sql_column_int64((stmt), (column))
#define sql_col_count(stmt)         sql_column_count(stmt)

static inline bool sql_bind(sql_stmt *stmt, int index, sql_val value) {
    if (!stmt || !stmt->db || !stmt->db->drv || !stmt->stmt) return false;
    if (index < 1) return false;
    if (!stmt->db->drv->bind(stmt, index, value)) {
        nob_log(NOB_ERROR, "sql_bind[%d]: %s", index, db_errmsg(stmt->db));
        return false;
    }
    return true;
}

// Steps a statement that must produce no rows (INSERT/UPDATE/DELETE/DDL).
static inline bool sql_final_step(sql_stmt *stmt) {
    Sql_Step rc = sql_step(stmt);
    if (rc == SQL_DONE) return true;
    nob_log(NOB_ERROR, "sql_final_step: %s",
            rc == SQL_ERROR ? db_errmsg(stmt->db) : "unexpected row");
    return false;
}

static inline bool sql_exec_script(db_t *db, const char *script) {
    if (!db || !db->drv || !script) return false;
    if (!db->drv->exec_script(db, script)) {
        nob_log(NOB_ERROR, "sql_exec_script: %s", db_errmsg(db));
        return false;
    }
    return true;
}

static inline bool sql_txn_begin(db_t *db) {
    if (!db || !db->drv) return false;
    if (!db->drv->txn_begin(db)) {
        nob_log(NOB_ERROR, "sql_txn_begin: %s", db_errmsg(db));
        return false;
    }
    return true;
}

static inline bool sql_txn_commit(db_t *db) {
    if (!db || !db->drv) return false;
    if (!db->drv->txn_commit(db)) {
        nob_log(NOB_ERROR, "sql_txn_commit: %s", db_errmsg(db));
        return false;
    }
    return true;
}

static inline bool sql_txn_rollback(db_t *db) {
    if (!db || !db->drv) return false;
    if (!db->drv->txn_rollback(db)) {
        nob_log(NOB_ERROR, "sql_txn_rollback: %s", db_errmsg(db));
        return false;
    }
    return true;
}

#endif  // SRC_DB_SQL_H_
