#ifndef SRC_DB_H_
#define SRC_DB_H_

#include <stdbool.h>
#include <stdio.h>
#include "sqlite3.h"
#include "sql.h"

#define LOG_SQLITE3_ERROR(db) fprintf(stderr, "%s:%d: SQLITE3 ERROR: %s\n", __FILE__, __LINE__, sqlite3_errmsg(db))

extern const char *WEBC_DIR_PATH;
extern const char *WEBC_DB_PATH;
extern bool WEBC_TRACE_MIGRATION_QUERIES;

bool txn_begin(sqlite3 *db);
bool txn_commit(sqlite3 *db);
bool txn_rollback(sqlite3 *db);
// Dialect + connection string resolved from WEBC_DB_PATH: the "mysql:" and
// "postgres:" prefixes select the dialect (the rest is the driver's DSN);
// anything else is a sqlite file path.
sql_lang_t webc_db_lang(void);
const char *webc_db_dsn(void);  // WEBC_DB_PATH without the dialect prefix

db_t *open_webc_db(void);

// Connection pool
#define DB_POOL_SIZE 8
db_t *db_pool_get(void);
void db_pool_put(db_t *db);
void db_pool_init(void);
void db_pool_cleanup(void);

#endif // SRC_DB_H_
