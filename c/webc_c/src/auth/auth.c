#include "auth.h"

#include <openssl/evp.h>
#include <string.h>

#include "src/http/utils.h"
#include "src/i18n/i18n.h"
#include "src/db/db.h"
#include "src/core/webc_template.h"

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

// Reads webc_sid and, when it maps to a live session, writes the session id
// to sid_out and its switch_group ("" when NULL) to group_out. This is the
// multi-account anchor: whichever session is active also names the browser
// group its sibling logins share. Returns false when nobody is signed in.
static bool active_session(db_t *db, Serve_Context *sc,
                           char *sid_out, size_t sid_sz,
                           char *group_out, size_t grp_sz) {
    String_View sid = {0};
    sid_out[0] = group_out[0] = '\0';
    if (!http_cookie_find(sc, SID_COOKIE, &sid) || sid.count == 0
        || sid.count > 64) {
        return false;
    }
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "SELECT id, COALESCE(switch_group, '') "
                         "FROM user_sessions WHERE id = ? "
                         "AND expires_at > strftime('%Y-%m-%dT%H:%M:%fZ','now') "
                         "LIMIT 1;",
        [SQL_MYSQL]    = "SELECT id, COALESCE(switch_group, '') "
                         "FROM user_sessions WHERE id = ? AND expires_at > NOW() "
                         "LIMIT 1;",
        [SQL_POSTGRES] = "SELECT id, COALESCE(switch_group, '') "
                         "FROM user_sessions WHERE id = $1 AND expires_at > now() "
                         "LIMIT 1;",
    };
    bool got = false;
    sql_stmt stmt = {0};
    if (sql_prepare(db, q[db->lang], &stmt)
        && sql_bind(&stmt, 1, SQL_SV(sid))
        && sql_step(&stmt) == SQL_ROW) {
        const char *id  = sql_col_text(&stmt, 0);
        const char *grp = sql_col_text(&stmt, 1);
        if (id && id[0]) {
            snprintf(sid_out, sid_sz, "%s", id);
            snprintf(group_out, grp_sz, "%s", (grp && grp[0]) ? grp : "");
            got = true;
        }
    }
    sql_finalize(&stmt);
    return got;
}

// ---------------------------------------------------------------------------
// Current request identity (sidebar / workspaces)
// ---------------------------------------------------------------------------

// route_request() is atomic - coroutines only yield at socket I/O outside
// it - so one request-scoped static is safe (same pattern as i18n_begin).
static Serve_Context *g_auth_sc;
static Auth_User      g_auth_user;
static bool           g_auth_resolved;

