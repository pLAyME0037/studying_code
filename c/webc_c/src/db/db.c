#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOB_STRIP_PREFIX
#include "module/nob.h"

#include "sqlite3.h"
#include "db.h"
#include "sql.h"

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

/* Migrations live as per-dialect SQL files under migrations/ (no inline
 * strings): every entry names a directory with sqlite3.sql / mysql.sql /
 * postgres.sql. All migrations are applied inside one transaction, so these
 * files must not contain PRAGMAs (SQLite rejects `journal_mode`/`synchronous`
 * changes inside a transaction) - those run in open_webc_db() before the
 * transaction starts.
 *
 * Existing databases store the exact file contents in their Migrations
 * history, so the sqlite3.sql files must keep matching what the old inline
 * migrations[] held byte for byte (pinned by test/golden/). */
typedef struct {
    const char *name;
    const char *paths[SQL_LANG_COUNT];
} Migration;

static const Migration migrations[] = {
    { "0001_notes", {
        [SQL_SQLITE]   = "migrations/0001_notes/sqlite3.sql",
        [SQL_MYSQL]    = "migrations/0001_notes/mysql.sql",
        [SQL_POSTGRES] = "migrations/0001_notes/postgres.sql",
    }},
    { "0002_users", {
        [SQL_SQLITE]   = "migrations/0002_users/sqlite3.sql",
        [SQL_MYSQL]    = "migrations/0002_users/mysql.sql",
        [SQL_POSTGRES] = "migrations/0002_users/postgres.sql",
    }},
};

/* Bootstrap table recording which migrations are already applied. It must
 * exist before the history can be read, so it cannot itself live in a
 * migration file - and its DDL differs per dialect (DATETIME vs TIMESTAMP). */
static const char *const migrations_table_sql[SQL_LANG_COUNT] = {
    [SQL_SQLITE]   = "CREATE TABLE IF NOT EXISTS Migrations (\n"
                     "    applied_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,\n"
                     "    query TEXT NOT NULL\n"
                     ");\n",
    [SQL_MYSQL]    = "CREATE TABLE IF NOT EXISTS Migrations (\n"
                     "    applied_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,\n"
                     "    query TEXT NOT NULL\n"
                     ");\n",
    [SQL_POSTGRES] = "CREATE TABLE IF NOT EXISTS Migrations (\n"
                     "    applied_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,\n"
                     "    query TEXT NOT NULL\n"
                     ");\n",
};

/* Migration file contents for the active dialect, read from disk once per
 * process - the same lifetime the inline strings used to have. */
static String_Builder migration_bufs[ARRAY_LEN(migrations)];
static sql_lang_t migration_lang = SQL_SQLITE;
static bool migration_files_loaded = false;

static bool migration_files_load(sql_lang_t lang) {
    if (lang >= SQL_LANG_COUNT) return false;
    if (migration_files_loaded && migration_lang == lang) return true;
    if (migration_files_loaded) {
        for (size_t i = 0; i < ARRAY_LEN(migration_bufs); ++i) {
            sb_free(migration_bufs[i]);
            migration_bufs[i] = (String_Builder) {0};
        }
        migration_files_loaded = false;
    }
    for (size_t i = 0; i < ARRAY_LEN(migrations); ++i) {
        const char *path = migrations[i].paths[lang];
        if (!path) {
            fprintf(stderr, "ERROR: migration %s has no file for SQL dialect %d\n",
                    migrations[i].name, (int) lang);
            return false;
        }
        if (!read_entire_file(path, &migration_bufs[i])) return false;
        sb_append_null(&migration_bufs[i]);  // callers treat items as a C string
    }
    migration_lang = lang;
    migration_files_loaded = true;
    return true;
}

static const char *migration_sql(size_t index) {
    return migration_bufs[index].items;
}

// Result of checking/applying the migrations history.
typedef enum {
    SCHEMA_OK = 0,            // history matches and all pending migrations are applied
    SCHEMA_ERROR,             // SQL/internal error (nothing was committed)
    SCHEMA_HISTORY_MISMATCH,  // an already-applied migration differs from the
                              // migration files (the history was rewritten)
                              // -> caller may rebuild
} Schema_Status;

static Schema_Status create_schema_status(db_t *db, const char *webc_path);

