#ifndef POS_DELIVERIES_H_
#define POS_DELIVERIES_H_

#include "core/display/master_child.h"

/* deliveries: child entity of /pos/orders (fk = order_id via the child
 * forms' query string; order_id is UNIQUE -- one delivery per order).
 * Write routes only -- no master page. */
void serve_pos_deliveries_create(Serve_Context *sc);
void serve_pos_deliveries_update(Serve_Context *sc);
void serve_pos_deliveries_delete(Serve_Context *sc);
void serve_pos_deliveries_restore(Serve_Context *sc);

#endif // !POS_DELIVERIES_H_
