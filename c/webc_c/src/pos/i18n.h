#ifndef POS_I18N_H_
#define POS_I18N_H_

#include "core/display/master_child.h"

/* /pos/i18n: languages master (code/name + is_default/is_active flags)
 * with a translations child tab (fk language_id). */
void serve_pos_i18n(Serve_Context *sc);
void serve_pos_i18n_create(Serve_Context *sc);
void serve_pos_i18n_update(Serve_Context *sc);
void serve_pos_i18n_delete(Serve_Context *sc);
void serve_pos_i18n_restore(Serve_Context *sc);

extern MD_Column md_languages_columns[];
extern const size_t md_languages_columns_count;

#endif // !POS_I18N_H_
