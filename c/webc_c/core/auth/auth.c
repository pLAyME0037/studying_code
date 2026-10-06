#include "auth.h"

#include <openssl/evp.h>
#include <string.h>

#include "core/http/utils.h"
#include "core/i18n/i18n.h"
#include "src/db/db.h"

// Cookie + hashing constants. Passwords are stored as "<salt>$<hex>" where
// hex = sha256(salt || password) - the 0005_auth migration seeds the demo
// staff account (sd.staff1 / posadmin1) with salt 'webc2026'.
#define SID_COOKIE "webc_sid"
#define HASH_SALT_MAX 64
#define SESSION_DAYS 7

// ---------------------------------------------------------------------------
// Passwords
// ---------------------------------------------------------------------------

static bool hash_hex(const char *pw, const char *salt, char *out /* 65 */) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  dlen = 0;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return false;
    bool ok = EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) == 1
        && EVP_DigestUpdate(ctx, salt, strlen(salt)) == 1
        && EVP_DigestUpdate(ctx, pw, strlen(pw)) == 1
        && EVP_DigestFinal_ex(ctx, digest, &dlen) == 1;
    EVP_MD_CTX_free(ctx);
    if (!ok || dlen != 32) return false;
    for (unsigned int i = 0; i < dlen; ++i) sprintf(out + 2 * i, "%02x", digest[i]);
    out[64] = '\0';
    return true;
}

static bool auth_verify(const char *pw, const char *stored) {
    const char *sep = strchr(stored, '$');
    if (!sep) return false;
    size_t salt_len = (size_t) (sep - stored);
    if (salt_len == 0 || salt_len >= HASH_SALT_MAX) return false;
    char salt[HASH_SALT_MAX];
    memcpy(salt, stored, salt_len);
    salt[salt_len] = '\0';
    char hex[65];
    if (!hash_hex(pw, salt, hex)) return false;
    return strcmp(hex, sep + 1) == 0;
}

// ---------------------------------------------------------------------------
// Sessions
// ---------------------------------------------------------------------------

// Reads webc_sid and resolves it to a live session row; returns the user id.
static bool session_user_id(Serve_Context *sc, char *out, size_t outsz) {
    String_View sid = {0};
    if (!http_cookie_find(sc, SID_COOKIE, &sid) || sid.count == 0
        || sid.count > 64) {
        return false;
    }
    db_t *db = open_webc_db();
    if (!db) return false;
    bool got = false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "SELECT user_id FROM user_sessions "
                         "WHERE id = ? "
                         "AND expires_at > strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "LIMIT 1;",
        [SQL_MYSQL]    = "SELECT user_id FROM user_sessions "
                         "WHERE id = ? AND expires_at > NOW() LIMIT 1;",
        [SQL_POSTGRES] = "SELECT user_id FROM user_sessions "
                         "WHERE id = $1 AND expires_at > now() LIMIT 1;",
    };
    sql_stmt stmt = {0};
    if (sql_prepare(db, q[db->lang], &stmt)
        && sql_bind(&stmt, 1, SQL_SV(sid))
        && sql_step(&stmt) == SQL_ROW) {
        const char *v = sql_col_text(&stmt, 0);
        if (v && v[0]) {
            snprintf(out, outsz, "%s", v);
            got = true;
        }
    }
    sql_finalize(&stmt);
    db_close(db);
    return got;
}

// ---------------------------------------------------------------------------
// Current request identity (sidebar / workspaces)
// ---------------------------------------------------------------------------

// route_request() is atomic - coroutines only yield at socket I/O outside
// it - so one request-scoped static is safe (same pattern as i18n_begin).
static Serve_Context  *g_auth_sc;
static Auth_User        g_auth_user;
static bool             g_auth_resolved;

