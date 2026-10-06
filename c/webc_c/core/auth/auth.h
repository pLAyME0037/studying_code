#ifndef CORE_AUTH_H_
#define CORE_AUTH_H_

#include "core/http/serve.h"

// Staff auth (Phase 11). Sessions back the /login page only: the storefront,
// cart, guest checkout, /login, demo pages and resources stay public.
// auth_gate() runs once per request in route_request(), before routing.

// Identity of whoever is signed in for the request being served (sidebar,
// Phase 14 workspaces). auth_gate() rebinds the state once per request;
// the first auth_current_user() call resolves the webc_sid cookie lazily,
// so public pages that carry a live session show the user too. Returns
// NULL when nobody is signed in (or the account was soft-deleted).
typedef struct {
    const char *id;
    const char *name;
    const char *email;
    const char *profile_pic;
    const char *user_type;   /* dictionary id: ADMIN / CUSTOMER / ... */
    const char *status;      /* ACTIVE / INACTIVE / SUSPENDED */
    const char *role;        /* first live role_name (Khmer), "" when none */
    const char *role_code;   /* SD-MANAGER / SD-CASHIER / SD-DRIVER, "" none */
    const char *staff_id;    /* linked staff row id, "" when none */
    const char *perms;       /* ",SD.SELL,SD.REPORT.VIEW," joined grants,
                                NULL/"" when the role holds none */
} Auth_User;

const Auth_User *auth_current_user(void);

/* Phase 14 RBAC: true when the signed-in role holds `code`
 * ("SD.SELL", ...). Anonymous visitors hold nothing. */
bool auth_has_perm(const char *code);

/* Phase 14 page gate:403s a guarded URI whose required permission is
 * missing from the signed-in role (returns true after writing the
 * response - route_request then stops). Runs right after auth_gate();
 * /dashboard itself is not perm-gated - it role-dispatches instead. */
bool perm_gate(Serve_Context *sc);

// True when this URI needs a signed-in ADMIN session and the request had
// none - the request has then been answered with 303 /login?next=...
// and the caller must stop routing.
bool auth_gate(Serve_Context *sc);

void serve_auth_login(Serve_Context *sc);       // GET  /login
void serve_auth_login_post(Serve_Context *sc);  // POST /login
void serve_auth_logout(Serve_Context *sc);      // GET/POST /logout

#endif  // CORE_AUTH_H_
