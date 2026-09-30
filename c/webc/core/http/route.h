#ifndef CORE_ROUTE_H_
#define CORE_ROUTE_H_

#include "serve.h"

typedef void (*route_handler)(Serve_Context *sc);

typedef enum {
    ROUTE_EXACT,
    ROUTE_PREFIX,
    ROUTE_ID_ACTION,
} route_kind;

typedef struct {
    const char   *prefix;
    const char   *suffix;
    const char   *method;
    route_kind    kind;
    route_handler handle;
} route_t;

DA_NEW(route_t, route_da)

void route_request(Serve_Context *sc);

#endif // CORE_ROUTE_H_
