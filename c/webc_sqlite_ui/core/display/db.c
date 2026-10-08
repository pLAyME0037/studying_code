#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#define NOB_STRIP_PREFIX
#include "module/nob.h"

#include "db.h"
#include "core/http/serve.h"
#include "core/http/utils.h"
#include "core/layout/header.h"
#include "core/layout/footer.h"
#include "src/db/db.h"
#include "src/db/open_db.h"

/* =========================================================================
 * /db - overview of the active database + open/new/close panel.
 *
 * Active target rules (see src/db/open_db.h):
 *   no user DB selected -> the application DB (migrations already applied)
 *   user DB selected    -> that file, opened without any PRAGMA writes
 * Non-sqlite application dialects (mysql DSN) render the panel without the
 * file/pragma sections.
 * ========================================================================= */

#define DB_PAGE_MAX_OBJECTS 500
#define BROWSE_MAX_ENTRIES  2000

typedef struct {
    char      name[160];
    char      type[8];   // "table" | "view"
    long long rows;      // -1 = count failed
} Db_Object;

/* -- query helpers ------------------------------------------------------ */

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// ?key=value out of `query` (percent-decoded). False when absent.
static bool query_param(String_View query, const char *key,
                        char *out, size_t outsz)
{
    String_View needle = sv_from_cstr(temp_sprintf("%s=", key));
    size_t i = 0;
    while (i <= query.count) {
        size_t j = i;
        while (j < query.count && query.data[j] != '&') ++j;
        String_View pair = { .data = query.data + i, .count = j - i };
        if (sv_starts_with(pair, needle)) {
            String_View val = { .data = pair.data + needle.count,
                                .count = pair.count - needle.count };
            size_t o = 0;
            for (size_t k = 0; k < val.count && o + 1 < outsz; ++k) {
                if (val.data[k] == '%' && k + 2 < val.count) {
                    int hi = hex_value(val.data[k + 1]);
                    int lo = hex_value(val.data[k + 2]);
                    if (hi >= 0 && lo >= 0) {
                        out[o++] = (char) ((hi << 4) | lo);
                        k += 2;
                        continue;
                    }
                }
                out[o++] = val.data[k];
            }
            out[o] = '\0';
            return true;
        }
        i = j + 1;
    }
    return false;
}

// Percent-encode for use inside a redirect query value.
static const char *url_encode(const char *s) {
    String_Builder b = {0};
    for (const unsigned char *p = (const unsigned char *) s; *p; ++p) {
        if (isalnum(*p) || strchr("-_.~", *p)) sb_append_buf(&b, p, 1);
        else sb_appendf(&b, "%%%02X", *p);
    }
    sb_append_null(&b);
    const char *out = temp_sprintf("%s", b.items);
    sb_free(b);
    return out;
}

static void redirect_db(Serve_Context *sc, const char *ok_key,
                        const char *err_msg)
{
    const char *loc = err_msg
        ? temp_sprintf("/db?err=%s", url_encode(err_msg))
        : temp_sprintf("/db?ok=%s", ok_key);
    http_render_redirect(sc, 302, loc);
}

/* -- pragma / count helpers (sqlite only) -------------------------------- */

static long long pragma_ll(db_t *db, const char *sql) {
    sql_stmt st = {0};
    long long v = -1;
    if (sql_prepare(db, sql, &st)) {
        if (sql_step(&st) == SQL_ROW) v = sql_column_int64(&st, 0);
        sql_finalize(&st);
    }
    return v;
}

static const char *pragma_text(db_t *db, const char *sql) {
    sql_stmt st = {0};
    const char *v = "";
    if (sql_prepare(db, sql, &st)) {
        if (sql_step(&st) == SQL_ROW) {
            const char *t = sql_column_text(&st, 0);
            if (t) v = temp_sprintf("%s", t);  // copy: invalid after finalize
        }
        sql_finalize(&st);
    }
    return v;
}

static long long table_rows(db_t *db, const char *name) {
    String_Builder q = {0};
    sb_append_cstr(&q, "SELECT count(*) FROM ");
    db_quote_ident(&q, name);
    sb_append_null(&q);
    sql_stmt st = {0};
    long long v = -1;
    if (sql_prepare(db, q.items, &st)) {
        if (sql_step(&st) == SQL_ROW) v = sql_column_int64(&st, 0);
        sql_finalize(&st);
    }
    sb_free(q);
    return v;
}

