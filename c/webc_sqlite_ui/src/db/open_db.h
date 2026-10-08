#ifndef SRC_DB_OPEN_DB_H_
#define SRC_DB_OPEN_DB_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sqlite3.h"
#include "sql.h"

/* Runtime-selected database target (the "open any .db file" feature).
 *
 * WEBC_ACTIVE_DB_PATH == NULL means the application database (WEBC_DB_PATH,
 * migrations + journal_mode setup run as before). A non-NULL path is a
 * user-selected SQLite file: it opens WITHOUT migrations and WITHOUT the
 * app DB's journal_mode/synchronous PRAGMAs, so merely opening a file never
 * modifies it.
 *
 * User databases are passed to the driver as sqlite URIs:
 *     read-write:  file:<absolute path>
 *     read-only:   file:<absolute path>?mode=ro
 * The "file:" prefix is what tells driver_sqlite.c "user file, skip the app
 * PRAGMAs"; mode=ro is what makes it read-only (the driver mirrors the mode
 * into the sqlite3_open_v2 flags - URI access modes replace the flags, they
 * do not merge).
 */
extern const char *WEBC_ACTIVE_DB_PATH;  // absolute path, or NULL = app DB
extern bool         WEBC_ACTIVE_READONLY;

// DSN for a user-selected file (URI-encoded, mode=ro when readonly).
const char *user_db_dsn(const char *path, bool readonly);
// sqlite3_open_v2 flags matching a DSN produced by user_db_dsn().
int user_db_open_flags(const char *dsn);

// Open whatever the active target is: app DB (with migrations) when none is
// selected, otherwise the user file. NULL on failure.
db_t *open_active_db(void);

// Validate + switch the active target. `create` makes a new empty file
// (read-write only). On failure returns false and fills err.
bool db_switch(const char *path, bool readonly, bool create,
               char *err, size_t errsz);
// Back to the application database.
void db_close_active(void);

// True when the path is safe to hand to sqlite: absolute, no ".." segment.
bool db_path_sane(const char *path);
// True when path is a regular file with the SQLite header (0-byte files
// count: SQLite treats them as a fresh database).
bool db_path_is_sqlite(const char *path, char *err, size_t errsz);

// "double-quote an identifier" helper for user-influenced SQL fragments.
void db_quote_ident(String_Builder *sb, const char *s);

/* Recently opened files, one absolute path per line in
 * <WEBC_DIR_PATH>/recents, most recent first. */
#define DB_RECENTS_MAX 10
#define DB_PATH_MAX    4096
typedef struct {
    char   items[DB_RECENTS_MAX][DB_PATH_MAX];
    size_t count;
} Db_Recents;

void db_recents_load(Db_Recents *out);
void db_recents_add(const char *path);   // prepend + dedup + cap

#endif  // SRC_DB_OPEN_DB_H_
