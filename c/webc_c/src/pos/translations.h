#ifndef POS_TRANSLATIONS_H_
#define POS_TRANSLATIONS_H_

#include "core/display/master_child.h"

/* translations: child entity of /pos/i18n (fk = language_id via the
 * child forms' query string). UNIQUE(language_id, trans_key).
 * Write routes only -- no master page. */
void serve_pos_translations_create(Serve_Context *sc);
void serve_pos_translations_update(Serve_Context *sc);
void serve_pos_translations_delete(Serve_Context *sc);
void serve_pos_translations_restore(Serve_Context *sc);

#endif // !POS_TRANSLATIONS_H_
