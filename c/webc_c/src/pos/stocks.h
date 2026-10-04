#ifndef POS_STOCKS_H_
#define POS_STOCKS_H_

#include "core/display/master_child.h"

/* inventory_stocks: child entity of /pos/products (fk = product_id via
 * the child forms' query string). Write routes only for now; the master
 * page /pos/stocks + stock_ledger child tab arrive with Phase 5b. */
void serve_pos_stocks_create(Serve_Context *sc);
void serve_pos_stocks_update(Serve_Context *sc);
void serve_pos_stocks_delete(Serve_Context *sc);
void serve_pos_stocks_restore(Serve_Context *sc);

#endif // !POS_STOCKS_H_
