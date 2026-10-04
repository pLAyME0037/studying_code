#ifndef POS_CATEGORIES_H_
#define POS_CATEGORIES_H_

#include "core/display/master_child.h"

/* /pos/categories: catalog master (self-FK parent select); its child tab
 * is products (fk = category_id, handlers in src/pos/products.c). */
void serve_pos_categories(Serve_Context *sc);
void serve_pos_categories_create(Serve_Context *sc);
void serve_pos_categories_update(Serve_Context *sc);
void serve_pos_categories_delete(Serve_Context *sc);
void serve_pos_categories_restore(Serve_Context *sc);

extern MD_Column md_categories_columns[];
extern const size_t md_categories_columns_count;

#endif // !POS_CATEGORIES_H_