/* -- GET /db ------------------------------------------------------------ */

void serve_db_page(Serve_Context *sc) {
    char buf[DB_PATH_MAX];
    const char *err_msg = query_param(sc->query_string, "err", buf, sizeof(buf))
                          ? temp_sprintf("%s", buf) : NULL;
    const char *ok_msg = query_param(sc->query_string, "ok", buf, sizeof(buf))
                         ? temp_sprintf("%s", buf) : NULL;

    const bool db_is_user   = WEBC_ACTIVE_DB_PATH != NULL;
    const bool db_readonly  = db_is_user && WEBC_ACTIVE_READONLY;
    const bool sqlite_tools = db_is_user || webc_db_lang() == SQL_SQLITE;
    const char *db_path     = db_is_user ? WEBC_ACTIVE_DB_PATH : WEBC_DB_PATH;
    const char *sqlite_version = sqlite3_libversion();

    long long page_count = 0, page_size = 0, freelist = 0;
    long long user_version = 0, app_id = 0, encoding = 0;
    long long db_size = -1, trigger_count = 0;
    const char *journal_mode = "";
    const char *foreign_keys = "";
    Db_Object *objs = NULL;
    size_t obj_count = 0;
    long long obj_overflow = 0;
    Db_Recents recents = {0};
    db_recents_load(&recents);

    if (sqlite_tools) {
        db_t *db = open_active_db();
        if (!db) {
            free(objs);
            serve_error(sc, 500);
            return;
        }

        struct stat file_st;
        if (stat(db_path, &file_st) == 0 && S_ISREG(file_st.st_mode))
            db_size = file_st.st_size;

        page_count   = pragma_ll(db, "PRAGMA page_count;");
        page_size    = pragma_ll(db, "PRAGMA page_size;");
        freelist     = pragma_ll(db, "PRAGMA freelist_count;");
        user_version = pragma_ll(db, "PRAGMA user_version;");
        app_id       = pragma_ll(db, "PRAGMA application_id;");
        encoding     = pragma_ll(db, "PRAGMA encoding;");
        journal_mode = pragma_text(db, "PRAGMA journal_mode;");
        foreign_keys = pragma_text(db, "PRAGMA foreign_keys;");

        objs = calloc(DB_PAGE_MAX_OBJECTS, sizeof(*objs));
        sql_stmt st = {0};
        if (objs && sql_prepare(db,
                "SELECT name, type FROM sqlite_master "
                "WHERE type IN ('table','view') "
                "AND name NOT LIKE 'sqlite_%' "
                "ORDER BY type DESC, name;", &st)) {
            long long total = 0;
            Sql_Step rc = sql_step(&st);
            for (; rc == SQL_ROW; rc = sql_step(&st)) {
                total += 1;
                if (obj_count >= DB_PAGE_MAX_OBJECTS) continue;
                const char *nm = sql_column_text(&st, 0);
                const char *ty = sql_column_text(&st, 1);
                Db_Object *o = &objs[obj_count];
                snprintf(o->name, sizeof(o->name), "%s", nm ? nm : "");
                snprintf(o->type, sizeof(o->type), "%s", ty ? ty : "");
                o->rows = table_rows(db, nm ? nm : "");
                obj_count += 1;
            }
            sql_finalize(&st);
            obj_overflow = total - (long long) obj_count;
        }
        sql_stmt tst = {0};
        if (sql_prepare(db,
                "SELECT count(*) FROM sqlite_master WHERE type='trigger';",
                &tst)) {
            if (sql_step(&tst) == SQL_ROW) trigger_count = sql_column_int64(&tst, 0);
            sql_finalize(&tst);
        }
        db_close(db);
    }

    const char *db_size_str = db_size >= 0
        ? temp_sprintf("%lld bytes (%.1f KiB)", db_size, db_size / 1024.0)
        : "n/a";

    String_Builder *sb = &sc->body;
    sb->count = 0;
    render_page_header(sb, "Database", "/db");
#define OUT(buf_, size_) sb_append_buf(sb, (buf_), (size_));
#define STR(s) sb_append_cstr(sb, (s) ? (s) : "");
#define LLINT(v) sb_append_cstr(sb, temp_sprintf("%lld", (long long) (v)));
#define ESCAPED(s) sb_append_html_escaped(sb, (s) ? (s) : "");
#include "../../build/h_to_html/db.h"
#undef OUT
#undef STR
#undef LLINT
#undef ESCAPED
    render_page_footer(sb);
    free(objs);
    http_render_response(sc, 200, "text/html", sb_to_sv(*sb));
}

