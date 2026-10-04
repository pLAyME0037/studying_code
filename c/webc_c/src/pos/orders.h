#ifndef POS_ORDERS_H_
#define POS_ORDERS_H_

#include "core/display/master_child.h"

/* /pos/orders: sales master (org/staff/customer/status FKs + amounts
 * cell) with order_items, payments and deliveries child tabs. */
void serve_pos_orders(Serve_Context *sc);
void serve_pos_orders_create(Serve_Context *sc);
void serve_pos_orders_update(Serve_Context *sc);
void serve_pos_orders_delete(Serve_Context *sc);
void serve_pos_orders_restore(Serve_Context *sc);

extern MD_Column md_orders_columns[];
extern const size_t md_orders_columns_count;

#endif // !POS_ORDERS_H_
