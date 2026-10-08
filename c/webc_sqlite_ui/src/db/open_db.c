#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define NOB_STRIP_PREFIX
#include "module/nob.h"

#include "open_db.h"
#include "db.h"

const char *WEBC_ACTIVE_DB_PATH = NULL;
bool         WEBC_ACTIVE_READONLY = false;

// Owned copy of the active path (the app DB constant is never freed).
static char *active_owned = NULL;

const char *user_db_dsn(const char *path, bool readonly) {
    String_Builder b = {0};
    sb_append_cstr(&b, "file:");
    for (const char *p = path; *p; ++p) {
        // Characters that would break the URI query/path split.
        if (*p == ' ' || *p == '%' || *p == '?' || *p == '#') {
            sb_appendf(&b, "%%%02X", (unsigned char) *p);
        } else {
            sb_append_buf(&b, p, 1);
        }
    }
    if (readonly) sb_append_cstr(&b, "?mode=ro");
    sb_append_null(&b);
    const char *out = temp_sprintf("%s", b.items);
    sb_free(b);
    return out;
}

int user_db_open_flags(const char *dsn) {
    // URI access modes REPLACE the open flags (sqlite3_open_v2 rejects a
    // mode whose bit is not already present), so the flag must mirror the
    // mode exactly.
    int flags = SQLITE_OPEN_URI;
    flags |= strstr(dsn, "?mode=ro")
                 ? SQLITE_OPEN_READONLY
                 : SQLITE_OPEN_READWRITE;
    return flags;
}

bool db_path_sane(const char *path) {
    if (!path || path[0] != '/') return false;          // absolute only
    if (strstr(path, "//")) return false;               // no empty segments
    // Reject ".." segments anywhere in the path.
    const char *p = path;
    while ((p = strstr(p, "..")) != NULL) {
        bool seg_start = p == path || p[-1] == '/';
        bool seg_end = p[2] == '/' || p[2] == '\0';
        if (seg_start && seg_end) return false;
        p += 2;
    }
    return true;
}

bool db_path_is_sqlite(const char *path, char *err, size_t errsz) {
    struct stat st;
    if (stat(path, &st) != 0) {
        snprintf(err, errsz, "cannot stat %s", path);
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        snprintf(err, errsz, "%s is not a regular file", path);
        return false;
    }
    if (st.st_size == 0) return true;  // fresh database, header not written yet

    static const unsigned char hdr[16] = "SQLite format 3";  // incl. trailing NUL
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, errsz, "cannot read %s", path);
        return false;
    }
    unsigned char buf[16] = {0};
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (n != sizeof(buf) || memcmp(buf, hdr, sizeof(hdr)) != 0) {
        snprintf(err, errsz, "%s is not a SQLite database file", path);
        return false;
    }
    return true;
}

// Open the target once to surface a user-facing error (locked, permissions,
// corrupt) before committing to the switch.
static bool probe_open(const char *dsn, char *err, size_t errsz) {
    sqlite3 *conn = NULL;
    int rc = sqlite3_open_v2(dsn, &conn, user_db_open_flags(dsn), NULL);
    if (rc != SQLITE_OK) {
        snprintf(err, errsz, "%s",
                 conn ? sqlite3_errmsg(conn) : "cannot open database file");
        if (conn) sqlite3_close(conn);
        return false;
    }
    sqlite3_close(conn);
    return true;
}

static bool probe_create(const char *path, char *err, size_t errsz) {
    sqlite3 *conn = NULL;
    int rc = sqlite3_open_v2(path, &conn,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                             NULL);
    if (rc != SQLITE_OK) {
        snprintf(err, errsz, "%s",
                 conn ? sqlite3_errmsg(conn) : "cannot create database file");
        if (conn) sqlite3_close(conn);
        return false;
    }
    sqlite3_close(conn);
    return true;
}