/* -- GET /db/file (raw bytes of the active user database) ---------------- */

// "<size>-<mtime_sec>.<mtime_nsec>" change-detection token for load/save.
static bool db_file_meta(const char *path, char *out, size_t cap) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    snprintf(out, cap, "%lld-%lld.%lld", (long long) st.st_size,
             (long long) st.st_mtim.tv_sec, (long long) st.st_mtim.tv_nsec);
    return true;
}

// Value after `name:` on a header line of the raw request (headers only,
// stops at the blank line; case-sensitive for our own X-DB-Meta header).
static String_View request_header(String_View request, const char *name) {
    size_t name_len = strlen(name);
    size_t i = 0;
    while (i < request.count) {
        size_t eol = i;
        while (eol + 1 < request.count
               && !(request.data[eol] == '\r' && request.data[eol + 1] == '\n')) {
            eol++;
        }
        String_View line = sv_from_parts(request.data + i, eol - i);
        if (line.count == 0) break;  // end of headers
        if (line.count > name_len && memcmp(line.data, name, name_len) == 0
            && line.data[name_len] == ':') {
            return sv_trim(sv_from_parts(line.data + name_len + 1,
                                         line.count - name_len - 1));
        }
        i = eol + 2;
    }
    return sv_from_parts(NULL, 0);
}

void serve_db_file(Serve_Context *sc) {
    // File transport for the browser engine: bytes only, no server SQL.
    if (!WEBC_ACTIVE_DB_PATH) {
        serve_error(sc, 404);
        return;
    }
    String_Builder file = {0};
    if (!read_entire_file(WEBC_ACTIVE_DB_PATH, &file)) {
        serve_error(sc, 404);
        return;
    }
    char meta[128];
    int has_meta = db_file_meta(WEBC_ACTIVE_DB_PATH, meta, sizeof(meta));
    // Sidecars on the device mean committed frames we are not serving
    // (stale view) and likely live writers (save is refused meanwhile).
    int has_wal = file_exists(temp_sprintf("%s-wal", WEBC_ACTIVE_DB_PATH)) > 0
        || file_exists(temp_sprintf("%s-shm", WEBC_ACTIVE_DB_PATH)) > 0;
    const char *extra = temp_sprintf("%s%s",
        has_meta ? temp_sprintf("X-DB-Meta: %s\r\n", meta) : "",
        has_wal ? "X-DB-Wal: 1\r\n" : "");
    http_render_response_headers(sc, 200, "application/octet-stream", extra,
                                 sb_to_sv(file));
    sb_free(file);
}

/* -- POST /db/save (save-back: browser bytes -> device file) ------------- */

static void save_json(Serve_Context *sc, int status, const char *msg) {
    String_Builder b = {0};
    sb_append_cstr(&b, "{\"ok\":false,\"error\":");
    sb_append_json_escaped(&b, msg);  // adds its own quotes
    sb_append_cstr(&b, "}");
    http_render_response(sc, status, "application/json", sb_to_sv(b));
    sb_free(b);
}

