#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/http/utils.h"
#include "src/db/db.h"

// Phase 12 language cookie: one year, Path=/, not HttpOnly (nothing secret
// lives in it and the top-bar <select> is a plain GET form anyway).
#define LANG_COOKIE "webc_lang"
#define LANG_COOKIE_MAX_AGE (365 * 24 * 60 * 60)

#define I18N_MAX_ROWS 512   // per language, generous for ~70 seed keys
#define I18N_MAX_LANGS 50   // languages.limit in the list query

typedef struct {
    const char *key;
    const char *val;
} Trans_Row;

typedef struct {
    char id[40];
    char code[16];
    char name[64];
} Lang_Row;

typedef struct {
    Serve_Context *sc;      // request that owns this state (i18n_begin)
    bool           resolved; // cookie + languages table already read
    char           active[16]; // chosen language code ("" = none)
    char           def[16];    // default language code (fallback level)
    Trans_Row      rows[I18N_MAX_ROWS];
    size_t         nrows;
    Trans_Row      drows[I18N_MAX_ROWS]; // default-language rows, if any
    size_t         ndrows;
    Lang_Row       langs[I18N_MAX_LANGS];
    size_t         nlangs;
} I18n_State;

static I18n_State g_i18n = {0};

void i18n_begin(Serve_Context *sc) {
    // Row pointers reference the previous request's temp-arena strings:
    // zero the counts before anything can read them.
    memset(&g_i18n, 0, sizeof(g_i18n));
    g_i18n.sc = sc;
}

// "km"/"en"-shaped cookie value: lowercase letters, digits and dashes only.
static bool lang_code_ok(String_View c) {
    if (c.count < 2 || c.count > 15) return false;
    for (size_t i = 0; i < c.count; ++i) {
        char x = c.data[i];
        bool ok = (x >= 'a' && x <= 'z') || (x >= '0' && x <= '9') || x == '-';
        if (!ok) return false;
    }
    return true;
}

// Load one language's rows. Returns the number stored in `out`.
static size_t load_rows(db_t *db, const char *lang_id, Trans_Row *out) {
    size_t n = 0;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "SELECT trans_key, trans_value FROM translations "
                         "WHERE language_id = ? AND deleted_at IS NULL "
                         "ORDER BY trans_key LIMIT 512;",
        [SQL_MYSQL]    = "SELECT trans_key, trans_value FROM translations "
                         "WHERE language_id = ? AND deleted_at IS NULL "
                         "ORDER BY trans_key LIMIT 512;",
        [SQL_POSTGRES] = "SELECT trans_key, trans_value FROM translations "
                         "WHERE language_id = $1 AND deleted_at IS NULL "
                         "ORDER BY trans_key LIMIT 512;",
    };
    sql_stmt stmt = {0};
    if (sql_prepare(db, q[db->lang], &stmt)
        && sql_bind(&stmt, 1, SQL_SV(sv_from_cstr(lang_id)))) {
        while (sql_step(&stmt) == SQL_ROW && n < I18N_MAX_ROWS) {
            const char *k = sql_col_text(&stmt, 0);
            const char *v = sql_col_text(&stmt, 1);
            if (!k || !k[0] || !v) continue;
            out[n].key = temp_strdup(k);
            out[n].val = temp_strdup(v);
            ++n;
        }
    }
    sql_finalize(&stmt);
    return n;
}

// One-shot: read the cookie, the active language list and both row sets.
// Called at most once per request; a failed open leaves every list empty
// so tr() returns its C literal (mysql without seeded languages).
static void i18n_resolve(void) {
    if (g_i18n.resolved) return;
    g_i18n.resolved = true;
    if (!g_i18n.sc) return;

    char cand[16] = {0};
    String_View cookie = {0};
    if (http_cookie_find(g_i18n.sc, LANG_COOKIE, &cookie)
        && lang_code_ok(cookie)) {
        snprintf(cand, sizeof(cand), "%.*s",
                 (int) cookie.count, cookie.data);
    }

    db_t *db = open_webc_db();
    if (!db) return;

    // Active languages. ORDER BY is_default DESC puts the default first,
    // so langs[0] doubles as the default fallback language.
    {
        static const char *const q[SQL_LANG_COUNT] = {
            [SQL_SQLITE]   = "SELECT id, code, name FROM languages "
                             "WHERE is_active = 1 AND deleted_at IS NULL "
                             "ORDER BY is_default DESC, code LIMIT 50;",
            [SQL_MYSQL]    = "SELECT id, code, name FROM languages "
                             "WHERE is_active = 1 AND deleted_at IS NULL "
                             "ORDER BY is_default DESC, code LIMIT 50;",
            [SQL_POSTGRES] = "SELECT id, code, name FROM languages "
                             "WHERE is_active = 1 AND deleted_at IS NULL "
                             "ORDER BY is_default DESC, code LIMIT 50;",
        };
        sql_stmt stmt = {0};
        if (sql_prepare(db, q[db->lang], &stmt)) {
            while (sql_step(&stmt) == SQL_ROW
                   && g_i18n.nlangs < I18N_MAX_LANGS) {
                const char *id   = sql_col_text(&stmt, 0);
                const char *code = sql_col_text(&stmt, 1);
                const char *name = sql_col_text(&stmt, 2);
                if (!code || !code[0]) continue;
                Lang_Row *L = &g_i18n.langs[g_i18n.nlangs];
                snprintf(L->id, sizeof(L->id), "%s", id ? id : "");
                snprintf(L->code, sizeof(L->code), "%s", code);
                snprintf(L->name, sizeof(L->name), "%s", name ? name : "");
                ++g_i18n.nlangs;
            }
        }
        sql_finalize(&stmt);
    }
    if (g_i18n.nlangs == 0) { db_close(db); return; }

    snprintf(g_i18n.def, sizeof(g_i18n.def), "%s", g_i18n.langs[0].code);
    snprintf(g_i18n.active, sizeof(g_i18n.active), "%s",
             cand[0] ? cand : g_i18n.def);
    bool known = false;
    for (size_t i = 0; i < g_i18n.nlangs; ++i) {
        if (strcmp(g_i18n.langs[i].code, g_i18n.active) == 0) {
            known = true;
            break;
        }
    }
    if (!known) snprintf(g_i18n.active, sizeof(g_i18n.active), "%s",
                         g_i18n.def);

    // Active rows first, then the default language's rows as fallback level
    // (skipped when they are the same language - one SELECT per request).
    for (size_t i = 0; i < g_i18n.nlangs; ++i) {
        if (strcmp(g_i18n.langs[i].code, g_i18n.active) != 0) continue;
        g_i18n.nrows = load_rows(db, g_i18n.langs[i].id, g_i18n.rows);
        break;
    }
    if (strcmp(g_i18n.active, g_i18n.def) != 0) {
        g_i18n.ndrows = load_rows(db, g_i18n.langs[0].id, g_i18n.drows);
    }
    db_close(db);
}

