#ifndef CORE_DISPLAY_DB_H_
#define CORE_DISPLAY_DB_H_

#include "core/http/serve.h"

// DB Admin pages: overview of the active database + the open/switch panel
// (Phase 1 of the SQLite management UI).
void serve_db_page(Serve_Context *sc);    // GET  /db
void serve_db_file(Serve_Context *sc);    // GET  /db/file (active SQLite bytes)
void serve_db_save(Serve_Context *sc);    // POST /db/save (save-back bytes)
void serve_db_browse(Serve_Context *sc);  // GET  /db/browse?dir=... (JSON)
void serve_db_open(Serve_Context *sc);    // POST /db/open   (open | create)
void serve_db_close(Serve_Context *sc);   // POST /db/close

#endif  // CORE_DISPLAY_DB_H_