static void auth_resolve(void) {
    g_auth_resolved = true;
    if (!g_auth_sc) return;
    char uid[64];
    if (!session_user_id(g_auth_sc, uid, sizeof(uid))) return;

    // One join loads the profile + the first live role. LEFT JOIN keeps
    // role-less users visible; a soft-deleted account resolves to NULL
    // (the sidebar then shows the generic workspace identity).
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE] =
            "SELECT u.id, u.name, u.email, u.profile_pic, "
                   "u.user_type_dict_id, u.status, COALESCE(r.role_name, '') "
            "FROM users u "
            "LEFT JOIN user_roles ur ON ur.user_id = u.id "
                                   "AND ur.deleted_at IS NULL "
            "LEFT JOIN roles r ON r.id = ur.role_id "
                             "AND r.deleted_at IS NULL "
            "WHERE u.id = ? AND u.deleted_at IS NULL LIMIT 1;",
        [SQL_MYSQL] =
            "SELECT u.id, u.name, u.email, u.profile_pic, "
                   "u.user_type_dict_id, u.status, COALESCE(r.role_name, '') "
            "FROM users u "
            "LEFT JOIN user_roles ur ON ur.user_id = u.id "
                                   "AND ur.deleted_at IS NULL "
            "LEFT JOIN roles r ON r.id = ur.role_id "
                             "AND r.deleted_at IS NULL "
            "WHERE u.id = ? AND u.deleted_at IS NULL LIMIT 1;",
        [SQL_POSTGRES] =
            "SELECT u.id, u.name, u.email, u.profile_pic, "
                   "u.user_type_dict_id, u.status, COALESCE(r.role_name, '') "
            "FROM users u "
            "LEFT JOIN user_roles ur ON ur.user_id = u.id "
                                   "AND ur.deleted_at IS NULL "
            "LEFT JOIN roles r ON r.id = ur.role_id "
                             "AND r.deleted_at IS NULL "
            "WHERE u.id = $1 AND u.deleted_at IS NULL LIMIT 1;",
    };
    db_t *db = open_webc_db();
    if (!db) return;
    sql_stmt stmt = {0};
    if (sql_prepare(db, q[db->lang], &stmt)
        && sql_bind(&stmt, 1, SQL_SV(sv_from_cstr(uid)))
        && sql_step(&stmt) == SQL_ROW) {
        const char *v;
        g_auth_user.id          = temp_strdup(uid);
        if ((v = sql_col_text(&stmt, 0)) != NULL && v[0]) g_auth_user.id = temp_strdup(v);
        if ((v = sql_col_text(&stmt, 1)) != NULL && v[0]) g_auth_user.name = temp_strdup(v);
        if ((v = sql_col_text(&stmt, 2)) != NULL && v[0]) g_auth_user.email = temp_strdup(v);
        if ((v = sql_col_text(&stmt, 3)) != NULL && v[0]) g_auth_user.profile_pic = temp_strdup(v);
        if ((v = sql_col_text(&stmt, 4)) != NULL && v[0]) g_auth_user.user_type = temp_strdup(v);
        if ((v = sql_col_text(&stmt, 5)) != NULL && v[0]) g_auth_user.status = temp_strdup(v);
        if ((v = sql_col_text(&stmt, 6)) != NULL)        g_auth_user.role = temp_strdup(v);
    }
    sql_finalize(&stmt);
    db_close(db);
}

const Auth_User *auth_current_user(void) {
    if (!g_auth_resolved) auth_resolve();
    return g_auth_user.id ? &g_auth_user : NULL;
}

// ---------------------------------------------------------------------------
// Route gate
// ---------------------------------------------------------------------------

static bool uri_is(String_View a, const char *b) {
    size_t n = strlen(b);
    return a.count == n && memcmp(a.data, b, n) == 0;
}

static bool uri_under(String_View a, const char *prefix) {
    size_t n = strlen(prefix);
    return a.count > n && memcmp(a.data, prefix, n) == 0;
}

static bool auth_guarded_uri(String_View uri) {
    return uri_is(uri, "/pos") || uri_under(uri, "/pos/")
        || uri_is(uri, "/dashboard") || uri_under(uri, "/dashboard/")
        || uri_is(uri, "/reports") || uri_under(uri, "/reports/");
}

