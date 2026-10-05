#ifndef SRC_SHOP_H_
#define SRC_SHOP_H_

#include "core/http/serve.h"

// Storefront (Phase 11): public catalog at /, cookie-backed cart, guest
// checkout. Orders enter the system only through serve_shop_checkout_post
// (server-computed prices/totals) or behind auth_gate() at /pos for staff -
// nothing else inserts orders.

void serve_shop_index(Serve_Context *sc);         // GET  /
void serve_shop_product(Serve_Context *sc);       // GET  /product/<id>
void serve_shop_cart(Serve_Context *sc);          // GET  /cart
void serve_shop_cart_add(Serve_Context *sc);      // POST /cart/add
void serve_shop_cart_buynow(Serve_Context *sc);   // POST /cart/buynow
void serve_shop_cart_update(Serve_Context *sc);   // POST /cart/update
void serve_shop_cart_remove(Serve_Context *sc);   // POST /cart/remove
void serve_shop_checkout(Serve_Context *sc);      // GET  /checkout
void serve_shop_checkout_post(Serve_Context *sc); // POST /checkout
void serve_shop_order(Serve_Context *sc);         // GET  /order/<id>

// -- shared page chrome (public layout - no admin sidebar) ----------------
void shop_render(Serve_Context *sc, int status, const char *title,
                 String_Builder *content);
void shop_chrome_start(String_Builder *sb, Serve_Context *sc,
                       const char *active);   // <body> + header nav
void shop_chrome_end(String_Builder *sb);     // footer + closing tags
const char *shop_money(double v);             // "123.45" as temp string

// -- cart ------------------------------------------------------------------
// Cookie format: "id:qty,id:qty,..." (url-safe, HttpOnly). Every read
// validates ids against the DB; unknown/deleted products render as skipped
// lines. Subtotal is summed from live base_price values.
#define CART_COOKIE "webc_cart"
#define CART_MAX 40

typedef struct {
    char   id[40];
    int    qty;
    bool   valid;      // product exists and is not deleted
    char   name[160];
    char   sku[64];
    double price;      // products.base_price
    double cost;       // products.cost_price (checkout only)
    double stock;      // SUM(inventory_stocks.quantity) for the product
} Shop_Cart_Item;

typedef struct {
    Shop_Cart_Item items[CART_MAX];
    size_t count;
    double subtotal;   // filled by shop_cart_load()
} Shop_Cart;

void shop_cart_load(Serve_Context *sc, Shop_Cart *cart);
void shop_cart_save(Serve_Context *sc, const Shop_Cart *cart); // queues Set-Cookie

#endif  // SRC_SHOP_H_