static void auth_resolve(void) {
    g_auth_resolved = true;
    if (!g_auth_sc) return;
    char uid[64];
    if (!session_user_id(g_auth_sc, uid, sizeof(uid))) return;

    // One join loads the profile + the first live role (+ the linked staff
    // row the dashboards workspaces key off). LEFT JOIN keeps role-less
    // users visible; a soft-deleted account resolves to NULL (the sidebar
    // then shows the generic workspace identity).
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE] =
            "SELECT u.id, u.name, u.email, u.profile_pic, "
                "u.user_type_dict_id, u.status, COALESCE(r.role_name, ''), "
                "COALESCE(r.role_code, ''), COALESCE(ur.role_id, ''), "
                "COALESCE(s.id, ''), "
                "COALESCE(s.org_unit_id, u.org_unit_id, '') "
            "FROM users u "
            "LEFT JOIN user_roles ur ON ur.user_id = u.id "
                "AND ur.deleted_at IS NULL "
            "LEFT JOIN roles r ON r.id = ur.role_id "
                "AND r.deleted_at IS NULL "
            "LEFT JOIN staff s ON s.user_id = u.id "
                "AND s.deleted_at IS NULL "
            "WHERE u.id = ? AND u.deleted_at IS NULL LIMIT 1;",
        [SQL_MYSQL] =
            "SELECT u.id, u.name, u.email, u.profile_pic, "
                "u.user_type_dict_id, u.status, COALESCE(r.role_name, ''), "
                "COALESCE(r.role_code, ''), COALESCE(ur.role_id, ''), "
                "COALESCE(s.id, ''), "
                "COALESCE(s.org_unit_id, u.org_unit_id, '') "
            "FROM users u "
            "LEFT JOIN user_roles ur ON ur.user_id = u.id "
                "AND ur.deleted_at IS NULL "
            "LEFT JOIN roles r ON r.id = ur.role_id "
                "AND r.deleted_at IS NULL "
            "LEFT JOIN staff s ON s.user_id = u.id "
                "AND s.deleted_at IS NULL "
            "WHERE u.id = ? AND u.deleted_at IS NULL LIMIT 1;",
        [SQL_POSTGRES] =
            "SELECT u.id, u.name, u.email, u.profile_pic, "
                "u.user_type_dict_id, u.status, COALESCE(r.role_name, ''), "
                "COALESCE(r.role_code, ''), COALESCE(ur.role_id, ''), "
                "COALESCE(s.id, ''), "
                "COALESCE(s.org_unit_id, u.org_unit_id, '') "
            "FROM users u "
            "LEFT JOIN user_roles ur ON ur.user_id = u.id "
                "AND ur.deleted_at IS NULL "
            "LEFT JOIN roles r ON r.id = ur.role_id "
                "AND r.deleted_at IS NULL "
            "LEFT JOIN staff s ON s.user_id = u.id "
                "AND s.deleted_at IS NULL "
            "WHERE u.id = $1 AND u.deleted_at IS NULL LIMIT 1;",
    };
    db_t *db = open_webc_db();
    if (!db) return;
    char rid[64] = {0};
    sql_stmt stmt = {0};
    if (sql_prepare(db, q[db->lang], &stmt)
        && sql_bind(&stmt, 1, SQL_SV(sv_from_cstr(uid)))
        && sql_step(&stmt) == SQL_ROW) {
        const char *v;
        g_auth_user.id          = temp_strdup(uid);
        g_auth_user.role_code   = "";
        g_auth_user.staff_id    = "";
        g_auth_user.org_unit_id = "";
        g_auth_user.perms       = "";
        if ((v = sql_col_text(&stmt, 0)) != NULL && v[0]) g_auth_user.id = temp_strdup(v);
        if ((v = sql_col_text(&stmt, 1)) != NULL && v[0]) g_auth_user.name = temp_strdup(v);
        if ((v = sql_col_text(&stmt, 2)) != NULL && v[0]) g_auth_user.email = temp_strdup(v);
        if ((v = sql_col_text(&stmt, 3)) != NULL && v[0]) g_auth_user.profile_pic = temp_strdup(v);
        if ((v = sql_col_text(&stmt, 4)) != NULL && v[0]) g_auth_user.user_type = temp_strdup(v);
        if ((v = sql_col_text(&stmt, 5)) != NULL && v[0]) g_auth_user.status = temp_strdup(v);
        if ((v = sql_col_text(&stmt, 6)) != NULL)         g_auth_user.role = temp_strdup(v);
        if ((v = sql_col_text(&stmt, 7)) != NULL && v[0]) g_auth_user.role_code = temp_strdup(v);
        if ((v = sql_col_text(&stmt, 8)) != NULL && v[0])
            snprintf(rid, sizeof(rid), "%s", v);
        if ((v = sql_col_text(&stmt, 9)) != NULL && v[0]) g_auth_user.staff_id = temp_strdup(v);
        if ((v = sql_col_text(&stmt, 10)) != NULL && v[0]) g_auth_user.org_unit_id = temp_strdup(v);
    }
    sql_finalize(&stmt);

    // Phase 14: the role's permission codes, joined ",CODE," so
    // auth_has_perm() can needle-match without splitting. Runs once per
    // request (auth_gate rebinds and the resolved flag re-arms it).
    if (rid[0]) {
        static const char *const pq[SQL_LANG_COUNT] = {
            [SQL_SQLITE] =
                "SELECT p.perm_code FROM role_permissions rp "
                "JOIN permissions p ON p.id = rp.permission_id "
                "WHERE rp.role_id = ? AND rp.deleted_at IS NULL "
                  "AND p.deleted_at IS NULL;",
            [SQL_MYSQL] =
                "SELECT p.perm_code FROM role_permissions rp "
                "JOIN permissions p ON p.id = rp.permission_id "
                "WHERE rp.role_id = ? AND rp.deleted_at IS NULL "
                  "AND p.deleted_at IS NULL;",
            [SQL_POSTGRES] =
                "SELECT p.perm_code FROM role_permissions rp "
                "JOIN permissions p ON p.id = rp.permission_id "
                "WHERE rp.role_id = $1 AND rp.deleted_at IS NULL "
                  "AND p.deleted_at IS NULL;",
        };
        sql_stmt p2 = {0};
        if (sql_prepare(db, pq[db->lang], &p2)
            && sql_bind(&p2, 1, SQL_SV(sv_from_cstr(rid)))) {
            char perms[640] = ",";
            while (sql_step(&p2) == SQL_ROW) {
                const char *pc = sql_col_text(&p2, 0);
                if (pc && pc[0]
                    && strlen(perms) + strlen(pc) + 2 < sizeof(perms)) {
                    strcat(perms, pc);
                    strcat(perms, ",");
                }
            }
            g_auth_user.perms = temp_strdup(perms);
        }
        sql_finalize(&p2);
    }
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
    if (session_user_id(sc, uid, sizeof(uid))) {
        // Phase 16 signup: a live CUSTOMER session never enters the staff
        // areas - it goes back to the storefront instead of the login
        // form (a live ADMIN session sails through to the page itself).
        const Auth_User *au = auth_current_user();
        if (au && au->user_type && au->user_type[0]
            && strcmp(au->user_type, "ADMIN") != 0) {
            http_render_redirect(sc, 303, "/");
            return true;
        }
        return false;
    }

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
// Phase 14: page-level RBAC
// ---------------------------------------------------------------------------

bool auth_has_perm(const char *code) {
    if (!code || !code[0]) return false;
    const Auth_User *au = auth_current_user();
    if (!au || !au->perms || !au->perms[0]) return false;
    char needle[96];
    snprintf(needle, sizeof(needle), ",%s,", code);
    return strstr(au->perms, needle) != NULL;
}

// One required permission per guarded route prefix (page level: the whole
// /pos/<area> including its child CRUD POSTs). /dashboard is deliberately
// absent - every signed-in role reaches it and it role-dispatches its own
// workspace instead; /pos itself has no route.
typedef struct {
    const char *prefix;
    const char *perm;
} Perm_Rule;

