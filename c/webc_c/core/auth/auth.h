#ifndef CORE_AUTH_H_
#define CORE_AUTH_H_

#include "core/http/serve.h"

// Staff auth (Phase 11). Sessions back the /login page only: the storefront,
// cart, guest checkout, /login, demo pages and resources stay public.
// auth_gate() runs once per request in route_request(), before routing.

// True when this URI needs a signed-in ADMIN session and the request had
// none - the request has then been answered with 303 /login?next=...
// and the caller must stop routing.
bool auth_gate(Serve_Context *sc);

void serve_auth_login(Serve_Context *sc);       // GET  /login
void serve_auth_login_post(Serve_Context *sc);  // POST /login
void serve_auth_logout(Serve_Context *sc);      // GET/POST /logout

#endif  // CORE_AUTH_H_
