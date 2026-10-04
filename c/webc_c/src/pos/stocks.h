#ifndef POS_STOCKS_H_
#define POS_STOCKS_H_

#include "core/display/master_child.h"

/* inventory_stocks: /pos/stocks master (product/org/variant FKs + qty +
 * range cell) with a stock_ledger child tab. Also created as a child of
 * /pos/products (fk = product_id via the child forms' query string). */
void serve_pos_stocks(Serve_Context *sc);
void serve_pos_stocks_create(Serve_Context *sc);
void serve_pos_stocks_update(Serve_Context *sc);
void serve_pos_stocks_delete(Serve_Context *sc);
void serve_pos_stocks_restore(Serve_Context *sc);

#endif // !POS_STOCKS_H_
