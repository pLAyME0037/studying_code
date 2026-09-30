#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOB_STRIP_PREFIX
#include "module/nob.h"

#include "sqlite3.h"
#include "db.h"

const char *WEBC_DIR_PATH = NULL;
const char *WEBC_DB_PATH = NULL;
bool WEBC_TRACE_MIGRATION_QUERIES = false;

bool txn_begin(sqlite3 *db) {
    if (sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return false;
    }
    return true;
}

bool txn_commit(sqlite3 *db) {
    if (sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return false;
    }
    return true;
}

bool txn_rollback(sqlite3 *db) {
    if (sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return false;
    }
    return true;
}

/* NOTE: PRAGMA statements must NOT live in this array. create_schema() runs
 * every migration inside a single transaction and SQLite rejects
 * `journal_mode`/`synchronous` changes from within a transaction.
 * They are executed in open_webc_db() before the transaction starts instead.
*/
const char *migrations[] = {
    "CREATE TABLE IF NOT EXISTS notes (\n"
    "    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),\n"
    "    user_id TEXT NULL REFERENCES users(id),\n"
    "    title TEXT NOT NULL,\n"
    "    body TEXT,\n"
    "    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,\n"
    "    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP\n"
    ");\n",
    "CREATE TABLE IF NOT EXISTS users (\n"
    "    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),\n"
    "    name TEXT NOT NULL,\n"
    "    username TEXT NOT NULL,\n"
    "    email TEXT NOT NULL,\n"
    "    profile_pic BLOB NULL,\n"
    "    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,\n"
    "    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP\n"
    ");\n",

    // "DROP TABLE IF EXISTS Notes;\n"
    // "DROP TABLE IF EXISTS Users;\n"
};

// Result of checking/applying the migrations[] history.
typedef enum {
    SCHEMA_OK = 0,            // history matches and all pending migrations are applied
    SCHEMA_ERROR,             // SQL/internal error (nothing was committed)
    SCHEMA_HISTORY_MISMATCH,  // an already-applied migration differs from migrations[]
                              // (the history was rewritten) -> caller may rebuild
} Schema_Status;

static Schema_Status create_schema_status(sqlite3 *db, const char *webc_path);

// TODO: can we just extract webc_path from db somehow?
bool create_schema(sqlite3 *db, const char *webc_path) {
    return create_schema_status(db, webc_path) == SCHEMA_OK;
}