static const Perm_Rule perm_rules[] = {
    { "/pos/products",     "SD.PRODUCT.MGMT" },
    { "/pos/categories",   "SD.PRODUCT.MGMT" },
    { "/pos/orders",       "SD.SELL" },
    { "/pos/sale",         "SD.SELL" },
    { "/pos/shifts",       "SD.SELL" },
    { "/pos/customers",    "SD.SELL" },
    { "/pos/finance",      "SD.FINANCE" },
    { "/pos/users",        "SD.USER.MGMT" },
    { "/pos/staff",        "SD.USER.MGMT" },
    { "/pos/org",          "SD.USER.MGMT" },
    { "/pos/roles",        "SD.USER.MGMT" },
    { "/pos/permissions",  "SD.USER.MGMT" },
    { "/pos/stocks",       "SD.STOCK.ADJUST" },
    { "/pos/provinces",    "SD.SETTINGS" },
    { "/pos/districts",    "SD.SETTINGS" },
    { "/pos/communes",     "SD.SETTINGS" },
    { "/pos/villages",     "SD.SETTINGS" },
    { "/pos/dictionaries", "SD.SETTINGS" },
    { "/pos/i18n",         "SD.SETTINGS" },
    { "/pos/config",       "SD.SETTINGS" },
    { "/pos/alerts",       "SD.AUDIT" },
    { "/pos/audit",        "SD.AUDIT" },
    { "/reports",          "SD.REPORT.VIEW" },
};

static const char *uri_required_perm(String_View uri) {
    for (size_t i = 0; i < sizeof(perm_rules) / sizeof(perm_rules[0]); ++i) {
        size_t n = strlen(perm_rules[i].prefix);
        if (uri.count >= n && memcmp(uri.data, perm_rules[i].prefix, n) == 0
            && (uri.count == n || uri.data[n] == '/'))
            return perm_rules[i].perm;
    }
    return NULL;
}

bool perm_gate(Serve_Context *sc) {
    const char *need = uri_required_perm(sc->uri);
    if (!need) return false;
    const Auth_User *au = auth_current_user();
    if (au && auth_has_perm(need)) return false;

    const char *role = (au && au->role && au->role[0]) ? au->role : "—";
    String_Builder content = {0};
    sb_append_cstr(&content,
        "<div class=\"min-h-screen flex items-center justify-center"
        " bg-base px-4 sm:px-6 lg:px-8 py-6\">"
        "<div class=\"w-full bg-mantle border border-surface0\">"
        "<div class=\"bg-red text-onbase px-3 py-2 flex items-center"
        " justify-between\">"
        "<span class=\"text-sm font-semibold\">403 · Access restricted</span>"
        "<span class=\"text-xs opacity-80\">POS Admin</span></div>"
        "<div class=\"px-3 py-3 flex flex-col gap-2\">"
        "<p class=\"text-sm text-text\">Your role does not include this"
        " area.</p>"
        "<p class=\"text-xs text-subtext0\">Role: <span class=\"text-text"
        " font-medium\">");
    sb_append_html_escaped(&content, role);
    sb_append_cstr(&content, "</span> · Required: <code class=\"text-peach\">");
    sb_append_html_escaped(&content, need);
    sb_append_cstr(&content,
        "</code></p>"
        "<div class=\"flex gap-2 pt-1\">"
        "<a href=\"/dashboard\" class=\"bg-blue text-onbase"
        " hover:brightness-90 px-2 py-1.5 text-xs\">Back to workspace</a>"
        "<a href=\"/\" class=\"border border-surface0 text-subtext0"
        " hover:bg-surface0 px-2 py-1.5 text-xs\">Storefront</a>"
        "</div></div></div></div>");

    sc->body.count = 0;
    render_page_shell(sc, sv_from_cstr("403 · Access restricted"),
                      sb_to_sv(content));
    http_render_response(sc, 403, "text/html", sb_to_sv(sc->body));
    sb_free(content);
    return true;
}

// ---------------------------------------------------------------------------
// Login / logout pages
// ---------------------------------------------------------------------------

static bool next_safe(String_View target) {
    return target.count > 1 && target.data[0] == '/'
        && target.data[1] != '/' && target.data[1] != '\\';
}