db_t *open_active_db(void) {
    if (!WEBC_ACTIVE_DB_PATH) return open_webc_db();
    return db_open(SQL_SQLITE,
                   user_db_dsn(WEBC_ACTIVE_DB_PATH, WEBC_ACTIVE_READONLY));
}

bool db_switch(const char *path, bool readonly, bool create,
               char *err, size_t errsz)
{
    if (!db_path_sane(path)) {
        snprintf(err, errsz, "path must be absolute and contain no `..`: %s",
                 path ? path : "(empty)");
        return false;
    }

    if (create) {
        if (readonly) {
            snprintf(err, errsz, "a new database cannot be read-only");
            return false;
        }
        if (file_exists(path) > 0) {
            snprintf(err, errsz, "%s already exists", path);
            return false;
        }
        const char *slash = strrchr(path, '/');
        if (slash && slash != path) {
            const char *parent = temp_sprintf("%.*s",
                                              (int) (slash - path), path);
            if (!db_mkdir_recursive(parent)) {
                snprintf(err, errsz, "cannot create directory %s", parent);
                return false;
            }
        }
        if (!probe_create(path, err, errsz)) return false;
    } else {
        if (!db_path_is_sqlite(path, err, errsz)) return false;
        if (!probe_open(user_db_dsn(path, readonly), err, errsz)) return false;
    }

    // Switch: drop pooled connections to the previous target, then publish
    // the new one. Handlers open per request, so an in-flight request keeps
    // working on the connection it already has.
    db_pool_cleanup();
    free(active_owned);
    active_owned = strdup(path);
    WEBC_ACTIVE_DB_PATH = active_owned;
    WEBC_ACTIVE_READONLY = readonly && !create;
    db_recents_add(path);
    return true;
}

void db_close_active(void) {
    db_pool_cleanup();
    free(active_owned);
    active_owned = NULL;
    WEBC_ACTIVE_DB_PATH = NULL;
    WEBC_ACTIVE_READONLY = false;
}

void db_quote_ident(String_Builder *sb, const char *s) {
    sb_append_cstr(sb, "\"");
    for (; *s; ++s) {
        if (*s == '"') sb_append_cstr(sb, "\"\"");
        else sb_append_buf(sb, s, 1);
    }
    sb_append_cstr(sb, "\"");
}

/* -- recents ------------------------------------------------------------ */

static const char *recents_dir(void) {
    return temp_sprintf("%s/"WEBC_DATA_PATH, getenv("HOME"));
}

static const char *recents_path(void) {
    return temp_sprintf("%s/recents", recents_dir());
}

void db_recents_load(Db_Recents *out) {
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(recents_path(), "r");
    if (!f) return;
    char line[DB_PATH_MAX];
    while (out->count < DB_RECENTS_MAX && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!db_path_sane(line)) continue;
        bool dup = false;
        for (size_t i = 0; i < out->count; ++i) {
            if (strcmp(out->items[i], line) == 0) dup = true;
        }
        if (dup) continue;
        snprintf(out->items[out->count], DB_PATH_MAX, "%s", line);
        out->count += 1;
    }
    fclose(f);
}

void db_recents_add(const char *path) {
    Db_Recents old = {0};
    db_recents_load(&old);

    char fresh[DB_RECENTS_MAX][DB_PATH_MAX];
    size_t n = 0;
    snprintf(fresh[n++], DB_PATH_MAX, "%s", path);
    for (size_t i = 0; i < old.count && n < DB_RECENTS_MAX; ++i) {
        if (strcmp(old.items[i], path) == 0) continue;
        memcpy(fresh[n], old.items[i], DB_PATH_MAX);
        n += 1;
    }

    if (!db_mkdir_recursive(recents_dir())) return;
    FILE *f = fopen(recents_path(), "w");
    if (!f) return;
    for (size_t i = 0; i < n; ++i) fprintf(f, "%s\n", fresh[i]);
    fclose(f);
}