static Schema_Status create_schema_status(sqlite3 *db, const char *webc_path) {
    Schema_Status result = SCHEMA_OK;
    sqlite3_stmt *stmt = NULL;
    if (!txn_begin(db)) return_defer(SCHEMA_ERROR);

    if (sqlite3_exec(db,
            "CREATE TABLE IF NOT EXISTS Migrations (\n"
            "    applied_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,\n"
            "    query TEXT NOT NULL\n"
            ");\n",
            NULL, NULL, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(SCHEMA_ERROR);
    }

    if (sqlite3_prepare_v2(db, "SELECT query FROM Migrations;", -1, &stmt, NULL)!= SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(SCHEMA_ERROR);
    }

    size_t index = 0;
    int ret = sqlite3_step(stmt);
    for (; ret == SQLITE_ROW; ++index) {
        if (index >= ARRAY_LEN(migrations)) {
            fprintf(stderr, "ERROR: %s: Database scheme is too new. Contains "
                    "more migrations applied than expected. Update your "
                    "application.\n", webc_path);
            return_defer(SCHEMA_ERROR);
        }
        const char *query = (const char *)sqlite3_column_text(stmt, 0);
        if (strcmp(query, migrations[index]) != 0) {
            fprintf(stderr, "ERROR: %s: Invalid database scheme. Mismatch in "
                    "migration %zu:\n", webc_path, index);
            fprintf(stderr, "EXPECTED: %s\n", migrations[index]);
            fprintf(stderr, "FOUND: %s\n", query);
            return_defer(SCHEMA_HISTORY_MISMATCH);
        }
        ret = sqlite3_step(stmt);
    }

    if (ret != SQLITE_DONE) {
        LOG_SQLITE3_ERROR(db);
        return_defer(SCHEMA_ERROR);
    }
    sqlite3_finalize(stmt);
    stmt = NULL;

    for (; index < ARRAY_LEN(migrations); ++index) {
        printf("INFO: %s: applying migration %zu\n", webc_path, index);
        if (WEBC_TRACE_MIGRATION_QUERIES) printf("%s\n", migrations[index]);
        if (sqlite3_exec(db, migrations[index], NULL, NULL, NULL) != SQLITE_OK) {
            LOG_SQLITE3_ERROR(db);
            return_defer(SCHEMA_ERROR);
        }

        int ret = sqlite3_prepare_v2(db, "INSERT INTO Migrations (query) VALUES (?)", -1, &stmt, NULL);
        if (ret != SQLITE_OK) {
            LOG_SQLITE3_ERROR(db);
            return_defer(SCHEMA_ERROR);
        }

        if (sqlite3_bind_text(stmt, 1, migrations[index], strlen(migrations[index]), NULL) != SQLITE_OK) {
            LOG_SQLITE3_ERROR(db);
            return_defer(SCHEMA_ERROR);
        }

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            LOG_SQLITE3_ERROR(db);
            return_defer(SCHEMA_ERROR);
        }

        sqlite3_finalize(stmt);
        stmt = NULL;
    }

defer:
    if (stmt) sqlite3_finalize(stmt);
    if (result == SCHEMA_OK) {
        if (!txn_commit(db)) result = SCHEMA_ERROR;
    } else {
        // Nothing is committed on error/mismatch; close the open transaction
        // explicitly so the connection is in a clean state. (ROLLBACK outside
        // of a transaction just reports an error, which we ignore.)
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    }
    return result;
}

/* ---------------------------------------------------------------------------
 * Backup/restore support: when migrations[] history was rewritten we rebuild
 * the database from scratch, but keep the user data:
 *   1. db            -> db.bak     (old file, untouched)
 *   2. fresh db is created and migrated from migrations[]
 *   3. every table/column both databases share is copied from db.bak into
 *      the new schema (db_restore_from_backup)
 * ---------------------------------------------------------------------------
 */
static const char *db_backup_path(void) {
    return temp_sprintf("%s.bak", WEBC_DB_PATH);
}

// Moves the current database file out of the way so a fresh one can be
// created. The connection MUST be closed before calling this (so SQLite has
// checkpointed and removed the -wal/-shm files).
static bool db_backup_and_reset(void) {
    const char *bak     = db_backup_path();
    const char *wal     = temp_sprintf("%s-wal", WEBC_DB_PATH);
    const char *shm     = temp_sprintf("%s-shm", WEBC_DB_PATH);
    const char *bak_wal = temp_sprintf("%s.bak-wal", WEBC_DB_PATH);
    const char *bak_shm = temp_sprintf("%s.bak-shm", WEBC_DB_PATH);

    if (file_exists(bak) > 0 && !delete_file(bak)) return false;
    if (file_exists(bak_wal) > 0 && !delete_file(bak_wal)) return false;
    if (file_exists(bak_shm) > 0 && !delete_file(bak_shm)) return false;

    if (!nob_rename(WEBC_DB_PATH, bak)) return false;

    // Leftovers of the renamed file must not be picked up by the new database
    if (file_exists(wal) > 0 && !delete_file(wal)) return false;
    if (file_exists(shm) > 0 && !delete_file(shm)) return false;
    return true;
}

static bool str_ieq(const char *a, const char *b) {
    size_t n = strlen(a);
    return n == strlen(b) && sqlite3_strnicmp(a, b, (int)n) == 0;
}

static void sb_append_quoted_ident(String_Builder *sb, const char *s) {
    sb_append_cstr(sb, "\"");
    for (; *s; ++s) {
        if (*s == '"') sb_append_cstr(sb, "\"\"");
        else sb_append_buf(sb, s, 1);
    }
    sb_append_cstr(sb, "\"");
}

