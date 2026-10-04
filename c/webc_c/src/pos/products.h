#ifndef POS_PRODUCTS_H_
#define POS_PRODUCTS_H_

#include "core/display/master_child.h"

/* /pos/products: catalog master with product_variants + inventory_stocks
 * child tabs; also rendered as a child tab under /pos/categories
 * (md_products_child_columns, fk = category_id). */
void serve_pos_products(Serve_Context *sc);
void serve_pos_products_create(Serve_Context *sc);
void serve_pos_products_update(Serve_Context *sc);
void serve_pos_products_delete(Serve_Context *sc);
void serve_pos_products_restore(Serve_Context *sc);

extern MD_Column md_products_columns[];
extern const size_t md_products_columns_count;
extern MD_Column md_products_child_columns[];
extern const size_t md_products_child_columns_count;

#endif // !POS_PRODUCTS_H_
