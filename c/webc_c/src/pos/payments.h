#ifndef POS_PAYMENTS_H_
#define POS_PAYMENTS_H_

#include "core/display/master_child.h"

/* payments: child entity of /pos/orders (fk = order_id via the child
 * forms' query string). Write routes only -- no master page. */
void serve_pos_payments_create(Serve_Context *sc);
void serve_pos_payments_update(Serve_Context *sc);
void serve_pos_payments_delete(Serve_Context *sc);
void serve_pos_payments_restore(Serve_Context *sc);

#endif // !POS_PAYMENTS_H_
