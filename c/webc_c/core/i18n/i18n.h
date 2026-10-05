#ifndef CORE_I18N_H_
#define CORE_I18N_H_

#include "core/http/serve.h"

// Runtime i18n (Phase 12): DB-backed storefront/login strings.
//
// State lives in one static struct bound to the current request:
// route_request() runs to completion without yielding (coroutines only
// interleave on socket I/O, which happens outside it), so exactly one
// request owns the state at a time. i18n_begin() is called at the top of
// route_request(); every accessor below resolves lazily on first use -
// static assets never touch the DB.
//
// Lookup chain for tr(): active language row -> default language row ->
// the caller's C literal. Reads only, no writes on GET.

// Bind the current request (drops the previous request's rows).
void i18n_begin(Serve_Context *sc);

// Active language code ("km"/"en"/...) or "" when the languages table has
// no active rows - the C fallback literals then stand alone (mysql skips
// the 0006 seed).
const char *i18n_active_code(void);

// i18n_active_code() with the "km" fallback baked in, for <html lang>.
const char *i18n_html_lang(void);

// Translation lookup (see chain above).
const char *tr(const char *key, const char *fallback);

// HTML for the language form: hidden back path + one <option> per active
// language + a submit button for the no-JS case. Returns "" while fewer
// than two languages are active. `back` is a same-site path.
const char *i18n_lang_form_html(const char *back);

// GET /lang?code=<lang>&back=<same-site path>: validate `code` against the
// languages table, set the webc_lang cookie and 303 back. Invalid codes
// are ignored (no cookie) - still a redirect.
void serve_lang_set(Serve_Context *sc);

#endif // CORE_I18N_H_
