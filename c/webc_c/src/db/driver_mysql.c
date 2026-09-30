// MariaDB/MySQL driver behind the Sql_Driver vtable. Callers only ever see
// sql_*; dialect differences live in the q[] tables at call sites and in the
// naive `;` split of exec_script() below (migration files are written to be
// split-safe: no semicolons inside comments or strings).
#include <stdlib.h>
#include <string.h>

#include <mysql.h>

#define NOB_STRIP_PREFIX
#include "module/nob.h"

#include "sql.h"

// Initial size of a text result buffer per column. Values that do not fit
// (long TEXT bodies) are grown on demand: the fetch reports
// MYSQL_DATA_TRUNCATED and the column is re-read with
// mysql_stmt_fetch_column() into a buffer of the exact reported length.
#define MYSQL_TEXT_CAP 1024

typedef struct {
    MYSQL_STMT *stmt;
    bool        executed;
    bool        has_result;

    // input params (sql_bind's 1-based index maps to index-1)
    unsigned long  nparams;
    MYSQL_BIND    *pbind;
    char         **ptext;   // owned copy of a bound text value (NULL = SQL NULL)
    long long     *pnum;
    unsigned long *plen;
    my_bool       *pnull;

    // result columns
    int            ncols;
    MYSQL_BIND    *rbind;
    char         **rtext;   // owned text buffers (grown on truncation)
    unsigned long *rlen;
    long long     *rnum;
    my_bool       *rnull;
    bool          *rint;    // column bound as an integer
} Mysql_Stmt;

static bool mysql_fail(db_t *db, Mysql_Stmt *s, const char *what) {
    snprintf(db->err, sizeof(db->err), "%s: %s", what,
             s && s->stmt ? mysql_stmt_error(s->stmt) : "no statement");
    return false;
}

static void mysql_stmt_free(Mysql_Stmt *s) {
    if (!s) return;
    if (s->stmt) mysql_stmt_close(s->stmt);
    for (unsigned long i = 0; s->ptext && i < s->nparams; ++i) free(s->ptext[i]);
    free(s->ptext);
    free(s->pnum);
    free(s->plen);
    free(s->pnull);
    free(s->pbind);
    for (int i = 0; s->rtext && i < s->ncols; ++i) free(s->rtext[i]);
    free(s->rtext);
    free(s->rlen);
    free(s->rnum);
    free(s->rnull);
    free(s->rint);
    free(s->rbind);
    free(s);
}

/* -- connection ----------------------------------------------------------- */

// DSN: "key=value;key=value;..." with keys host, port, user, password,
// database, socket (the "mysql:" prefix is already stripped by the caller).
static bool mysql_drv_open(db_t *db, const char *dsn) {
    const char *host = NULL, *user = NULL, *pass = NULL;
    const char *database = NULL, *socket = NULL;
    unsigned int port = 0;

    char *copy = temp_sprintf("%s", dsn);
    for (char *save = NULL, *tok = strtok_r(copy, ";", &save); tok;
         tok = strtok_r(NULL, ";", &save)) {
        char *eq = strchr(tok, '=');
        if (!eq) {
            snprintf(db->err, sizeof(db->err), "malformed DSN segment `%s`", tok);
            return false;
        }
        *eq = '\0';
        const char *k = tok, *v = eq + 1;
        if (strcmp(k, "host") == 0) host = v;
        else if (strcmp(k, "port") == 0) port = (unsigned int) atoi(v);
        else if (strcmp(k, "user") == 0) user = v;
        else if (strcmp(k, "password") == 0) pass = v;
        else if (strcmp(k, "database") == 0) database = v;
        else if (strcmp(k, "socket") == 0) socket = v;
        else {
            snprintf(db->err, sizeof(db->err), "unknown DSN key `%s`", k);
            return false;
        }
    }

    MYSQL *conn = mysql_init(NULL);
    if (!conn) {
        snprintf(db->err, sizeof(db->err), "mysql_init failed");
        return false;
    }
    if (!mysql_real_connect(conn, host, user, pass, database, port, socket, 0)) {
        snprintf(db->err, sizeof(db->err), "mysql: %s", mysql_error(conn));
        mysql_close(conn);
        return false;
    }
    db->conn = conn;
    return true;
}

static void mysql_drv_close(db_t *db) {
    if (!db->conn) return;
    mysql_close((MYSQL *) db->conn);
    db->conn = NULL;
}

static const char *mysql_drv_errmsg(db_t *db) {
    if (db->err[0]) return db->err;
    if (db->conn) {
        const char *e = mysql_error((MYSQL *) db->conn);
        if (e && e[0]) return e;
    }
    return "mysql: no connection";
}

