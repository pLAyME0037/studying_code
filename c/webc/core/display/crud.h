#ifndef CORE_CRUD_H_
#define CORE_CRUD_H_

#include "../http/serve.h"
#include "../../src/crud/crud.h"

void serve_crud_list(Serve_Context *sc, const Crud_Module *mod);
void serve_crud_create(Serve_Context *sc, const Crud_Module *mod);
void serve_crud_edit(Serve_Context *sc, const Crud_Module *mod, int id);
void serve_crud_update(Serve_Context *sc, const Crud_Module *mod, int id);
void serve_crud_delete(Serve_Context *sc, const Crud_Module *mod, int id);

#endif // CORE_CRUD_H_