// Sign-in page: display/auth/login.h.tt (error banner + next round-trip
// are code state -> template, per the design rule in wiki/templates.md).
static void render_login(Serve_Context *sc, String_View next, const char *err) {
    const char *nexts = next.count
        ? temp_sprintf("%.*s", (int) next.count, next.data) : "";
    String_Builder content = {0};
    String_Builder *sb = &content;
#include "build/h_to_html/auth/login.h"
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

// Housekeeping + session mint shared by the login and signup flows: drop
// expired rows, insert the new one, hand the C-generated id back so the
// caller can set the cookie (no RETURNING clause across the dialects).
static bool auth_mint_session(db_t *db, const char *user_id,
                              const char *group /* NULL = mint fresh */,
                              char *sid /* 40 */) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE] = "DELETE FROM user_sessions "
                       "WHERE expires_at <= strftime('%Y-%m-%dT%H:%M:%fZ', 'now');",
        [SQL_MYSQL]    = "DELETE FROM user_sessions WHERE expires_at <= NOW();",
        [SQL_POSTGRES] = "DELETE FROM user_sessions WHERE expires_at <= now();",
    };
    static const char *const ins[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO user_sessions "
                         "(id, user_id, switch_group, expires_at) "
                         "VALUES (?, ?, ?, strftime('%Y-%m-%dT%H:%M:%fZ','now',"
                         "'+7 days'));",
        [SQL_MYSQL]    = "INSERT INTO user_sessions "
                         "(id, user_id, switch_group, expires_at) "
                         "VALUES (?, ?, ?, DATE_ADD(NOW(), INTERVAL 7 DAY));",
        [SQL_POSTGRES] = "INSERT INTO user_sessions "
                         "(id, user_id, switch_group, expires_at) "
                         "VALUES ($1, $2, $3, now() + interval '7 days');",
    };
    // Housekeeping first (best effort): drop expired sessions.
    {
        sql_stmt hs = {0};
        if (sql_prepare(db, q[db->lang], &hs)) sql_final_step(&hs);
        sql_finalize(&hs);
    }
    bool created = webc_uuid(sid);
    // Join the caller's browser group when given; otherwise mint a fresh one
    // (a first login on a new browser starts a group by itself).
    char grp[40] = {0};
    if (group && group[0]) {
        snprintf(grp, sizeof(grp), "%s", group);
    } else if (!webc_uuid(grp)) {
        created = false;
    }
    sql_stmt stmt = {0};
    if (created && sql_prepare(db, ins[db->lang], &stmt)) {
        created = sql_bind(&stmt, 1, SQL_SV(sv_from_cstr(sid)))
            && sql_bind(&stmt, 2, SQL_SV(sv_from_cstr(user_id)))
            && sql_bind(&stmt, 3, SQL_SV(sv_from_cstr(grp)))
            && sql_final_step(&stmt);
    } else {
        created = false;
    }
    sql_finalize(&stmt);
    return created;
}

static void auth_send_session(Serve_Context *sc, const char *sid) {
    http_set_cookie(sc, temp_sprintf(
        SID_COOKIE "=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=%d",
        sid, SESSION_DAYS * 24 * 60 * 60));
}

