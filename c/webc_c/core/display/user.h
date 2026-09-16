#ifndef CORE_USER_H_
#define CORE_USER_H_

#include "../http/serve.h"

void serve_users_read(Serve_Context *sc);
void serve_users_create(Serve_Context *sc);
void serve_users_edit(Serve_Context *sc);
void serve_users_update(Serve_Context *sc);
void serve_users_delete(Serve_Context *sc);

#endif // CORE_USER_H_
