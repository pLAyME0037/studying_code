#ifndef POS_CUSTOMERS_H_
#define POS_CUSTOMERS_H_

#include "core/display/master_child.h"

/* /pos/customers: tier FK + loyalty points + created_at, with users and
 * customer_interactions child tabs (users shape comes from
 * core/display/pos.h -- the users showcase owns its own columns). */
void serve_pos_customers(Serve_Context *sc);
void serve_pos_customers_create(Serve_Context *sc);
void serve_pos_customers_update(Serve_Context *sc);
void serve_pos_customers_delete(Serve_Context *sc);
void serve_pos_customers_restore(Serve_Context *sc);

extern MD_Column md_customers_columns[];
extern const size_t md_customers_columns_count;

#endif // !POS_CUSTOMERS_H_
