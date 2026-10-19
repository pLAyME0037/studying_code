#ifndef POS_FINANCE_H_
#define POS_FINANCE_H_

#include "src/layout/master_child.h"

/* /pos/finance: read_only view of financial_ledgers -- GET route only,
 * no write handlers registered (see route.c). */
void serve_pos_finance(Serve_Context *sc);

#endif // !POS_FINANCE_H_
