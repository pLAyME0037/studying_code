#ifndef POS_VARIANTS_H_
#define POS_VARIANTS_H_

#include "core/display/master_child.h"

/* product_variants: child entity of /pos/products (fk = product_id via
 * the child forms' query string). Write routes only -- no master page. */
void serve_pos_variants_create(Serve_Context *sc);
void serve_pos_variants_update(Serve_Context *sc);
void serve_pos_variants_delete(Serve_Context *sc);
void serve_pos_variants_restore(Serve_Context *sc);

#endif // !POS_VARIANTS_H_