const char *i18n_active_code(void) {
    i18n_resolve();
    return g_i18n.active;
}

const char *i18n_html_lang(void) {
    const char *code = i18n_active_code();
    // No seeded languages (mysql skips 0006): the fallback literals are
    // Khmer, so the document language stays km.
    return code[0] ? code : "km";
}

const char *tr(const char *key, const char *fallback) {
    if (!key) return fallback;
    i18n_resolve();
    for (size_t i = 0; i < g_i18n.nrows; ++i) {
        if (strcmp(g_i18n.rows[i].key, key) != 0) continue;
        if (g_i18n.rows[i].val[0]) return g_i18n.rows[i].val;
        break; // empty value: skip to the default language, then literal
    }
    for (size_t i = 0; i < g_i18n.ndrows; ++i) {
        if (strcmp(g_i18n.drows[i].key, key) != 0) continue;
        if (g_i18n.drows[i].val[0]) return g_i18n.drows[i].val;
        break;
    }
    return fallback;
}

const char *i18n_lang_form_html(const char *back) {
    i18n_resolve();
    if (g_i18n.nlangs < 2) return "";

    String_Builder sb = {0};
    sb_append_cstr(&sb,
        "<form method=\"GET\" action=\"/lang\" data-lang-form"
        " class=\"flex items-center gap-1\">"
        "<input type=\"hidden\" name=\"back\" value=\"");
    if (back && back[0]) sb_append_html_escaped(&sb, back);
    sb_append_cstr(&sb,
        "\">"
        "<select name=\"code\" onchange=\"this.form.submit()\""
        " aria-label=\"Language\" class=\"border border-surface0 bg-mantle"
        " text-text px-1 py-1.5 text-xs\">");
    for (size_t i = 0; i < g_i18n.nlangs; ++i) {
        Lang_Row *L = &g_i18n.langs[i];
        sb_append_cstr(&sb, "<option value=\"");
        sb_append_html_escaped(&sb, L->code);
        if (strcmp(L->code, g_i18n.active) == 0) {
            sb_append_cstr(&sb, "\" selected>");
        } else {
            sb_append_cstr(&sb, "\">");
        }
        sb_append_html_escaped(&sb, L->name[0] ? L->name : L->code);
        sb_append_cstr(&sb, "</option>");
    }
    sb_append_cstr(&sb,
        "</select>"
        // No-JS fallback: the onchange above never fires, so keep a button.
        "<noscript><button type=\"submit\" class=\"border border-surface0"
        " bg-mantle text-text px-1 py-1.5 text-xs\">&#8629;</button>"
        "</noscript></form>");
    sb_append_null(&sb);
    const char *html = temp_strdup(sb.items ? sb.items : "");
    sb_free(sb);
    return html;
}

void serve_lang_set(Serve_Context *sc) {
    String_View code = {0}, back = {0};
    form_find(sc->query_string, "code", &code);
    form_find(sc->query_string, "back", &back);

    // Same-site return path only (mirrors the ?next= open-redirect guard).
    const char *to = "/";
    if (back.count > 1 && back.data[0] == '/'
        && back.data[1] != '/' && back.data[1] != '\\') {
        to = temp_sprintf("%.*s", (int) back.count, back.data);
    }

    i18n_resolve();
    if (lang_code_ok(code)) {
        for (size_t i = 0; i < g_i18n.nlangs; ++i) {
            Lang_Row *L = &g_i18n.langs[i];
            if (strlen(L->code) != code.count
                || memcmp(L->code, code.data, code.count) != 0) {
                continue;
            }
            http_set_cookie(sc, temp_sprintf(
                LANG_COOKIE "=%.*s; Path=/; SameSite=Lax; Max-Age=%d",
                (int) code.count, code.data, LANG_COOKIE_MAX_AGE));
            break;
        }
    }
    http_render_redirect(sc, 303, to);
}
