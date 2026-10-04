#ifndef POS_LEDGER_H_
#define POS_LEDGER_H_

#include "core/display/master_child.h"

/* stock_ledger: child entity of /pos/stocks (fk = stock_id via the child
 * forms' query string). Write routes only -- no master page. */
void serve_pos_ledger_create(Serve_Context *sc);
void serve_pos_ledger_update(Serve_Context *sc);
void serve_pos_ledger_delete(Serve_Context *sc);
void serve_pos_ledger_restore(Serve_Context *sc);

#endif // !POS_LEDGER_H_