// Percent-encode `src` into `dst` keeping only unreserved chars + '/' (so
// the login page can round-trip "uri?query" through a form field; form_get
// decodes it again).
static void uri_encode(char *dst, size_t dstsz, const char *src) {
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (size_t i = 0; src[i] && n + 4 < dstsz; ++i) {
        unsigned char c = (unsigned char) src[i];
        bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9')
            || c == '-' || c == '_' || c == '.' || c == '~' || c == '/';
        if (keep) {
            dst[n++] = (char) c;
        } else {
            dst[n++] = '%';
            dst[n++] = hex[c >> 4];
            dst[n++] = hex[c & 15];
        }
    }
    dst[n] = '\0';
}

bool auth_gate(Serve_Context *sc) {
    // Rebind the per-request identity state on every call (route_request
    // invokes auth_gate exactly once, before routing); auth_current_user()
    // fills it lazily on first use.
    g_auth_sc = sc;
    g_auth_user = (Auth_User) {0};
    g_auth_resolved = false;

    if (!auth_guarded_uri(sc->uri)) return false;
    char uid[64];
    if (session_user_id(sc, uid, sizeof(uid))) return false;

    char raw[700];
    size_t n = 0;
    int w = snprintf(raw, sizeof(raw), "%.*s", (int) sc->uri.count, sc->uri.data);
    if (w > 0) n = (size_t) w < sizeof(raw) ? (size_t) w : sizeof(raw) - 1;
    if (sc->query_string.count > 0 && n + 1 < sizeof(raw)) {
        size_t room = sizeof(raw) - n;
        w = snprintf(raw + n, room, "?%.*s",
                     (int) sc->query_string.count, sc->query_string.data);
        if (w > 0) n += (size_t) w < room ? (size_t) w : room - 1;
    }
    char enc[3 * sizeof(raw) + 4];
    uri_encode(enc, sizeof(enc), raw);
    http_render_redirect(sc, 303, temp_sprintf("/login?next=%s", enc));
    return true;
}

// ---------------------------------------------------------------------------
// Login / logout pages
// ---------------------------------------------------------------------------

static bool next_safe(String_View target) {
    return target.count > 1 && target.data[0] == '/'
        && target.data[1] != '/' && target.data[1] != '\\';
}

