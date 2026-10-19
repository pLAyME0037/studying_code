#ifndef POS_SALE_H_
#define POS_SALE_H_

#include "src/http/serve.h"

/* /pos/sale: the dedicated in-store POS sale. The cashier picks products
 * from a single dropdown sourced from product_variants (plus products with
 * no variants) and a quantity per line; the server builds the whole order -
 * staff/org from the signed-in session, customer NULL, status PAID, auto id
 * + order number, delivery fee 0 - and computes every money figure from
 * products.base_price (+ variant price_delta) x quantity. A CASH payment is
 * written for the total and stock is decremented, all in one transaction.
 * The online / e-shop checkout path is untouched. */
void serve_pos_sale(Serve_Context *sc);      // GET  /pos/sale
void serve_pos_sale_post(Serve_Context *sc); // POST /pos/sale

#endif // !POS_SALE_H_
