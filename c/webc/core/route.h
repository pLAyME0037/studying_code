#ifndef CORE_ROUTE_H_
#define CORE_ROUTE_H_

#include "serve.h"

void route_request(Serve_Context *sc, String_View method, String_View uri);

#endif // CORE_ROUTE_H_
