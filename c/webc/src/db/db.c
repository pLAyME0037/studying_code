#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOB_STRIP_PREFIX
#include "../../nob.h"

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

const char *migrations[] = {
    "CREATE TABLE IF NOT EXISTS Notes (\n"
    "    id INTEGER PRIMARY KEY ASC,\n"
    "    title TEXT,\n"
    "    body TEXT,\n"
    "    created_at DATETIME DEFAULT CURRENT_TIMESTAMP\n"
    ");\n",
    "CREATE TABLE IF NOT EXISTS Users (\n"
    "    id INTEGER PRIMARY KEY ASC,\n"
    "    name TEXT,\n"
    "    username TEXT,\n"
    "    email TEXT,\n"
    "    profile_pic BLOB\n"
    ");\n",

    // "DROP TABLE IF EXISTS Notes;\n"
    // "DROP TABLE IF EXISTS Users;\n"
};

// TODO: can we just extract webc_path from db somehow?
bool create_schema(sqlite3 *db, const char *webc_path) {
    bool result = true;
    sqlite3_stmt *stmt = NULL;
    if (!txn_begin(db)) return_defer(false);

    if (sqlite3_exec(db,
            "CREATE TABLE IF NOT EXISTS Migrations (\n"
            "    applied_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,\n"
            "    query TEXT NOT NULL\n"
            ");\n",
            NULL, NULL, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }

    if (sqlite3_prepare_v2(db, "SELECT query FROM Migrations;", -1, &stmt, NULL)!= SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }

    size_t index = 0;
    int ret = sqlite3_step(stmt);
    for (; ret == SQLITE_ROW; ++index) {
        if (index >= ARRAY_LEN(migrations)) {
            fprintf(stderr, "ERROR: %s: Database scheme is too new. Contains "
                    "more migrations applied than expected. Update your "
                    "application.\n", webc_path);
            return_defer(false);
        }
        const char *query = (const char *)sqlite3_column_text(stmt, 0);
        if (strcmp(query, migrations[index]) != 0) {
            fprintf(stderr, "ERROR: %s: Invalid database scheme. Mismatch in "
                    "migration %zu:\n", webc_path, index);
            fprintf(stderr, "EXPECTED: %s\n", migrations[index]);
            fprintf(stderr, "FOUND: %s\n", query);
            return_defer(false);
        }
        ret = sqlite3_step(stmt);
    }

    if (ret != SQLITE_DONE) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    sqlite3_finalize(stmt);
    stmt = NULL;

    for (; index < ARRAY_LEN(migrations); ++index) {
        printf("INFO: %s: applying migration %zu\n", webc_path, index);
        if (WEBC_TRACE_MIGRATION_QUERIES) printf("%s\n", migrations[index]);
        if (sqlite3_exec(db, migrations[index], NULL, NULL, NULL) != SQLITE_OK) {
            LOG_SQLITE3_ERROR(db);
            return_defer(false);
        }

        int ret = sqlite3_prepare_v2(db, "INSERT INTO Migrations (query) VALUES (?)", -1, &stmt, NULL);
        if (ret != SQLITE_OK) {
            LOG_SQLITE3_ERROR(db);
            return_defer(false);
        }

        if (sqlite3_bind_text(stmt, 1, migrations[index], strlen(migrations[index]), NULL) != SQLITE_OK) {
            LOG_SQLITE3_ERROR(db);
            return_defer(false);
        }

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            LOG_SQLITE3_ERROR(db);
            return_defer(false);
        }

        sqlite3_finalize(stmt);
        stmt = NULL;
    }

defer:
    if (stmt) sqlite3_finalize(stmt);
    if (result) result = txn_commit(db);
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
        return_defer(NULL);
    }

    if (!create_schema(result, WEBC_DB_PATH)) {
        sqlite3_close(result);
        return_defer(NULL);
    }

    if (sqlite3_exec(result, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(result);
        sqlite3_close(result);
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
