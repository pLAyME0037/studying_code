#ifndef POS_ORDER_ITEMS_H_
#define POS_ORDER_ITEMS_H_

#include "core/display/master_child.h"

/* order_items: child entity of /pos/orders (fk = order_id via the child
 * forms' query string). Write routes only -- no master page. */
void serve_pos_order_items_create(Serve_Context *sc);
void serve_pos_order_items_update(Serve_Context *sc);
void serve_pos_order_items_delete(Serve_Context *sc);
void serve_pos_order_items_restore(Serve_Context *sc);

#endif // !POS_ORDER_ITEMS_H_
