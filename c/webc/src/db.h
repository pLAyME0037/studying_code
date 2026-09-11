#ifndef SRC_DB_H_
#define SRC_DB_H_

#include <stdbool.h>
#include <stdio.h>
#include "sqlite3.h"

#define LOG_SQLITE3_ERROR(db) fprintf(stderr, "%s:%d: SQLITE3 ERROR: %s\n", __FILE__, __LINE__, sqlite3_errmsg(db))

extern const char *WEBC_DIR_PATH;
extern const char *WEBC_DB_PATH;
extern bool WEBC_TRACE_MIGRATION_QUERIES;

bool txn_begin(sqlite3 *db);
bool txn_commit(sqlite3 *db);
bool create_schema(sqlite3 *db, const char *webc_path);
sqlite3 *open_webc_db(void);

#endif // SRC_DB_H_
