#ifndef CORE_USER_H_
#define CORE_USER_H_

#include "../http/serve.h"

void serve_users(Serve_Context *sc, String_View method);
void serve_users_create(Serve_Context *sc);
void serve_users_edit(Serve_Context *sc, String_View uri);
void serve_users_update(Serve_Context *sc, String_View uri);
void serve_users_delete(Serve_Context *sc, String_View uri);

#endif // CORE_USER_H_