static bool mysql_exec_raw(db_t *db, const char *sql) {
    if (mysql_query((MYSQL *) db->conn, sql) != 0) {
        snprintf(db->err, sizeof(db->err), "mysql: %s",
                 mysql_error((MYSQL *) db->conn));
        return false;
    }
    return true;
}

// A fragment counts as a statement when it has at least one line that is
// neither blank nor a "--"/"#" comment. Trailing comment-only fragments (or
// whitespace after the last `;`) are skipped instead of sent to the server,
// which would reject them with "Query was empty".
static bool fragment_has_stmt(const char *s, size_t len) {
    for (size_t i = 0; i < len;) {
        size_t eol = i;
        while (eol < len && s[eol] != '\n') ++eol;
        size_t j = i;
        while (j < eol && (s[j] == ' ' || s[j] == '\t' || s[j] == '\r')) ++j;
        if (j < eol) {
            size_t rest = eol - j;
            bool comment = s[j] == '#'
                || (rest >= 2 && s[j] == '-' && s[j + 1] == '-');
            if (!comment) return true;
        }
        i = eol + 1;
    }
    return false;
}

// Naive `;` splitter - migration files are written so this is safe (no `;`
// inside comments or string literals).
static bool mysql_drv_exec_script(db_t *db, const char *script) {
    const char *p = script;
    while (*p) {
        const char *semi = strchr(p, ';');
        size_t len = semi ? (size_t) (semi - p) : strlen(p);

        if (fragment_has_stmt(p, len)) {
            char *stmt_text = temp_sprintf("%.*s", (int) len, p);
            if (!mysql_exec_raw(db, stmt_text)) return false;
            // Single statement (we split them apart): drain a possible result
            // set so the connection is ready for the next one.
            if (mysql_field_count((MYSQL *) db->conn)) {
                MYSQL_RES *res = mysql_store_result((MYSQL *) db->conn);
                if (!res) {
                    snprintf(db->err, sizeof(db->err), "mysql: %s",
                             mysql_error((MYSQL *) db->conn));
                    return false;
                }
                mysql_free_result(res);
            }
        }

        if (!semi) break;
        p = semi + 1;
    }
    return true;
}

// MySQL DDL commits implicitly, so these are best-effort bookkeeping around
// plain statements - no different from what the mysql CLI would do.
static bool mysql_drv_txn_begin(db_t *db) {
    return mysql_exec_raw(db, "START TRANSACTION;");
}

static bool mysql_drv_txn_commit(db_t *db) {
    return mysql_exec_raw(db, "COMMIT;");
}

static bool mysql_drv_txn_rollback(db_t *db) {
    return mysql_exec_raw(db, "ROLLBACK;");
}

/* -- statements ------------------------------------------------------------ */

// Integer columns are bound as LONGLONG (exact sql_column_int64()). Floats,
// decimals and everything else (TEXT, VARCHAR, BLOB, DATETIME, ...) are bound
// as STRING so text reads keep the server's representation ("2.5", not 2).
static bool mysql_is_int_type(enum enum_field_types t) {
    return t == MYSQL_TYPE_TINY || t == MYSQL_TYPE_SHORT
        || t == MYSQL_TYPE_INT24 || t == MYSQL_TYPE_LONG
        || t == MYSQL_TYPE_LONGLONG;
}