// The browser's account group: reuse the switch_group of whatever session is
// already active on this browser (so a second login joins it); a session
// minted before 0010 has none and is stamped here so it can participate; a
// fresh browser (nobody signed in) starts a brand-new group. The group lives
// only in the DB, anchored by the active webc_sid - no second cookie.
static bool browser_group(db_t *db, Serve_Context *sc, char *grp /* 40 */) {
    char sid[40] = {0}, cur[40] = {0};
    if (!active_session(db, sc, sid, sizeof(sid), cur, sizeof(cur)))
        return webc_uuid(grp);  // nobody signed in on this browser yet
    if (cur[0]) { snprintf(grp, 40, "%s", cur); return true; }

    // Active session predates the group column: stamp it now so this login
    // links to it instead of orphaning it.
    if (!webc_uuid(grp)) return false;
    static const char *const u[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE user_sessions SET switch_group = ? WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE user_sessions SET switch_group = ? WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE user_sessions SET switch_group = $1 WHERE id = $2;",
    };
    bool ok = false;
    sql_stmt st = {0};
    if (sql_prepare(db, u[db->lang], &st)
        && sql_bind(&st, 1, SQL_SV(sv_from_cstr(grp)))
        && sql_bind(&st, 2, SQL_SV(sv_from_cstr(sid)))
        && sql_final_step(&st)) {
        ok = true;
    }
    sql_finalize(&st);
    return ok;
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
    char user_type[32] = {0};
    char stored[HASH_SALT_MAX + 80] = {0};
    {
        // Phase 16 signup: any ACTIVE account with a password may sign in
        // (the old gate was ADMIN-only); the non-ADMIN landing target is
        // decided after the verify below.
        static const char *const q[SQL_LANG_COUNT] = {
            [SQL_SQLITE]   = "SELECT id, password_hash, user_type_dict_id "
                             "FROM users "
                             "WHERE (username = ? OR email = ?) "
                             "AND status = 'ACTIVE' AND deleted_at IS NULL "
                             "LIMIT 1;",
            [SQL_MYSQL]    = "SELECT id, password_hash, user_type_dict_id "
                             "FROM users "
                             "WHERE (username = ? OR email = ?) "
                             "AND status = 'ACTIVE' AND deleted_at IS NULL "
                             "LIMIT 1;",
            [SQL_POSTGRES] = "SELECT id, password_hash, user_type_dict_id "
                             "FROM users "
                             "WHERE (username = $1 OR email = $2) "
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
            const char *type = sql_col_text(&stmt, 2);
            if (uid && uid[0] && hash && hash[0]) {
                snprintf(user_id, sizeof(user_id), "%s", uid);
                snprintf(stored, sizeof(stored), "%s", hash);
            }
            if (type && type[0]) snprintf(user_type, sizeof(user_type), "%s", type);
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
    // ADMIN follows the (validated) staff next= target; CUSTOMER sessions
    // belong on the storefront, so a crafted ?next=/dashboard bounces.
    if (user_type[0] && strcmp(user_type, "ADMIN") != 0)
        next = sv_from_cstr("/");

    char sid[40];
    char group[40] = {0};
    bool grouped = browser_group(db, sc, group);
    bool created = grouped && auth_mint_session(db, user_id, group, sid);
    db_close(db);

    if (!created) { serve_error(sc, 500); return; }
    auth_send_session(sc, sid);
    http_render_redirect(sc, 303,
                         temp_sprintf("%.*s", (int) next.count, next.data));
}

// ---------------------------------------------------------------------------
// Phase 16 signup: public permanent accounts. A fresh buyer inserts a
// CUSTOMER row; a phone that already matches a guest checkout account
// (0007 keeps password_hash empty until the buyer deliberately claims it)
// is claimed by filling name/username/email/password. The minted session
// is the same one login produces, so the storefront keeps the identity.
// ---------------------------------------------------------------------------

typedef struct {
    const char *input;   // form field name
    const char *trk;     // i18n key for the label
    const char *def;     // Khmer fallback label
    const char *type;    // input type
    const char *ac;      // autocomplete token
    int min_len;         // minlength (password), 0 = none
} Signup_Field;

static const Signup_Field signup_fields[] = {
    { "name",     "auth.su_name",        "ឈ្មោះពេញ",
      "text",     "name", 0 },
    { "username", "auth.label_username", "ឈ្មោះអ្នកប្រើ",
      "text",     "username", 0 },
    { "phone",    "auth.su_phone",       "លេខទូរស័ព្ទ",
      "tel",      "tel", 0 },
    { "email",    "auth.su_email",       "អ៊ីមែល",
      "email",    "email", 0 },
    { "password", "auth.label_password", "ពាក្យសម្ងាត់",
      "password", "new-password", 6 },
};

// Signup page: display/auth/signup.h.tt (field loop + error banner are
// code state -> template, per the design rule in wiki/templates.md).
static void render_signup(Serve_Context *sc, const char *err) {
    String_Builder content = {0};
    String_Builder *sb = &content;
#include "build/h_to_html/auth/signup.h"
    sc->body.count = 0;
    String_View title = sv_from_cstr(tr("auth.signup_title", "បង្កើតគណនីថ្មី"));
    render_page_shell(sc, title, sb_to_sv(content));
    http_render_response(sc, 200, "text/html", sb_to_sv(sc->body));
    sb_free(content);
}

void serve_auth_signup(Serve_Context *sc) {
    render_signup(sc, NULL);
}

void serve_auth_signup_post(Serve_Context *sc) {
    String_View req  = sb_to_sv(sc->request);
    String_View body = sb_to_sv(sc->body);
    String_View name     = form_text(req, body, "name");
    String_View username = form_text(req, body, "username");
    String_View phone    = form_text(req, body, "phone");
    String_View email    = form_text(req, body, "email");
    String_View password = form_text(req, body, "password");

    const char *err = NULL;
    if (name.count == 0 || username.count == 0 || phone.count == 0
        || email.count == 0 || password.count == 0) {
        err = tr("auth.su_err_fill", "សូមបំពេញព័ត៌មានទាំងអស់");
    } else if (password.count < 6) {
        err = tr("auth.su_err_short",
                 "ពាក្យសម្ងាត់ត្រូវមានយ៉ាងតិច៦អក្ខរា");
    }
    if (err) { render_signup(sc, err); return; }

    db_t *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }

    char uid[64] = {0};
    bool claim = false;
    char sid[40] = {0};

    // The phone decides insert-vs-claim-vs-reject: an existing row with an
    // empty password_hash is the guest checkout account (0007) and gets
    // claimed; a hashed row means the phone is already registered.
    {
        static const char *const q[SQL_LANG_COUNT] = {
            [SQL_SQLITE]   = "SELECT id, COALESCE(password_hash, '') "
                             "FROM users WHERE phone = ? LIMIT 1;",
            [SQL_MYSQL]    = "SELECT id, COALESCE(password_hash, '') "
                             "FROM users WHERE phone = ? LIMIT 1;",
            [SQL_POSTGRES] = "SELECT id, COALESCE(password_hash, '') "
                             "FROM users WHERE phone = $1 LIMIT 1;",
        };
        sql_stmt stmt = {0};
        if (sql_prepare(db, q[db->lang], &stmt)
            && sql_bind(&stmt, 1, SQL_SV(phone))
            && sql_step(&stmt) == SQL_ROW) {
            const char *id  = sql_col_text(&stmt, 0);
            const char *hsh = sql_col_text(&stmt, 1);
            if (hsh && hsh[0]) {
                err = tr("auth.su_err_phone",
                         "លេខទូរស័ព្ទនេះបានចុះឈ្មោះរួមហើយ");
            } else if (id && id[0]) {
                snprintf(uid, sizeof(uid), "%s", id);
                claim = true;
            }
        }
        sql_finalize(&stmt);
    }

    // Username and email must be free (the claimed row may keep its own).
    if (!err) {
        static const char *const q_user[SQL_LANG_COUNT] = {
            [SQL_SQLITE]   = "SELECT id FROM users WHERE username = ? LIMIT 1;",
            [SQL_MYSQL]    = "SELECT id FROM users WHERE username = ? LIMIT 1;",
            [SQL_POSTGRES] = "SELECT id FROM users WHERE username = $1 LIMIT 1;",
        };
        static const char *const q_mail[SQL_LANG_COUNT] = {
            [SQL_SQLITE]   = "SELECT id FROM users WHERE email = ? LIMIT 1;",
            [SQL_MYSQL]    = "SELECT id FROM users WHERE email = ? LIMIT 1;",
            [SQL_POSTGRES] = "SELECT id FROM users WHERE email = $1 LIMIT 1;",
        };
        for (int which = 0; !err && which < 2; ++which) {
            String_View sv = which == 0 ? username : email;
            sql_stmt stmt = {0};
            if (sql_prepare(db, which == 0 ? q_user[db->lang] : q_mail[db->lang],
                            &stmt)
                && sql_bind(&stmt, 1, SQL_SV(sv))
                && sql_step(&stmt) == SQL_ROW) {
                const char *id = sql_col_text(&stmt, 0);
                if (!claim || !id || strcmp(id, uid) != 0)
                    err = tr("auth.su_err_taken",
                             "ឈ្មោះ អ៊ីមែល ឬលេខទូរស័ព្ទនេះបានប្រើរួមហើយ");
            }
            sql_finalize(&stmt);
        }
    }

    // Per-account salt: stored as "<salt>$<hex>" exactly like 0005_auth,
    // auth_verify reads the salt back out of the stored string.
    char stored[HASH_SALT_MAX + 80] = {0};
    if (!err) {
        char salt[40] = {0};
        char hex[65] = {0};
        const char *pw = temp_sprintf("%.*s", (int) password.count, password.data);
        if (!webc_uuid(salt)) {
            err = tr("auth.su_err_sys", "មិនអាចបង្កើតគណនីបានទេ");
        } else {
            salt[16] = '\0';   // 16 uuid chars is plenty of salt
            if (!hash_hex(pw, salt, hex)) {
                err = tr("auth.su_err_sys", "មិនអាចបង្កើតគណនីបានទេ");
            } else {
                snprintf(stored, sizeof(stored), "%s$%s", salt, hex);
            }
        }
    }

    // Insert the fresh CUSTOMER row (DDL default) or claim the guest one
    // by filling the credentials - guest rows keep their customer link.
    if (!err) {
        bool wrote = false;
        if (claim) {
            static const char *const q[SQL_LANG_COUNT] = {
                [SQL_SQLITE]   = "UPDATE users SET name = ?, username = ?, "
                                 "email = ?, password_hash = ? WHERE id = ?;",
                [SQL_MYSQL]    = "UPDATE users SET name = ?, username = ?, "
                                 "email = ?, password_hash = ? WHERE id = ?;",
                [SQL_POSTGRES] = "UPDATE users SET name = $1, username = $2, "
                                 "email = $3, password_hash = $4 WHERE id = $5;",
            };
            sql_stmt stmt = {0};
            if (sql_prepare(db, q[db->lang], &stmt)
                && sql_bind(&stmt, 1, SQL_SV(name))
                && sql_bind(&stmt, 2, SQL_SV(username))
                && sql_bind(&stmt, 3, SQL_SV(email))
                && sql_bind(&stmt, 4, SQL_SV(sv_from_cstr(stored)))
                && sql_bind(&stmt, 5, SQL_SV(sv_from_cstr(uid)))
                && sql_final_step(&stmt)) {
                wrote = true;
            }
            sql_finalize(&stmt);
        } else {
            static const char *const q[SQL_LANG_COUNT] = {
                [SQL_SQLITE]   = "INSERT INTO users "
                                 "(id, name, username, email, phone, "
                                 "password_hash) VALUES (?, ?, ?, ?, ?, ?);",
                [SQL_MYSQL]    = "INSERT INTO users "
                                 "(id, name, username, email, phone, "
                                 "password_hash) VALUES (?, ?, ?, ?, ?, ?);",
                [SQL_POSTGRES] = "INSERT INTO users "
                                 "(id, name, username, email, phone, "
                                 "password_hash) VALUES ($1, $2, $3, $4, $5, $6);",
            };
            if (webc_uuid(uid)) {
                sql_stmt stmt = {0};
                if (sql_prepare(db, q[db->lang], &stmt)
                    && sql_bind(&stmt, 1, SQL_SV(sv_from_cstr(uid)))
                    && sql_bind(&stmt, 2, SQL_SV(name))
                    && sql_bind(&stmt, 3, SQL_SV(username))
                    && sql_bind(&stmt, 4, SQL_SV(email))
                    && sql_bind(&stmt, 5, SQL_SV(phone))
                    && sql_bind(&stmt, 6, SQL_SV(sv_from_cstr(stored)))
                    && sql_final_step(&stmt)) {
                    wrote = true;
                }
                sql_finalize(&stmt);
            }
        }
        // A race lost against the UNIQUE indexes reads as "already taken".
        if (!wrote)
            err = tr("auth.su_err_taken",
                     "ឈ្មោះ អ៊ីមែល ឬលេខទូរស័ព្ទនេះបានប្រើរួមហើយ");
    }

    bool minted = false;
    if (!err) {
        char group[40] = {0};
        minted = browser_group(db, sc, group)
            && auth_mint_session(db, uid, group, sid);
    }
    db_close(db);

    if (err) { render_signup(sc, err); return; }
    if (!minted) { serve_error(sc, 500); return; }
    auth_send_session(sc, sid);
    http_render_redirect(sc, 303, "/");
}

// Signs out of this browser entirely: every session in the browser's account
// group is deleted and the cookie cleared. A group never spans browsers, so
// this only ends sessions the user signed into here (a lone account behaves
// exactly as before). Drop a single account instead via /accounts (op=remove).
void serve_auth_logout(Serve_Context *sc) {
    char sid[40] = {0}, group[40] = {0};
    db_t *db = open_webc_db();
    if (db) {
        if (active_session(db, sc, sid, sizeof(sid), group, sizeof(group))) {
            static const char *const del[SQL_LANG_COUNT] = {
                [SQL_SQLITE]   = "DELETE FROM user_sessions "
                                 "WHERE id = ? OR switch_group = ?;",
                [SQL_MYSQL]    = "DELETE FROM user_sessions "
                                 "WHERE id = ? OR switch_group = ?;",
                [SQL_POSTGRES] = "DELETE FROM user_sessions "
                                 "WHERE id = $1 OR switch_group = $2;",
            };
            sql_stmt stmt = {0};
            if (sql_prepare(db, del[db->lang], &stmt)) {
                sql_bind(&stmt, 1, SQL_SV(sv_from_cstr(sid)));
                sql_bind(&stmt, 2, SQL_SV(sv_from_cstr(group)));
                sql_final_step(&stmt);
            }
            sql_finalize(&stmt);
        }
        db_close(db);
    }
    http_set_cookie(sc, SID_COOKIE "=; Path=/; HttpOnly; Max-Age=0");
    http_render_redirect(sc, 303, "/");
}

// ---------------------------------------------------------------------------
// Multi-account: /accounts (list + hop) and /switch (POST). Sessions share a
// switch_group per browser, so switching is just repointing webc_sid at a
// sibling session - no second cookie, no password re-entry.
// ---------------------------------------------------------------------------

#define ACCT_MAX 16  // accounts shown on the switcher (deduped per user)

// One switcher row for display/auth/accounts.h.tt: the SQL text is copied
// out before finalize so the template renders stable storage.
typedef struct {
    char sid[40];
    char name[64];
    char mail[96];
    char type[64];
    bool is_me;
} Acct_Row;

// /accounts switcher shell: display/auth/accounts.h.tt (current-vs-switch
// row state is code state -> template, per the design rule in
// wiki/templates.md).
static void render_accounts(Serve_Context *sc, const Acct_Row *accts, size_t naccts) {
    String_Builder content = {0};
    String_Builder *sb = &content;
#include "build/h_to_html/auth/accounts.h"
    sc->body.count = 0;
    render_page_shell(sc, sv_from_cstr(tr("auth.acct_title", "គណនី")),
                      sb_to_sv(content));
    http_render_response(sc, 200, "text/html", sb_to_sv(sc->body));
    sb_free(content);
}

void serve_auth_accounts(Serve_Context *sc) {
    const Auth_User *me = auth_current_user();
    if (!me || !me->id) {
        http_render_redirect(sc, 303, "/login?next=/accounts");
        return;
    }
    db_t *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }

    char my_sid[40] = {0}, group[40] = {0};
    active_session(db, sc, my_sid, sizeof(my_sid), group, sizeof(group));

    Acct_Row accts[ACCT_MAX];
    size_t naccts = 0;
    char seen[ACCT_MAX][64];
    size_t nseen = 0;
    if (group[0]) {
        // Sibling sessions in this browser group, newest session first; the
        // loop keeps only the newest live session per user so a user who
        // signed in twice is listed once.
        static const char *const q[SQL_LANG_COUNT] = {
            [SQL_SQLITE]   = "SELECT s.id, u.id, u.name, COALESCE(u.email,''), "
                             "u.user_type_dict_id, COALESCE(u.profile_pic,'') "
                             "FROM user_sessions s JOIN users u ON u.id = s.user_id "
                             "WHERE s.switch_group = ? "
                             "AND s.expires_at > strftime('%Y-%m-%dT%H:%M:%fZ','now') "
                             "AND u.deleted_at IS NULL "
                             "ORDER BY s.created_at DESC;",
            [SQL_MYSQL]    = "SELECT s.id, u.id, u.name, COALESCE(u.email,''), "
                             "u.user_type_dict_id, COALESCE(u.profile_pic,'') "
                             "FROM user_sessions s JOIN users u ON u.id = s.user_id "
                             "WHERE s.switch_group = ? AND s.expires_at > NOW() "
                             "AND u.deleted_at IS NULL "
                             "ORDER BY s.created_at DESC;",
            [SQL_POSTGRES] = "SELECT s.id, u.id, u.name, COALESCE(u.email,''), "
                             "u.user_type_dict_id, COALESCE(u.profile_pic,'') "
                             "FROM user_sessions s JOIN users u ON u.id = s.user_id "
                             "WHERE s.switch_group = $1 AND s.expires_at > now() "
                             "AND u.deleted_at IS NULL "
                             "ORDER BY s.created_at DESC;",
        };
        sql_stmt st = {0};
        if (sql_prepare(db, q[db->lang], &st)
            && sql_bind(&st, 1, SQL_SV(sv_from_cstr(group)))) {
            while (sql_step(&st) == SQL_ROW && nseen < ACCT_MAX) {
                const char *sid  = sql_col_text(&st, 0);
                const char *uid  = sql_col_text(&st, 1);
                const char *name = sql_col_text(&st, 2);
                const char *mail = sql_col_text(&st, 3);
                const char *type = sql_col_text(&st, 4);
                if (!sid || !uid) continue;
                bool dup = false;
                for (size_t i = 0; i < nseen; ++i)
                    if (strcmp(seen[i], uid) == 0) { dup = true; break; }
                if (dup) continue;
                snprintf(seen[nseen++], sizeof(seen[0]), "%s", uid);

                if (naccts >= ACCT_MAX) break;
                Acct_Row *ar = &accts[naccts++];
                snprintf(ar->sid,  sizeof ar->sid,  "%s", sid);
                snprintf(ar->name, sizeof ar->name, "%s", name ? name : "");
                snprintf(ar->mail, sizeof ar->mail, "%s", mail ? mail : "");
                snprintf(ar->type, sizeof ar->type, "%s", type ? type : "");
                ar->is_me = strcmp(sid, my_sid) == 0;
            }
        }
        sql_finalize(&st);
    }
    db_close(db);
    render_accounts(sc, accts, naccts);
}

