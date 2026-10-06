#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define NOB_STRIP_PREFIX
#include "module/nob.h"

#include "sql.h"

extern const Sql_Driver drv_sqlite;
extern const Sql_Driver drv_mysql;
extern const Sql_Driver drv_postgres;

db_t *db_open(sql_lang_t lang, const char *dsn) {
    if (!dsn || !dsn[0]) return NULL;

    db_t *db = calloc(1, sizeof(*db));
    if (!db) return NULL;
    db->lang = lang;

    switch (lang) {
    case SQL_SQLITE:
        db->drv = &drv_sqlite;
        break;
    case SQL_MYSQL:
        db->drv = &drv_mysql;
        break;
    case SQL_POSTGRES:
        db->drv = &drv_postgres;
        break;
    case SQL_LANG_COUNT:
        snprintf(db->err, sizeof(db->err), "SQL dialect %d is not built in",
                 (int) lang);
        break;
    }

    if (!db->drv || !db->drv->open(db, dsn)) {
        nob_log(NOB_ERROR, "db_open: %s: %s", dsn,
                db->drv ? db->drv->errmsg(db) : db->err);
        free(db);
        return NULL;
    }
    return db;
}

void db_close(db_t *db) {
    if (!db) return;
    if (db->drv && db->drv->close) db->drv->close(db);
    free(db);
}

const char *db_errmsg(db_t *db) {
    if (!db || !db->drv || !db->drv->errmsg) return "no database connection";
    return db->drv->errmsg(db);
}

const Sql_Driver *sql_driver_for(sql_lang_t lang) {
    switch (lang) {
    case SQL_SQLITE:
        return &drv_sqlite;
    case SQL_MYSQL:
        return &drv_mysql;
    case SQL_POSTGRES:
        return &drv_postgres;
    case SQL_LANG_COUNT:
        break;
    }
    return NULL;
}

bool db_mkdir_recursive(const char *path) {
    if (!path || !path[0]) return false;

    // Walk the path, creating each prefix (mkdir(2) only makes one level and
    // does not fail on an existing directory, which is exactly what we want).
    char *buf = temp_sprintf("%s", path);
    for (char *p = buf + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
            nob_log(NOB_ERROR, "could not create directory `%s`: %s", buf,
                    strerror(errno));
            return false;
        }
        *p = '/';
    }
    if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
        nob_log(NOB_ERROR, "could not create directory `%s`: %s", buf,
                strerror(errno));
        return false;
    }
    return true;
}