static Schema_Status create_schema_status(db_t *db, const char *webc_path) {
    Schema_Status result = SCHEMA_OK;
    sql_stmt stmt = {0};
    bool began = false;

    if (!migration_files_load(db->lang)) return_defer(SCHEMA_ERROR);
    if (!sql_txn_begin(db)) return_defer(SCHEMA_ERROR);
    began = true;

    if (!sql_exec_script(db, migrations_table_sql[db->lang])) {
        return_defer(SCHEMA_ERROR);
    }

    if (!sql_prepare(db, "SELECT query FROM Migrations;", &stmt)) {
        return_defer(SCHEMA_ERROR);
    }

    size_t index = 0;
    Sql_Step rc = sql_step(&stmt);
    for (; rc == SQL_ROW; ++index) {
        if (index >= ARRAY_LEN(migrations)) {
            fprintf(stderr, "ERROR: %s: Database scheme is too new. Contains "
                    "more migrations applied than expected. Update your "
                    "application.\n", webc_path);
            return_defer(SCHEMA_ERROR);
        }
        const char *query = sql_column_text(&stmt, 0);
        const char *expected = migration_sql(index);
        if (!query || strcmp(query, expected) != 0) {
            fprintf(stderr, "ERROR: %s: Invalid database scheme. Mismatch in "
                    "migration %s:\n", webc_path, migrations[index].name);
            fprintf(stderr, "EXPECTED: %s\n", expected);
            fprintf(stderr, "FOUND: %s\n", query ? query : "(null)");
            return_defer(SCHEMA_HISTORY_MISMATCH);
        }
        rc = sql_step(&stmt);
    }
    if (rc != SQL_DONE) {
        nob_log(NOB_ERROR, "reading migration history: %s", db_errmsg(db));
        return_defer(SCHEMA_ERROR);
    }
    sql_finalize(&stmt);

    for (; index < ARRAY_LEN(migrations); ++index) {
        printf("INFO: %s: applying migration %s\n", webc_path,
               migrations[index].name);
        const char *mig = migration_sql(index);
        if (WEBC_TRACE_MIGRATION_QUERIES) printf("%s\n", mig);
        if (!sql_exec_script(db, mig)) return_defer(SCHEMA_ERROR);

        if (!sql_prepare(db, "INSERT INTO Migrations (query) VALUES (?)",
                         &stmt)) {
            return_defer(SCHEMA_ERROR);
        }
        bool ok = sql_bind(&stmt, 1, SQL_SV(sv_from_cstr(mig)))
                  && sql_final_step(&stmt);
        sql_finalize(&stmt);
        if (!ok) return_defer(SCHEMA_ERROR);
    }

defer:
    sql_finalize(&stmt);
    if (result == SCHEMA_OK) {
        if (!sql_txn_commit(db)) result = SCHEMA_ERROR;
    } else if (began) {
        // Nothing is committed on error/mismatch; close the open transaction
        // explicitly so the connection is in a clean state. (ROLLBACK outside
        // of a transaction just reports an error, which we skip.)
        sql_txn_rollback(db);
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
// Foreign key checks are toggled off inside (the connection now comes up
// with them on) so rows can be inserted in any order.
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

    // Run the copy with foreign key checks off: a note may reference a user
    // that is restored later. Re-enabled in the defer below, which always
    // runs outside the transaction.
    if (sqlite3_exec(db, "PRAGMA foreign_keys=OFF;", NULL, NULL, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }

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
    sqlite3_exec(db, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL);
    sb_free(sql);
    return result;
}

sql_lang_t webc_db_lang(void) {
    if (strncmp(WEBC_DB_PATH, "mysql:", 6) == 0) return SQL_MYSQL;
    if (strncmp(WEBC_DB_PATH, "postgres:", 9) == 0) return SQL_POSTGRES;
    return SQL_SQLITE;
}

const char *webc_db_dsn(void) {
    sql_lang_t lang = webc_db_lang();
    if (lang == SQL_MYSQL) return WEBC_DB_PATH + 6;
    if (lang == SQL_POSTGRES) return WEBC_DB_PATH + 9;
    return WEBC_DB_PATH;
}

db_t *open_webc_db(void) {
    db_t *result = NULL;

    sql_lang_t lang = webc_db_lang();
    bool webc_dir_is_symlink = false;

    // The legacy file->directory dance only applies to sqlite: the DSN of
    // the other dialects is not a local path.
    if (lang == SQL_SQLITE) {
        int exists = file_exists(WEBC_DIR_PATH);
        if (exists < 0) return_defer(NULL);
        if (!exists) {
            if (!db_mkdir_recursive(WEBC_DIR_PATH)) return_defer(NULL);
        } else {
            File_Type type = get_file_type(WEBC_DIR_PATH);
            if (type < 0) return_defer(NULL);
            switch (type) {
            case FILE_DIRECTORY: break;
            case FILE_REGULAR: {
                nob_log(INFO, "%s is a file! Migrating it to a directory...", WEBC_DIR_PATH);
                const char *webc_tmp_db_path = temp_sprintf("%s.tmp", WEBC_DIR_PATH);
                if (!nob_rename(WEBC_DIR_PATH, webc_tmp_db_path)) return_defer(NULL);
                if (!db_mkdir_recursive(WEBC_DIR_PATH)) return_defer(NULL);
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
    }

    bool rebuilt = false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        // db_open creates parent directories and applies the connection
        // PRAGMAs (WAL, synchronous, foreign_keys) - everything SQLite
        // refuses to run inside a transaction, so it all happens before
        // create_schema_status() opens one.
        result = db_open(lang, webc_db_dsn());
        if (!result) {
            if (lang == SQL_SQLITE && webc_dir_is_symlink) {
                fprintf(stderr, "NOTE: Your %s is a symlink! We used to expect this "
                        "path to lead to an sqlite3 database file, but at some point "
                        "we changed it to a directory. And now the database file is "
                        "expected to be at %s. If you are using some clever symlink "
                        "setup, please update it accordingly so we could open %s as "
                        "the sqlite3 database.\n", WEBC_DIR_PATH, WEBC_DB_PATH, WEBC_DB_PATH);
            }
            return_defer(NULL);
        }

        Schema_Status status = create_schema_status(result, WEBC_DB_PATH);
        if (status == SCHEMA_OK) break;

        if (status == SCHEMA_HISTORY_MISMATCH && attempt == 0
            && lang == SQL_SQLITE) {
            // migrations[] history was rewritten: move the old database aside,
            // rebuild from scratch and copy the data over afterwards.
            fprintf(stderr, "NOTE: migrations[] history changed. Backing up "
                    "%s to %s and rebuilding the schema...\n",
                    WEBC_DB_PATH, db_backup_path());
            db_close(result);
            result = NULL;
            if (!db_backup_and_reset()) return_defer(NULL);
            rebuilt = true;
            continue;
        }

        if (status == SCHEMA_HISTORY_MISMATCH) {
            fprintf(stderr, "ERROR: %s: migration history does not match the "
                    "migration files; the automatic rebuild exists only for "
                    "the sqlite dialect - fix or recreate the database.\n",
                    WEBC_DB_PATH);
        }
        db_close(result);
        result = NULL;
        return_defer(NULL);
    }

    if (rebuilt && !db_restore_from_backup((sqlite3 *) result->conn, db_backup_path())) {
        fprintf(stderr, "ERROR: could not copy the data back from %s. Your "
                "previous data is still safe there; the application continues "
                "with an empty schema.\n", db_backup_path());
    }

    return result;

defer:
    // Error paths close result themselves (or never opened it); this close
    // is a safety net so no connection can leak.
    db_close(result);
    return NULL;
}

// Connection pool implementation
static db_t *db_pool[DB_POOL_SIZE] = {0};
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
                db_close(db_pool[j]);
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
            db_close(db_pool[i]);
            db_pool[i] = NULL;
        }
    }
    db_pool_initialized = false;
}

db_t *db_pool_get(void) {
    if (!db_pool_initialized) {
        db_pool_init();
    }
    if (!db_pool_initialized) return NULL;
    
    // Simple round-robin
    db_t *db = db_pool[db_pool_index];
    db_pool_index = (db_pool_index + 1) % DB_POOL_SIZE;
    return db;
}

void db_pool_put(db_t *db) {
    // In this simple implementation, we don't need to do anything
    // The connection stays in the pool
    (void)db;
}