static bool mysql_drv_prepare(db_t *db, const char *query, sql_stmt *out) {
    Mysql_Stmt *s = calloc(1, sizeof(*s));
    if (!s) {
        snprintf(db->err, sizeof(db->err), "out of memory");
        return false;
    }

    s->stmt = mysql_stmt_init((MYSQL *) db->conn);
    if (!s->stmt) {
        snprintf(db->err, sizeof(db->err), "mysql_stmt_init: %s",
                 mysql_error((MYSQL *) db->conn));
        free(s);
        return false;
    }
    if (mysql_stmt_prepare(s->stmt, query, (unsigned long) strlen(query)) != 0) {
        mysql_fail(db, s, "mysql_stmt_prepare");
        mysql_stmt_free(s);
        return false;
    }

    s->nparams = mysql_stmt_param_count(s->stmt);
    if (s->nparams) {
        s->pbind = calloc(s->nparams, sizeof(MYSQL_BIND));
        s->ptext = calloc(s->nparams, sizeof(char *));
        s->pnum  = calloc(s->nparams, sizeof(long long));
        s->plen  = calloc(s->nparams, sizeof(unsigned long));
        s->pnull = calloc(s->nparams, sizeof(my_bool));
        if (!s->pbind || !s->ptext || !s->pnum || !s->plen || !s->pnull) {
            snprintf(db->err, sizeof(db->err), "out of memory");
            mysql_stmt_free(s);
            return false;
        }
        for (unsigned long i = 0; i < s->nparams; ++i) {
            MYSQL_BIND *b = &s->pbind[i];
            // Unbound params act as SQL NULL until sql_bind() says otherwise.
            s->pnull[i] = 1;
            b->buffer_type = MYSQL_TYPE_STRING;
            b->buffer = (void *) "";
            b->length = &s->plen[i];
            b->is_null = &s->pnull[i];
        }
    }

    MYSQL_RES *meta = mysql_stmt_result_metadata(s->stmt);
    if (meta) {
        s->has_result = true;
        MYSQL_FIELD *fields = mysql_fetch_fields(meta);
        unsigned int n = mysql_num_fields(meta);
        s->ncols = (int) n;
        s->rbind = calloc(n, sizeof(MYSQL_BIND));
        s->rtext = calloc(n, sizeof(char *));
        s->rnum  = calloc(n, sizeof(long long));
        s->rlen  = calloc(n, sizeof(unsigned long));
        s->rnull = calloc(n, sizeof(my_bool));
        s->rint  = calloc(n, sizeof(bool));
        if (!s->rbind || !s->rtext || !s->rnum || !s->rlen || !s->rnull
            || !s->rint) {
            snprintf(db->err, sizeof(db->err), "out of memory");
            mysql_free_result(meta);
            mysql_stmt_free(s);
            return false;
        }
        for (unsigned int i = 0; i < n; ++i) {
            MYSQL_BIND *b = &s->rbind[i];
            b->length = &s->rlen[i];
            b->is_null = &s->rnull[i];
            if (mysql_is_int_type(fields[i].type)) {
                s->rint[i] = true;
                b->buffer_type = MYSQL_TYPE_LONGLONG;
                b->buffer = &s->rnum[i];
                b->buffer_length = sizeof(long long);
            } else {
                s->rtext[i] = malloc(MYSQL_TEXT_CAP);
                if (!s->rtext[i]) {
                    snprintf(db->err, sizeof(db->err), "out of memory");
                    mysql_free_result(meta);
                    mysql_stmt_free(s);
                    return false;
                }
                b->buffer_type = MYSQL_TYPE_STRING;
                b->buffer = s->rtext[i];
                b->buffer_length = MYSQL_TEXT_CAP - 1;
            }
        }
        mysql_free_result(meta);
    }

    out->stmt = s;
    return true;
}

// After a truncated fetch: grow each too-small text buffer to the exact
// length the server reported, re-read the value, then re-install the bind
// array so following rows land in the grown buffers too.
static bool mysql_fix_truncated(db_t *db, Mysql_Stmt *s) {
    bool grew = false;
    for (int i = 0; i < s->ncols; ++i) {
        MYSQL_BIND *b = &s->rbind[i];
        if (b->buffer_type != MYSQL_TYPE_STRING || s->rnull[i]) continue;
        unsigned long len = s->rlen[i];
        if (len <= b->buffer_length) continue;
        char *buf = realloc(s->rtext[i], len + 1);
        if (!buf) {
            snprintf(db->err, sizeof(db->err), "out of memory growing result buffer");
            return false;
        }
        s->rtext[i] = buf;
        b->buffer = buf;
        b->buffer_length = len;
        if (mysql_stmt_fetch_column(s->stmt, b, (unsigned int) i, 0) != 0) {
            return mysql_fail(db, s, "mysql_stmt_fetch_column");
        }
        grew = true;
    }
    if (grew && mysql_stmt_bind_result(s->stmt, s->rbind) != 0) {
        return mysql_fail(db, s, "mysql_stmt_bind_result");
    }
    return true;
}

static Sql_Step mysql_drv_step(sql_stmt *stmt) {
    Mysql_Stmt *s = stmt->stmt;
    db_t *db = stmt->db;

    if (!s->executed) {
        if (s->nparams
            && mysql_stmt_bind_param(s->stmt, s->pbind) != 0) {
            mysql_fail(db, s, "mysql_stmt_bind_param");
            return SQL_ERROR;
        }
        if (mysql_stmt_execute(s->stmt) != 0) {
            mysql_fail(db, s, "mysql_stmt_execute");
            return SQL_ERROR;
        }
        s->executed = true;
        if (s->has_result && mysql_stmt_bind_result(s->stmt, s->rbind) != 0) {
            mysql_fail(db, s, "mysql_stmt_bind_result");
            return SQL_ERROR;
        }
    }
    if (!s->has_result) return SQL_DONE;  // INSERT/UPDATE/DELETE/DDL

    switch (mysql_stmt_fetch(s->stmt)) {
    case 0:
        return SQL_ROW;
    case MYSQL_DATA_TRUNCATED:
        if (!mysql_fix_truncated(db, s)) return SQL_ERROR;
        return SQL_ROW;
    case MYSQL_NO_DATA:
        return SQL_DONE;
    default:
        mysql_fail(db, s, "mysql_stmt_fetch");
        return SQL_ERROR;
    }
}

