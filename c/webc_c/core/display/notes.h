#ifndef CORE_NOTES_CONTROLLER_H_
#define CORE_NOTES_CONTROLLER_H_

#include "../http/serve.h"

void serve_notes_read(Serve_Context *sc, String_View method);
void serve_notes_create(Serve_Context *sc);
void serve_notes_edit(Serve_Context *sc, String_View uri);
void serve_notes_update(Serve_Context *sc, String_View uri);
void serve_notes_delete(Serve_Context *sc, String_View uri);
void serve_notes_api(Serve_Context *sc, String_View method);

#endif // CORE_NOTES_CONTROLLER_H_