void serve_db_save(Serve_Context *sc) {
    if (!WEBC_ACTIVE_DB_PATH) {
        save_json(sc, 404, "no database file open");
        return;
    }
    String_View body = sb_to_sv(sc->body);
    if (body.count < 16 || memcmp(body.data, "SQLite format 3\0", 16) != 0) {
        save_json(sc, 400, "body is not a SQLite database file");
        return;
    }

    // Change detection: the client must present the meta of the bytes it
    // loaded; if the file changed on disk since then, refuse (409).
    char meta[128];
    if (!db_file_meta(WEBC_ACTIVE_DB_PATH, meta, sizeof(meta))) {
        save_json(sc, 404, "database file disappeared");
        return;
    }
    String_View want = request_header(sv_from_parts(sc->request.items,
                                                    sc->request.count),
                                      "X-DB-Meta");
    if (want.count == 0 || !sv_eq(want, sv_from_cstr(meta))) {
        save_json(sc, 409, "file changed on disk since load; reload before saving");
        return;
    }

    // WAL sidecars mean another process may hold pending state we would
    // silently discard by replacing the main file.
    const char *wal = temp_sprintf("%s-wal", WEBC_ACTIVE_DB_PATH);
    const char *shm = temp_sprintf("%s-shm", WEBC_ACTIVE_DB_PATH);
    if (file_exists(wal) > 0 || file_exists(shm) > 0) {
        save_json(sc, 409, "WAL sidecar files present; close other writers first");
        return;
    }

    // Backup current bytes, then replace atomically (temp file + rename).
    const char *bak = temp_sprintf("%s.bak", WEBC_ACTIVE_DB_PATH);
    String_Builder current = {0};
    if (read_entire_file(WEBC_ACTIVE_DB_PATH, &current)) {
        if (!write_entire_file(bak, current.items, current.count)) {
            sb_free(current);
            save_json(sc, 500, "cannot write backup file");
            return;
        }
    }
    sb_free(current);

    const char *tmp = temp_sprintf("%s.webc-new", WEBC_ACTIVE_DB_PATH);
    if (!write_entire_file(tmp, body.data, body.count)) {
        delete_file(tmp);
        save_json(sc, 500, "cannot write database file");
        return;
    }
    if (rename(tmp, WEBC_ACTIVE_DB_PATH) != 0) {
        delete_file(tmp);
        save_json(sc, 500, "cannot replace database file");
        return;
    }

    db_file_meta(WEBC_ACTIVE_DB_PATH, meta, sizeof(meta));
    String_Builder resp = {0};
    sb_append_cstr(&resp, "{\"ok\":true,\"size\":");
    sb_append_cstr(&resp, temp_sprintf("%zu", body.count));
    sb_append_cstr(&resp, ",\"meta\":");
    sb_append_json_escaped(&resp, meta);  // adds its own quotes
    sb_append_cstr(&resp, "}");
    http_render_response(sc, 200, "application/json", sb_to_sv(resp));
    sb_free(resp);
}

/* -- GET /db/browse?dir=... (JSON directory listing) --------------------- */

typedef struct {
    char      name[256];
    bool      is_dir;
    bool      is_sqlite;
    long long size;
} Browse_Entry;

static const char *join_path(const char *dir, const char *name) {
    if (dir[0] == '/' && dir[1] == '\0') return temp_sprintf("/%s", name);
    return temp_sprintf("%s/%s", dir, name);
}

static int browse_cmp(const void *a, const void *b) {
    const Browse_Entry *x = a, *y = b;
    if (x->is_dir != y->is_dir) return x->is_dir ? -1 : 1;
    return strcmp(x->name, y->name);
}

static void browse_json_error(Serve_Context *sc, const char *msg) {
    String_Builder b = {0};
    sb_append_cstr(&b, "{\"error\":");
    sb_append_json_escaped(&b, msg);
    sb_append_cstr(&b, "}");
    http_render_response(sc, 400, "application/json", sb_to_sv(b));
    sb_free(b);
}