#define MAX_TABLE_COLS 64
typedef struct {
    char name[128];
} Col_Name;
typedef struct {
    Col_Name items[MAX_TABLE_COLS];
    size_t count;
} Column_List;

// PRAGMA <schema>.table_info(<table>) -> column names of that table.
// A table that does not exist simply yields count == 0.
static bool get_table_columns(sqlite3     *db,
                              const char  *schema,
                              const char  *table,
                              Column_List *out)
{
    bool result = true;
    sqlite3_stmt *stmt = NULL;
    String_Builder sql = {0};

    out->count = 0;
    sb_append_cstr(&sql, "PRAGMA ");
    sb_append_cstr(&sql, schema);
    sb_append_cstr(&sql, ".table_info(");
    sb_append_quoted_ident(&sql, table);
    sb_append_cstr(&sql, ");");
    sb_append_null(&sql);

    if (sqlite3_prepare_v2(db, sql.items, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        if (out->count >= MAX_TABLE_COLS) {
            fprintf(stderr, "ERROR: table `%s` has more than %d columns, "
                    "cannot restore it automatically\n", table, MAX_TABLE_COLS);
            return_defer(false);
        }
        snprintf(out->items[out->count].name, sizeof(out->items[0].name), "%s", name);
        out->count += 1;
    }

defer:
    if (stmt) sqlite3_finalize(stmt);
    sb_free(sql);
    return result;
}

// Copies all tables that exist in both databases (matching columns by name,
// case-insensitively) from the backup into the freshly migrated database.
// Must run BEFORE `PRAGMA foreign_keys=ON` is executed so rows can be
// inserted in any order.
static bool db_restore_from_backup(sqlite3 *db, const char *bak_path) {
    bool result = true;
    bool attached = false;
    sqlite3_stmt *tables_stmt = NULL;
    String_Builder sql = {0};
    char tables[64][128] = {0};
    size_t tables_count = 0;

    sb_append_cstr(&sql, "ATTACH DATABASE '");
    for (const char *p = bak_path; *p; ++p) {
        if (*p == '\'') sb_append_cstr(&sql, "''");
        else sb_append_buf(&sql, p, 1);
    }
    sb_append_cstr(&sql, "' AS bak");
    sb_append_null(&sql);

    if (sqlite3_exec(db, sql.items, NULL, NULL, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    attached = true;

    if (!txn_begin(db)) return_defer(false);

    // Materialize the table list first so we do not hold a statement open
    // while inserting into the main database.
    if (sqlite3_prepare_v2(db,
            "SELECT name FROM bak.sqlite_master WHERE type='table' "
            "AND name NOT LIKE 'sqlite_%' AND name <> 'Migrations' ORDER BY name;",
            -1, &tables_stmt, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }

    int step = SQLITE_ROW;
    while ((step = sqlite3_step(tables_stmt)) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(tables_stmt, 0);
        if (tables_count >= ARRAY_LEN(tables)) {
            fprintf(stderr, "ERROR: backup contains more than %zu tables, "
                    "cannot restore it automatically\n", ARRAY_LEN(tables));
            return_defer(false);
        }
        snprintf(tables[tables_count], sizeof(tables[0]), "%s", name);
        tables_count += 1;
    }
    if (step != SQLITE_DONE) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    sqlite3_finalize(tables_stmt);
    tables_stmt = NULL;

    for (size_t i = 0; i < tables_count; ++i) {
        const char *old_name = tables[i];
        Column_List old_cols = {0}, new_cols = {0};

        if (!get_table_columns(db, "bak", old_name, &old_cols)) return_defer(false);
        if (!get_table_columns(db, "main", old_name, &new_cols)) return_defer(false);

        if (new_cols.count == 0) {
            printf("INFO: backup table `%s` has no counterpart in the new "
                    "schema, skipped\n", old_name);
            continue;
        }

        String_Builder cols = {0};
        String_Builder sel = {0};
        size_t shared = 0;
        for (size_t j = 0; j < new_cols.count; ++j) {
            const char *col = new_cols.items[j].name;
            bool exists = false;
            for (size_t k = 0; k < old_cols.count; ++k) {
                if (str_ieq(col, old_cols.items[k].name)) {
                    exists = true;
                    break;
                }
            }
            if (!exists) continue;
            if (shared > 0) {
                sb_append_cstr(&cols, ", ");
                sb_append_cstr(&sel, ", ");
            }
            sb_append_quoted_ident(&cols, col);
            sb_append_quoted_ident(&sel, col);
            shared += 1;
        }

        if (shared == 0) {
            printf("INFO: backup table `%s` shares no columns with the new "
                    "schema, skipped\n", old_name);
            sb_free(cols);
            sb_free(sel);
            continue;
        }

        sql.count = 0;
        sb_append_cstr(&sql, "INSERT INTO ");
        sb_append_quoted_ident(&sql, old_name);
        sb_append_cstr(&sql, " (");
        sb_append_buf(&sql, cols.items, cols.count);
        sb_append_cstr(&sql, ") SELECT ");
        sb_append_buf(&sql, sel.items, sel.count);
        sb_append_cstr(&sql, " FROM bak.");
        sb_append_quoted_ident(&sql, old_name);
        sb_append_cstr(&sql, ";");
        sb_append_null(&sql);
        sb_free(cols);
        sb_free(sel);

        if (sqlite3_exec(db, sql.items, NULL, NULL, NULL) != SQLITE_OK) {
            fprintf(stderr, "ERROR: restoring table `%s` failed: %s\n",
                    old_name, sqlite3_errmsg(db));
            return_defer(false);
        }
        printf("INFO: restored %d row(s) into `%s`\n", sqlite3_changes(db), old_name);
    }

    if (!txn_commit(db)) return_defer(false);

defer:
    if (tables_stmt) sqlite3_finalize(tables_stmt);
    if (!result) sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    if (attached) sqlite3_exec(db, "DETACH DATABASE bak;", NULL, NULL, NULL);
    sb_free(sql);
    return result;
}

sqlite3 *open_webc_db(void) {
    sqlite3 *result = NULL;

    int exists = file_exists(WEBC_DIR_PATH);
    if (exists < 0) return_defer(NULL);
    bool webc_dir_is_symlink = false;
    if (!exists) {
        if (!mkdir_if_not_exists(WEBC_DIR_PATH)) return_defer(NULL);
    } else {
        File_Type type = get_file_type(WEBC_DIR_PATH);
        if (type < 0) return_defer(NULL);
        switch (type) {
        case FILE_DIRECTORY: break;
        case FILE_REGULAR: {
            nob_log(INFO, "%s is a file! Migrating it to a directory...", WEBC_DIR_PATH);
            const char *webc_tmp_db_path = temp_sprintf("%s.tmp", WEBC_DIR_PATH);
            if (!nob_rename(WEBC_DIR_PATH, webc_tmp_db_path)) return_defer(NULL);
            if (!mkdir_if_not_exists(WEBC_DIR_PATH)) return_defer(NULL);
            if (!nob_rename(webc_tmp_db_path, WEBC_DB_PATH)) return_defer(NULL);
        } break;
        case FILE_SYMLINK: {
            webc_dir_is_symlink = true;
        } break;
        case FILE_OTHER: {
            fprintf(stderr, "ERROR: %s is a weird file! We expect it to be a "
                    "directory or a regular file in case of a legacy database...\n", WEBC_DIR_PATH);
            return_defer(NULL);
        } break;
        }
    }

    bool rebuilt = false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        int ret = sqlite3_open(WEBC_DB_PATH, &result);
        if (ret != SQLITE_OK) {
            fprintf(stderr, "ERROR: %s: %s\n", WEBC_DB_PATH, sqlite3_errstr(ret));
            if (webc_dir_is_symlink) {
                fprintf(stderr, "NOTE: Your %s is a symlink! We used to expect this "
                        "path to lead to an sqlite3 database file, but at some point "
                        "we changed it to a directory. And now the database file is "
                        "expected to be at %s. If you are using some clever symlink "
                        "setup, please update it accordingly so we could open %s as "
                        "the sqlite3 database.\n", WEBC_DIR_PATH, WEBC_DB_PATH, WEBC_DB_PATH);
            }
            if (result) sqlite3_close(result);
            result = NULL;
            return_defer(NULL);
        }

        // PRAGMAs that SQLite refuses to run inside a transaction have to be
        // executed before create_schema() opens one.
        if (sqlite3_exec(result, "PRAGMA journal_mode = WAL;", NULL, NULL, NULL) != SQLITE_OK) {
            LOG_SQLITE3_ERROR(result);
            sqlite3_close(result);
            result = NULL;
            return_defer(NULL);
        }
        if (sqlite3_exec(result, "PRAGMA synchronous = NORMAL;", NULL, NULL, NULL) != SQLITE_OK) {
            LOG_SQLITE3_ERROR(result);
            sqlite3_close(result);
            result = NULL;
            return_defer(NULL);
        }

        Schema_Status status = create_schema_status(result, WEBC_DB_PATH);
        if (status == SCHEMA_OK) break;

        if (status == SCHEMA_HISTORY_MISMATCH && attempt == 0) {
            // migrations[] history was rewritten: move the old database aside,
            // rebuild from scratch and copy the data over afterwards.
            fprintf(stderr, "NOTE: migrations[] history changed. Backing up "
                    "%s to %s and rebuilding the schema...\n",
                    WEBC_DB_PATH, db_backup_path());
            sqlite3_close(result);
            result = NULL;
            if (!db_backup_and_reset()) return_defer(NULL);
            rebuilt = true;
            continue;
        }

        sqlite3_close(result);
        result = NULL;
        return_defer(NULL);
    }

    if (rebuilt && !db_restore_from_backup(result, db_backup_path())) {
        fprintf(stderr, "ERROR: could not copy the data back from %s. Your "
                "previous data is still safe there; the application continues "
                "with an empty schema.\n", db_backup_path());
    }

    if (sqlite3_exec(result, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(result);
        sqlite3_close(result);
        result = NULL;
        return_defer(NULL);
    }

defer:
    return result;
}

// Connection pool implementation
static sqlite3 *db_pool[DB_POOL_SIZE] = {0};
static int db_pool_index = 0;
static bool db_pool_initialized = false;

void db_pool_init(void) {
    if (db_pool_initialized) return;
    for (int i = 0; i < DB_POOL_SIZE; ++i) {
        db_pool[i] = open_webc_db();
        if (!db_pool[i]) {
            fprintf(stderr, "ERROR: Failed to initialize DB pool connection %d\n", i);
            // Clean up already created connections
            for (int j = 0; j < i; ++j) {
                sqlite3_close(db_pool[j]);
                db_pool[j] = NULL;
            }
            return;
        }
    }
    db_pool_initialized = true;
    printf("DB pool initialized with %d connections\n", DB_POOL_SIZE);
}

void db_pool_cleanup(void) {
    if (!db_pool_initialized) return;
    for (int i = 0; i < DB_POOL_SIZE; ++i) {
        if (db_pool[i]) {
            sqlite3_close(db_pool[i]);
            db_pool[i] = NULL;
        }
    }
    db_pool_initialized = false;
}

sqlite3 *db_pool_get(void) {
    if (!db_pool_initialized) {
        db_pool_init();
    }
    if (!db_pool_initialized) return NULL;
    
    // Simple round-robin
    sqlite3 *db = db_pool[db_pool_index];
    db_pool_index = (db_pool_index + 1) % DB_POOL_SIZE;
    return db;
}

void db_pool_put(sqlite3 *db) {
    // In this simple implementation, we don't need to do anything
    // The connection stays in the pool
    (void)db;
}