static void render_login(Serve_Context *sc, String_View next, const char *err) {
    String_Builder content = {0};
    sb_append_cstr(&content,
        "<div class=\"min-h-screen flex items-center justify-center"
        " bg-base\">"
        "<form method=\"POST\" action=\"/login\" data-login-form"
        " class=\"w-full max-w-xs bg-mantle border border-surface0\">"
        "<div class=\"bg-text text-onbase px-3 py-2\">"
        "<div class=\"text-sm font-semibold\">");
    sb_append_html_escaped(&content, tr("auth.title", "កត់ឈ្មោះចូល"));
    sb_append_cstr(&content, "</div>"
        "<div class=\"text-xs text-onbase/70\">POS Admin</div></div>"
        "<div class=\"px-3 py-3 flex flex-col gap-3\">");
    if (err && err[0]) {
        sb_append_cstr(&content,
            "<div data-login-error class=\"bg-red/10 border-l-2"
            " border-red text-red px-2 py-1.5 text-xs\">");
        sb_append_html_escaped(&content, err);
        sb_append_cstr(&content, "</div>");
    }
    sb_append_cstr(&content,
        "<label class=\"text-xs text-subtext0\">");
    sb_append_html_escaped(&content,
                           tr("auth.label_username", "ឈ្មោះអ្នកប្រើ"));
    sb_append_cstr(&content,
        "</label>"
        "<input name=\"username\" autocomplete=\"username\" required"
        " class=\"border border-surface0 bg-base text-text px-2 py-1.5"
        " text-sm w-full\">"
        "<label class=\"text-xs text-subtext0\">");
    sb_append_html_escaped(&content,
                           tr("auth.label_password", "ពាក្យសម្ងាត់"));
    sb_append_cstr(&content,
        "</label>"
        "<input name=\"password\" type=\"password\" autocomplete=\"current-password\""
        " required"
        " class=\"border border-surface0 bg-base text-text px-2 py-1.5"
        " text-sm w-full\">"
        "<input type=\"hidden\" name=\"next\" value=\"");
    sb_append_html_escaped(&content, next.count ? temp_sprintf("%.*s", (int) next.count, next.data) : "");
    sb_append_cstr(&content,
        "\">"
        "<button type=\"submit\" class=\"bg-blue text-onbase"
        " hover:brightness-90"
        " px-2 py-1.5 text-sm w-full\">");
    sb_append_html_escaped(&content, tr("auth.submit", "ចូល"));
    sb_append_cstr(&content,
        "</button>"
        "<a href=\"/\" class=\"text-xs text-blue hover:underline"
        " text-center\">");
    sb_append_html_escaped(&content,
                           tr("auth.back", "ត្រឡប់ទៅហាង"));
    sb_append_cstr(&content, "</a>"
        "</div></form></div>");

    sc->body.count = 0;
    String_View title = sv_from_cstr(tr("auth.page_title", "ចូលគណនី"));
    render_page_shell(sc, title, sb_to_sv(content));
    http_render_response(sc, 200, "text/html", sb_to_sv(sc->body));
    sb_free(content);
}

void serve_auth_login(Serve_Context *sc) {
    String_View next = {0};
    form_find(sc->query_string, "next", &next);
    if (!next_safe(next)) next = sv_from_cstr("/dashboard");
    render_login(sc, next, NULL);
}