void serve_db_browse(Serve_Context *sc) {
    char dirbuf[DB_PATH_MAX];
    const char *dir = NULL;
    if (query_param(sc->query_string, "dir", dirbuf, sizeof(dirbuf))
        && dirbuf[0]) {
        dir = dirbuf;
    }
    if (!dir) {
        dir = getenv("HOME");
        if (!dir) dir = "/";
    }

    // No path restriction by design (trusted local tool); still reject
    // non-absolute / ".." input and anything that is not a directory.
    if (!db_path_sane(dir)) {
        browse_json_error(sc, "path must be absolute and contain no `..`");
        return;
    }
    struct stat st;
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        browse_json_error(sc, "not a directory");
        return;
    }
    DIR *d = opendir(dir);
    if (!d) {
        browse_json_error(sc, "cannot list directory");
        return;
    }

    Browse_Entry *entries = calloc(BROWSE_MAX_ENTRIES, sizeof(*entries));
    if (!entries) {
        closedir(d);
        serve_error(sc, 500);
        return;
    }
    size_t count = 0;
    long long overflow = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        if (strlen(ent->d_name) >= sizeof(entries[0].name)) continue;
        if (count >= BROWSE_MAX_ENTRIES) {
            overflow += 1;
            continue;
        }
        const char *full = join_path(dir, ent->d_name);
        if (stat(full, &st) != 0) continue;
        Browse_Entry *e = &entries[count];
        snprintf(e->name, sizeof(e->name), "%s", ent->d_name);
        e->is_dir = S_ISDIR(st.st_mode);
        e->size = S_ISREG(st.st_mode) ? (long long) st.st_size : 0;
        if (S_ISREG(st.st_mode)) {
            char err[64];
            e->is_sqlite = db_path_is_sqlite(full, err, sizeof(err));
        }
        count += 1;
    }
    closedir(d);
    qsort(entries, count, sizeof(*entries), browse_cmp);

    // Parent directory ("/" is its own parent).
    char parent[DB_PATH_MAX];
    snprintf(parent, sizeof(parent), "%s", dir);
    size_t len = strlen(parent);
    while (len > 1 && parent[len - 1] == '/') parent[--len] = '\0';
    char *slash = strrchr(parent, '/');
    if (!slash) {
        snprintf(parent, sizeof(parent), "/");
    } else if (slash == parent) {
        parent[1] = '\0';
    } else {
        *slash = '\0';
    }

    String_Builder b = {0};
    sb_append_cstr(&b, "{\"dir\":");
    sb_append_json_escaped(&b, dir);
    sb_append_cstr(&b, ",\"parent\":");
    sb_append_json_escaped(&b, parent);
    sb_append_cstr(&b, ",\"entries\":[");
    for (size_t i = 0; i < count; ++i) {
        Browse_Entry *e = &entries[i];
        if (i > 0) sb_append_cstr(&b, ",");
        sb_append_cstr(&b, "{\"name\":");
        sb_append_json_escaped(&b, e->name);
        sb_append_cstr(&b, ",\"path\":");
        sb_append_json_escaped(&b, join_path(dir, e->name));
        sb_appendf(&b, ",\"dir\":%s", e->is_dir ? "true" : "false");
        sb_appendf(&b, ",\"size\":%lld", e->size);
        sb_appendf(&b, ",\"sqlite\":%s", e->is_sqlite ? "true" : "false");
        sb_append_cstr(&b, "}");
    }
    sb_appendf(&b, "],\"overflow\":%lld}", overflow);
    free(entries);
    http_render_response(sc, 200, "application/json", sb_to_sv(b));
    sb_free(b);
}

/* -- POST /db/open, /db/close ------------------------------------------- */

void serve_db_open(Serve_Context *sc) {
    String_View request = sb_to_sv(sc->request);
    String_View body = sb_to_sv(sc->body);

    Form_Field f = {0};
    char path[DB_PATH_MAX];
    path[0] = '\0';
    if (form_get(request, body, "path", &f)
        && f.value.count > 0 && f.value.count < sizeof(path)) {
        memcpy(path, f.value.data, f.value.count);
        path[f.value.count] = '\0';
    }
    if (!path[0]) {
        redirect_db(sc, NULL, "path required");
        return;
    }

    Form_Field act = {0};
    bool create = form_get(request, body, "action", &act)
                  && sv_eq(act.value, sv_from_cstr("create"));
    Form_Field rof = {0};
    bool readonly = form_get(request, body, "readonly", &rof)
                    && rof.value.count > 0;

    char err[512] = {0};
    if (!db_switch(path, readonly, create, err, sizeof(err))) {
        redirect_db(sc, NULL, err[0] ? err : "cannot open database");
        return;
    }
    redirect_db(sc, create ? "created" : "opened", NULL);
}

void serve_db_close(Serve_Context *sc) {
    db_close_active();
    redirect_db(sc, "closed", NULL);
}