static void mysql_drv_finalize(sql_stmt *stmt) {
    Mysql_Stmt *s = stmt->stmt;
    if (!s) return;
    mysql_stmt_free(s);
    stmt->stmt = NULL;
}

static const char *mysql_drv_column_text(sql_stmt *stmt, int column) {
    Mysql_Stmt *s = stmt->stmt;
    if (column < 0 || column >= s->ncols || s->rnull[column]) return NULL;
    if (s->rint[column]) return temp_sprintf("%lld", s->rnum[column]);
    unsigned long len = s->rlen[column];
    if (len > s->rbind[column].buffer_length) {
        len = s->rbind[column].buffer_length;  // safety net
    }
    s->rtext[column][len] = '\0';
    return s->rtext[column];
}

static long long mysql_drv_column_int64(sql_stmt *stmt, int column) {
    Mysql_Stmt *s = stmt->stmt;
    if (column < 0 || column >= s->ncols || s->rnull[column]) return 0;
    if (s->rint[column]) return s->rnum[column];
    const char *text = mysql_drv_column_text(stmt, column);
    return text ? strtoll(text, NULL, 10) : 0;
}

static int mysql_drv_column_count(sql_stmt *stmt) {
    return ((Mysql_Stmt *) stmt->stmt)->ncols;
}

static bool mysql_drv_bind(sql_stmt *stmt, int index, sql_val value) {
    Mysql_Stmt *s = stmt->stmt;
    db_t *db = stmt->db;
    unsigned long i = (unsigned long) (index - 1);
    if (!s || i >= s->nparams) {
        snprintf(db->err, sizeof(db->err), "bind index %d out of range (%lu params)",
                 index, s ? s->nparams : 0UL);
        return false;
    }

    MYSQL_BIND *b = &s->pbind[i];
    free(s->ptext[i]);
    s->ptext[i] = NULL;

    switch (value.kind) {
    case SQL_VAL_NIL:
        s->pnull[i] = 1;
        s->plen[i] = 0;
        b->buffer_type = MYSQL_TYPE_STRING;
        b->buffer = (void *) "";
        break;
    case SQL_VAL_SV:
        if (!value.sv.data) {
            // NULL data pointer = SQL NULL (sqlite3_bind_text semantics the
            // form handlers rely on: empty upload part -> NULL column).
            s->pnull[i] = 1;
            s->plen[i] = 0;
            b->buffer_type = MYSQL_TYPE_STRING;
            b->buffer = (void *) "";
            break;
        }
        s->ptext[i] = malloc(value.sv.count + 1);
        if (!s->ptext[i]) {
            snprintf(db->err, sizeof(db->err), "out of memory");
            return false;
        }
        memcpy(s->ptext[i], value.sv.data, value.sv.count);
        s->ptext[i][value.sv.count] = '\0';
        s->pnull[i] = 0;
        s->plen[i] = (unsigned long) value.sv.count;
        b->buffer_type = MYSQL_TYPE_STRING;
        b->buffer = s->ptext[i];
        b->buffer_length = (unsigned long) value.sv.count;
        break;
    case SQL_VAL_I:
        s->pnull[i] = 0;
        s->pnum[i] = value.i;
        b->buffer_type = MYSQL_TYPE_LONGLONG;
        b->buffer = &s->pnum[i];
        b->buffer_length = sizeof(long long);
        break;
    }
    b->length = &s->plen[i];
    b->is_null = &s->pnull[i];
    return true;
}

const Sql_Driver drv_mysql = {
    .open = mysql_drv_open,
    .close = mysql_drv_close,
    .prepare = mysql_drv_prepare,
    .step = mysql_drv_step,
    .finalize = mysql_drv_finalize,
    .column_text = mysql_drv_column_text,
    .column_int64 = mysql_drv_column_int64,
    .column_count = mysql_drv_column_count,
    .bind = mysql_drv_bind,
    .exec_script = mysql_drv_exec_script,
    .txn_begin = mysql_drv_txn_begin,
    .txn_commit = mysql_drv_txn_commit,
    .txn_rollback = mysql_drv_txn_rollback,
    .errmsg = mysql_drv_errmsg,
};