void serve_auth_login_post(Serve_Context *sc) {
    String_View req  = sb_to_sv(sc->request);
    String_View body = sb_to_sv(sc->body);
    String_View username = form_text(req, body, "username");
    String_View password = form_text(req, body, "password");
    String_View next = form_text(req, body, "next");
    if (!next_safe(next)) next = sv_from_cstr("/dashboard");

    if (username.count == 0 || password.count == 0) {
        render_login(sc, next,
                     tr("auth.err_fill",
                        "សូមបំពេញឈ្មោះនិងពាក្យសម្ងាត់"));
        return;
    }

    db_t *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }

    char user_id[64] = {0};
    char stored[HASH_SALT_MAX + 80] = {0};
    {
        static const char *const q[SQL_LANG_COUNT] = {
            [SQL_SQLITE]   = "SELECT id, password_hash FROM users "
                             "WHERE (username = ? OR email = ?) "
                             "AND user_type_dict_id = 'ADMIN' "
                             "AND status = 'ACTIVE' AND deleted_at IS NULL "
                             "LIMIT 1;",
            [SQL_MYSQL]    = "SELECT id, password_hash FROM users "
                             "WHERE (username = ? OR email = ?) "
                             "AND user_type_dict_id = 'ADMIN' "
                             "AND status = 'ACTIVE' AND deleted_at IS NULL "
                             "LIMIT 1;",
            [SQL_POSTGRES] = "SELECT id, password_hash FROM users "
                             "WHERE (username = $1 OR email = $2) "
                             "AND user_type_dict_id = 'ADMIN' "
                             "AND status = 'ACTIVE' AND deleted_at IS NULL "
                             "LIMIT 1;",
        };
        sql_stmt stmt = {0};
        if (sql_prepare(db, q[db->lang], &stmt)
            && sql_bind(&stmt, 1, SQL_SV(username))
            && sql_bind(&stmt, 2, SQL_SV(username))
            && sql_step(&stmt) == SQL_ROW) {
            const char *uid  = sql_col_text(&stmt, 0);
            const char *hash = sql_col_text(&stmt, 1);
            if (uid && uid[0] && hash && hash[0]) {
                snprintf(user_id, sizeof(user_id), "%s", uid);
                snprintf(stored, sizeof(stored), "%s", hash);
            }
        }
        sql_finalize(&stmt);
    }

    bool ok = user_id[0] && stored[0]
        && auth_verify(temp_sprintf("%.*s", (int) password.count, password.data),
                       stored);
    if (!ok) {
        db_close(db);
        render_login(sc, next,
                     tr("auth.err_bad",
                        "ឈ្មោះឬពាក្យសម្ងាត់មិនត្រឹមត្រូវ"));
        return;
    }

    // Housekeeping: drop expired sessions, then mint this one. The session id
    // is generated in C so the cookie value is known without a RETURNING
    // clause (not portable across the dialects).
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE] = "DELETE FROM user_sessions "
                       "WHERE expires_at <= strftime('%Y-%m-%dT%H:%M:%fZ', 'now');",
        [SQL_MYSQL]    = "DELETE FROM user_sessions WHERE expires_at <= NOW();",
        [SQL_POSTGRES] = "DELETE FROM user_sessions WHERE expires_at <= now();",
    };
    static const char *const ins[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO user_sessions (id, user_id, expires_at) "
                         "VALUES (?, ?, strftime('%Y-%m-%dT%H:%M:%fZ','now',"
                         "'+7 days'));",
        [SQL_MYSQL]    = "INSERT INTO user_sessions (id, user_id, expires_at) "
                         "VALUES (?, ?, DATE_ADD(NOW(), INTERVAL 7 DAY));",
        [SQL_POSTGRES] = "INSERT INTO user_sessions (id, user_id, expires_at) "
                         "VALUES ($1, $2, now() + interval '7 days');",
    };
    // Housekeeping first (best effort): drop expired sessions.
    {
        sql_stmt hs = {0};
        if (sql_prepare(db, q[db->lang], &hs)) sql_final_step(&hs);
        sql_finalize(&hs);
    }
    // Mint this session. The id is generated in C so the cookie value is
    // known without a RETURNING clause (not portable across the dialects).
    char sid[40];
    bool created = webc_uuid(sid);
    sql_stmt stmt = {0};
    if (created && sql_prepare(db, ins[db->lang], &stmt)) {
        created = sql_bind(&stmt, 1, SQL_SV(sv_from_cstr(sid)))
            && sql_bind(&stmt, 2, SQL_SV(sv_from_cstr(user_id)))
            && sql_final_step(&stmt);
    } else {
        created = false;
    }
    sql_finalize(&stmt);
    db_close(db);

    if (!created) { serve_error(sc, 500); return; }
    http_set_cookie(sc, temp_sprintf(
        SID_COOKIE "=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=%d",
        sid, SESSION_DAYS * 24 * 60 * 60));
    http_render_redirect(sc, 303,
                         temp_sprintf("%.*s", (int) next.count, next.data));
}

void serve_auth_logout(Serve_Context *sc) {
    String_View sid = {0};
    if (http_cookie_find(sc, SID_COOKIE, &sid) && sid.count > 0) {
        db_t *db = open_webc_db();
        if (db) {
            static const char *const q[SQL_LANG_COUNT] = {
                [SQL_SQLITE]   = "DELETE FROM user_sessions WHERE id = ?;",
                [SQL_MYSQL]    = "DELETE FROM user_sessions WHERE id = ?;",
                [SQL_POSTGRES] = "DELETE FROM user_sessions WHERE id = $1;",
            };
            sql_stmt stmt = {0};
            if (sql_prepare(db, q[db->lang], &stmt)) {
                sql_bind(&stmt, 1, SQL_SV(sid));
                sql_final_step(&stmt);
            }
            sql_finalize(&stmt);
            db_close(db);
        }
    }
    http_set_cookie(sc, SID_COOKIE "=; Path=/; HttpOnly; Max-Age=0");
    http_render_redirect(sc, 303, "/");
}