void serve_auth_switch(Serve_Context *sc) {
    const Auth_User *me = auth_current_user();
    if (!me || !me->id) {
        http_render_redirect(sc, 303, "/login?next=/accounts");
        return;
    }
    String_View req  = sb_to_sv(sc->request);
    String_View body = sb_to_sv(sc->body);
    String_View op   = form_text(req, body, "op");
    String_View targ = form_text(req, body, "sid");
    String_View next = form_text(req, body, "next");
    if (!next_safe(next)) next = sv_from_cstr("/accounts");
    if (targ.count == 0 || targ.count > 64) {
        http_render_redirect(sc, 303, "/accounts");
        return;
    }

    db_t *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }
    char my_sid[40] = {0}, group[40] = {0};
    active_session(db, sc, my_sid, sizeof(my_sid), group, sizeof(group));
    if (!group[0]) {  // no group = nothing to switch between
        db_close(db);
        http_render_redirect(sc, 303, "/accounts");
        return;
    }

    if (op.count == 6 && strncmp(op.data, "remove", 6) == 0) {
        // Drop a sibling session from MY group only (never the active one).
        static const char *const q[SQL_LANG_COUNT] = {
            [SQL_SQLITE]   = "DELETE FROM user_sessions "
                             "WHERE id = ? AND switch_group = ? AND id <> ?;",
            [SQL_MYSQL]    = "DELETE FROM user_sessions "
                             "WHERE id = ? AND switch_group = ? AND id <> ?;",
            [SQL_POSTGRES] = "DELETE FROM user_sessions "
                             "WHERE id = $1 AND switch_group = $2 AND id <> $3;",
        };
        sql_stmt st = {0};
        if (sql_prepare(db, q[db->lang], &st)) {
            sql_bind(&st, 1, SQL_SV(targ));
            sql_bind(&st, 2, SQL_SV(sv_from_cstr(group)));
            sql_bind(&st, 3, SQL_SV(sv_from_cstr(my_sid)));
            sql_final_step(&st);
        }
        sql_finalize(&st);
        db_close(db);
        http_render_redirect(sc, 303, "/accounts");
        return;
    }

    // Switch: the target must be a live session in MY group owned by a live
    // user - this blocks hopping to an arbitrary/stolen sid.
    bool ok = false;
    {
        static const char *const q[SQL_LANG_COUNT] = {
            [SQL_SQLITE]   = "SELECT 1 FROM user_sessions s "
                             "JOIN users u ON u.id = s.user_id "
                             "WHERE s.id = ? AND s.switch_group = ? "
                             "AND s.expires_at > strftime('%Y-%m-%dT%H:%M:%fZ','now') "
                             "AND u.deleted_at IS NULL LIMIT 1;",
            [SQL_MYSQL]    = "SELECT 1 FROM user_sessions s "
                             "JOIN users u ON u.id = s.user_id "
                             "WHERE s.id = ? AND s.switch_group = ? "
                             "AND s.expires_at > NOW() AND u.deleted_at IS NULL "
                             "LIMIT 1;",
            [SQL_POSTGRES] = "SELECT 1 FROM user_sessions s "
                             "JOIN users u ON u.id = s.user_id "
                             "WHERE s.id = $1 AND s.switch_group = $2 "
                             "AND s.expires_at > now() AND u.deleted_at IS NULL "
                             "LIMIT 1;",
        };
        sql_stmt st = {0};
        if (sql_prepare(db, q[db->lang], &st)
            && sql_bind(&st, 1, SQL_SV(targ))
            && sql_bind(&st, 2, SQL_SV(sv_from_cstr(group)))
            && sql_step(&st) == SQL_ROW) {
            ok = true;
        }
        sql_finalize(&st);
    }
    db_close(db);

    if (ok) {
        auth_send_session(sc, temp_sprintf("%.*s", (int) targ.count, targ.data));
        http_render_redirect(sc, 303,
                             temp_sprintf("%.*s", (int) next.count, next.data));
    } else {
        http_render_redirect(sc, 303, "/accounts");
    }
}
