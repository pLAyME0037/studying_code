#ifndef POS_CUSTOMER_INTERACTIONS_H_
#define POS_CUSTOMER_INTERACTIONS_H_

#include "core/display/master_child.h"

/* customer_interactions: child entity of /pos/customers (fk = customer_id
 * via the child forms' query string). Write routes only. */
void serve_pos_customer_interactions_create(Serve_Context *sc);
void serve_pos_customer_interactions_update(Serve_Context *sc);
void serve_pos_customer_interactions_delete(Serve_Context *sc);
void serve_pos_customer_interactions_restore(Serve_Context *sc);

#endif // !POS_CUSTOMER_INTERACTIONS_H_
