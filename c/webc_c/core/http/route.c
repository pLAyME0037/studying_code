#include <stdbool.h>
#include <stdio.h>

#include "route.h"

#include "core/display/notes.h"
#include "core/display/version.h"
#include "core/display/people.h"
#include "core/display/user.h"
#include "core/display/pos.h"
#include "src/pos/categories.h"
#include "src/pos/products.h"
#include "src/pos/variants.h"
#include "src/pos/stocks.h"
#include "src/pos/ledger.h"
#include "src/pos/orders.h"
#include "src/pos/shifts.h"
#include "src/pos/finance.h"
#include "src/pos/order_items.h"
#include "src/pos/payments.h"
#include "src/pos/deliveries.h"
#include "src/pos/customers.h"
#include "src/pos/staff.h"
#include "src/pos/org.h"
#include "src/pos/customer_interactions.h"
#include "src/pos/user_roles.h"
#include "src/pos/roles.h"
#include "src/pos/permissions.h"
#include "src/pos/role_permissions.h"
#include "src/pos/i18n.h"
#include "src/pos/translations.h"
#include "src/pos/config.h"
#include "src/pos/alerts.h"
#include "src/pos/audit.h"
#include "core/report/report.h"
#include "core/auth/auth.h"
#include "core/i18n/i18n.h"
#include "src/shop/shop.h"
#include "serve.h"

// Extract + classify the id segment from "/<path>/<id>/<suffix>".
// Ids are TEXT primary keys (legacy numeric strings or schema uuid defaults);
// route_id_parse() tags all-digits segments as ID_INT for INTEGER tables.
static bool parse_uri_id(String_View path,
                         String_View uri,
                         const char *suffix,
                         Route_Id   *id)
{
    char prefix_buf[128];
    snprintf(prefix_buf, sizeof(prefix_buf), "%.*s/", (int)path.count, path.data);
    String_View prefix = sv_from_cstr(prefix_buf);
    if (uri.count <= prefix.count
        || memcmp(uri.data, prefix.data, prefix.count) != 0) {
        return false;
    }
    String_View suffix_sv;
    suffix_sv = sv_from_cstr(suffix);
    if (uri.count <= suffix_sv.count) return false;
    if (memcmp(uri.data + uri.count - suffix_sv.count, suffix_sv.data, suffix_sv.count) != 0) {
        return false;
    }
    if (uri.count <= prefix.count + suffix_sv.count) return false;

    String_View id_sv = {
        .data  = uri.data + prefix.count,
        .count = uri.count - prefix.count - suffix_sv.count,
    };
    return route_id_parse(id_sv, id);
}

// Reject ".." path segments anywhere in the URI (path traversal).
static bool uri_has_dotdot(String_View uri) {
    for (size_t i = 0; i + 1 < uri.count; ++i) {
        if (uri.data[i] != '.' || uri.data[i + 1] != '.') continue;
        bool seg_start = i == 0 || uri.data[i - 1] == '/';
        bool seg_end = i + 2 == uri.count || uri.data[i + 2] == '/';
        if (seg_start && seg_end) return true;
    }
    return false;
}

void serve_resource_route(Serve_Context *sc) {
    if (uri_has_dotdot(sc->uri)) {
        serve_error(sc, 404);
        return;
    }
    if (sv_starts_with(sc->uri, sv_from_cstr("/js/"))) {
        String_View rest = {
            .data  = sc->uri.data + 4,
            .count = sc->uri.count - 4
        };
        String_Builder path = {0};
        sb_append_cstr(&path, "./js/");
        sb_append_sv(&path, rest);
        sb_append_null(&path);
        // determine content_type (text/javascript for .js)
        serve_resource(sc, path.items, "text/javascript; charset=utf-8");
        sb_free(path);
        return;
    }
    if (sv_eq(sc->uri, sv_from_cstr("/css/output.css"))) {
        serve_resource(sc, "./css/output.css", "text/css; charset=utf-8");
        return;
    }
    if (sv_eq(sc->uri, sv_from_cstr("/favicon.ico"))) {
        serve_resource(sc, "./resource/image/user1.png", "image/png");
        return;
    }
    if (sv_starts_with(sc->uri, sv_from_cstr("/resource/"))) {
        String_View resource_prefix = sv_from_cstr("/resource/");
        String_View rest = {
            .data  = sc->uri.data + resource_prefix.count,
            .count = sc->uri.count - resource_prefix.count,
        };
        String_Builder path = {0};
        sb_append_cstr(&path, "./resource/");
        sb_append_sv(&path, rest);
        sb_append_null(&path);

        int type_id = 0;
        if (sv_ends_with(rest, sv_from_cstr(".png")))  type_id = 1;
        if (sv_ends_with(rest, sv_from_cstr(".jpg")))  type_id = 2;
        if (sv_ends_with(rest, sv_from_cstr(".svg")))  type_id = 3;
        if (sv_ends_with(rest, sv_from_cstr(".css")))  type_id = 4;
        if (sv_ends_with(rest, sv_from_cstr(".js")))   type_id = 5;
        if (sv_ends_with(rest, sv_from_cstr(".html"))) type_id = 6;
        if (sv_ends_with(rest, sv_from_cstr(".webp"))) type_id = 7;
        if (sv_ends_with(rest, sv_from_cstr(".gif")))  type_id = 8;
        const char *content_type;
        switch (type_id) {
        case 1: content_type = "image/png";                      break;
        case 2: content_type = "image/jpeg";                     break;
        case 3: content_type = "image/svg+xml";                  break;
        case 4: content_type = "text/css; charset=utf-8";        break;
        case 5: content_type = "text/javascript; charset=utf-8"; break;
        case 6: content_type = "text/html; charset=utf-8";       break;
        case 7: content_type = "image/webp";                     break;
        case 8: content_type = "image/gif";                      break;
        default: content_type = "application/octet-stream";      break;
        }
        serve_resource(sc, path.items, content_type);
        sb_free(path);
        return;
    }
    serve_error(sc, 404);
}

void serve_dashboard(Serve_Context *sc);

#define CMP_URI(dst_uri, src_uri)   sv_eq((dst_uri), (sv_from_cstr(src_uri)))
#define START_URI(dst_uri, src_uri) sv_starts_with((dst_uri), (sv_from_cstr(src_uri)))

static void route_new(route_da     *routes,
                      const char   *prefix,
                      const char   *suffix,
                      const char   *method,
                      route_kind    kind,
                      route_handler handle)
{
    route_da_add(routes, (route_t) {
        .prefix = prefix,
        .suffix = suffix,
        .method = method,
        .kind   = kind,
        .handle = handle,
    });
}

static route_da routes = {0};
static bool route_init = false;

void route_initialize(void) {
    // Phase 11: / is the storefront, the admin dashboard moved to /dashboard
    // (auth_gate() keeps /pos, /dashboard and /reports behind staff login).
    route_new(&routes, "/",            NULL, "GET", ROUTE_EXACT, serve_shop_index);
    route_new(&routes, "/dashboard",   NULL, "GET", ROUTE_EXACT, serve_dashboard);
    route_new(&routes, "/product",     "",   "GET", ROUTE_ID_ACTION, serve_shop_product);
    route_new(&routes, "/cart",        NULL, "GET", ROUTE_EXACT, serve_shop_cart);
    route_new(&routes, "/cart/add",    NULL, "POST", ROUTE_EXACT, serve_shop_cart_add);
    route_new(&routes, "/cart/buynow", NULL, "POST", ROUTE_EXACT, serve_shop_cart_buynow);
    route_new(&routes, "/cart/update", NULL, "POST", ROUTE_EXACT, serve_shop_cart_update);
    route_new(&routes, "/cart/remove", NULL, "POST", ROUTE_EXACT, serve_shop_cart_remove);
    route_new(&routes, "/checkout",    NULL, "GET", ROUTE_EXACT, serve_shop_checkout);
    route_new(&routes, "/checkout",    NULL, "POST", ROUTE_EXACT, serve_shop_checkout_post);
    route_new(&routes, "/order",       "",   "GET", ROUTE_ID_ACTION, serve_shop_order);
    route_new(&routes, "/login",       NULL, "GET", ROUTE_EXACT, serve_auth_login);
    route_new(&routes, "/login",       NULL, "POST", ROUTE_EXACT, serve_auth_login_post);
    route_new(&routes, "/logout",      NULL, "GET", ROUTE_EXACT, serve_auth_logout);
    route_new(&routes, "/logout",      NULL, "POST", ROUTE_EXACT, serve_auth_logout);
    // Phase 12: language switch - validate the code, set webc_lang, 303 back.
    route_new(&routes, "/lang", NULL, "GET", ROUTE_EXACT, serve_lang_set);
    route_new(&routes, "/version", NULL, "GET", ROUTE_EXACT, serve_version_page);
    // users master + their notes, one master-detail page
    route_new(&routes, "/people", NULL, "GET", ROUTE_EXACT, serve_people);

    // method NULL: serve_notes_api dispatches GET/POST/PUT/DELETE itself
    // and answers unsupported methods with 405.
    route_new(&routes, "/api/notes", NULL,  NULL,   ROUTE_EXACT,     serve_notes_api);
    route_new(&routes, "/notes", NULL,      "GET",  ROUTE_EXACT,     serve_notes_read);
    route_new(&routes, "/notes/create", NULL, "POST", ROUTE_EXACT,   serve_notes_create);
    route_new(&routes, "/notes", "/edit",   "GET",  ROUTE_ID_ACTION, serve_notes_edit);
    route_new(&routes, "/notes", "/update", "POST", ROUTE_ID_ACTION, serve_notes_update);
    route_new(&routes, "/notes", "/delete", "POST", ROUTE_ID_ACTION, serve_notes_delete);

    route_new(&routes, "/users", NULL,      "GET",  ROUTE_EXACT,     serve_users_read);
    route_new(&routes, "/users/create", NULL, "POST", ROUTE_EXACT,   serve_users_create);
    route_new(&routes, "/users", "/edit",   "GET",  ROUTE_ID_ACTION, serve_users_edit);
    route_new(&routes, "/users", "/update", "POST", ROUTE_ID_ACTION, serve_users_update);
    route_new(&routes, "/users", "/delete", "POST", ROUTE_ID_ACTION, serve_users_delete);

    // POS showcase pages (composite multi-field columns), same route shape
    route_new(&routes, "/pos/locations", NULL,       "GET",  ROUTE_EXACT,     serve_pos_locations);
    route_new(&routes, "/pos/locations/create", NULL, "POST", ROUTE_EXACT,    serve_pos_locations_create);
    route_new(&routes, "/pos/locations", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_locations_update);
    route_new(&routes, "/pos/locations", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_locations_delete);
    route_new(&routes, "/pos/locations", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_locations_restore);

    route_new(&routes, "/pos/users", NULL,       "GET",  ROUTE_EXACT,     serve_pos_users);
    route_new(&routes, "/pos/users/create", NULL, "POST", ROUTE_EXACT,    serve_pos_users_create);
    route_new(&routes, "/pos/users", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_users_update);
    route_new(&routes, "/pos/users", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_users_delete);
    route_new(&routes, "/pos/users", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_users_restore);

    // read_only showcase: view only
    route_new(&routes, "/pos/dictionaries", NULL, "GET", ROUTE_EXACT, serve_pos_dictionaries);

    // Phase 5a CATALOG
    route_new(&routes, "/pos/categories", NULL,       "GET",  ROUTE_EXACT,     serve_pos_categories);
    route_new(&routes, "/pos/categories/create", NULL, "POST", ROUTE_EXACT,    serve_pos_categories_create);
    route_new(&routes, "/pos/categories", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_categories_update);
    route_new(&routes, "/pos/categories", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_categories_delete);
    route_new(&routes, "/pos/categories", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_categories_restore);

    route_new(&routes, "/pos/products", NULL,       "GET",  ROUTE_EXACT,     serve_pos_products);
    route_new(&routes, "/pos/products/create", NULL, "POST", ROUTE_EXACT,    serve_pos_products_create);
    route_new(&routes, "/pos/products", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_products_update);
    route_new(&routes, "/pos/products", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_products_delete);
    route_new(&routes, "/pos/products", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_products_restore);

    // child entities: write routes only (child forms carry fk + redirect)
    route_new(&routes, "/pos/variants/create", NULL, "POST", ROUTE_EXACT,    serve_pos_variants_create);
    route_new(&routes, "/pos/variants", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_variants_update);
    route_new(&routes, "/pos/variants", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_variants_delete);
    route_new(&routes, "/pos/variants", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_variants_restore);

    route_new(&routes, "/pos/stocks", NULL,       "GET",  ROUTE_EXACT,     serve_pos_stocks);
    route_new(&routes, "/pos/stocks/create", NULL, "POST", ROUTE_EXACT,    serve_pos_stocks_create);
    route_new(&routes, "/pos/stocks", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_stocks_update);
    route_new(&routes, "/pos/stocks", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_stocks_delete);
    route_new(&routes, "/pos/stocks", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_stocks_restore);

    route_new(&routes, "/pos/ledger/create", NULL, "POST", ROUTE_EXACT,    serve_pos_ledger_create);
    route_new(&routes, "/pos/ledger", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_ledger_update);
    route_new(&routes, "/pos/ledger", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_ledger_delete);
    route_new(&routes, "/pos/ledger", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_ledger_restore);

    // Phase 5c SALES
    route_new(&routes, "/pos/orders", NULL,       "GET",  ROUTE_EXACT,     serve_pos_orders);
    route_new(&routes, "/pos/orders/create", NULL, "POST", ROUTE_EXACT,    serve_pos_orders_create);
    route_new(&routes, "/pos/orders", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_orders_update);
    route_new(&routes, "/pos/orders", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_orders_delete);
    route_new(&routes, "/pos/orders", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_orders_restore);

    route_new(&routes, "/pos/shifts", NULL,       "GET",  ROUTE_EXACT,     serve_pos_shifts);
    route_new(&routes, "/pos/shifts/create", NULL, "POST", ROUTE_EXACT,    serve_pos_shifts_create);
    route_new(&routes, "/pos/shifts", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_shifts_update);
    route_new(&routes, "/pos/shifts", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_shifts_delete);
    route_new(&routes, "/pos/shifts", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_shifts_restore);

    // read_only view: GET only
    route_new(&routes, "/pos/finance", NULL, "GET", ROUTE_EXACT, serve_pos_finance);

    // order children: write routes only
    route_new(&routes, "/pos/order_items/create", NULL, "POST", ROUTE_EXACT,    serve_pos_order_items_create);
    route_new(&routes, "/pos/order_items", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_order_items_update);
    route_new(&routes, "/pos/order_items", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_order_items_delete);
    route_new(&routes, "/pos/order_items", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_order_items_restore);

    route_new(&routes, "/pos/payments/create", NULL, "POST", ROUTE_EXACT,    serve_pos_payments_create);
    route_new(&routes, "/pos/payments", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_payments_update);
    route_new(&routes, "/pos/payments", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_payments_delete);
    route_new(&routes, "/pos/payments", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_payments_restore);

    route_new(&routes, "/pos/deliveries/create", NULL, "POST", ROUTE_EXACT,    serve_pos_deliveries_create);
    route_new(&routes, "/pos/deliveries", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_deliveries_update);
    route_new(&routes, "/pos/deliveries", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_deliveries_delete);
    route_new(&routes, "/pos/deliveries", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_deliveries_restore);

    // Phase 5d PARTY
    route_new(&routes, "/pos/customers", NULL,       "GET",  ROUTE_EXACT,     serve_pos_customers);
    route_new(&routes, "/pos/customers/create", NULL, "POST", ROUTE_EXACT,    serve_pos_customers_create);
    route_new(&routes, "/pos/customers", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_customers_update);
    route_new(&routes, "/pos/customers", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_customers_delete);
    route_new(&routes, "/pos/customers", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_customers_restore);

    route_new(&routes, "/pos/staff", NULL,       "GET",  ROUTE_EXACT,     serve_pos_staff);
    route_new(&routes, "/pos/staff/create", NULL, "POST", ROUTE_EXACT,    serve_pos_staff_create);
    route_new(&routes, "/pos/staff", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_staff_update);
    route_new(&routes, "/pos/staff", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_staff_delete);
    route_new(&routes, "/pos/staff", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_staff_restore);

    route_new(&routes, "/pos/org", NULL,       "GET",  ROUTE_EXACT,     serve_pos_org);
    route_new(&routes, "/pos/org/create", NULL, "POST", ROUTE_EXACT,    serve_pos_org_create);
    route_new(&routes, "/pos/org", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_org_update);
    route_new(&routes, "/pos/org", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_org_delete);
    route_new(&routes, "/pos/org", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_org_restore);

    route_new(&routes, "/pos/customer_interactions/create", NULL, "POST", ROUTE_EXACT,    serve_pos_customer_interactions_create);
    route_new(&routes, "/pos/customer_interactions", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_customer_interactions_update);
    route_new(&routes, "/pos/customer_interactions", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_customer_interactions_delete);
    route_new(&routes, "/pos/customer_interactions", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_customer_interactions_restore);

    route_new(&routes, "/pos/user_roles/create", NULL, "POST", ROUTE_EXACT,    serve_pos_user_roles_create);
    route_new(&routes, "/pos/user_roles", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_user_roles_update);
    route_new(&routes, "/pos/user_roles", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_user_roles_delete);
    route_new(&routes, "/pos/user_roles", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_user_roles_restore);

    // Phase 5e ACCESS
    route_new(&routes, "/pos/roles", NULL,       "GET",  ROUTE_EXACT,     serve_pos_roles);
    route_new(&routes, "/pos/roles/create", NULL, "POST", ROUTE_EXACT,    serve_pos_roles_create);
    route_new(&routes, "/pos/roles", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_roles_update);
    route_new(&routes, "/pos/roles", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_roles_delete);
    route_new(&routes, "/pos/roles", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_roles_restore);

    route_new(&routes, "/pos/permissions", NULL,       "GET",  ROUTE_EXACT,     serve_pos_permissions);
    route_new(&routes, "/pos/permissions/create", NULL, "POST", ROUTE_EXACT,    serve_pos_permissions_create);
    route_new(&routes, "/pos/permissions", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_permissions_update);
    route_new(&routes, "/pos/permissions", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_permissions_delete);
    route_new(&routes, "/pos/permissions", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_permissions_restore);

    route_new(&routes, "/pos/role_permissions/create", NULL, "POST", ROUTE_EXACT,    serve_pos_role_permissions_create);
    route_new(&routes, "/pos/role_permissions", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_role_permissions_update);
    route_new(&routes, "/pos/role_permissions", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_role_permissions_delete);
    route_new(&routes, "/pos/role_permissions", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_role_permissions_restore);

    route_new(&routes, "/pos/role_members/create", NULL, "POST", ROUTE_EXACT,    serve_pos_role_members_create);
    route_new(&routes, "/pos/role_members", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_role_members_update);
    route_new(&routes, "/pos/role_members", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_role_members_delete);
    route_new(&routes, "/pos/role_members", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_role_members_restore);

    // Phase 5f CONFIG
    route_new(&routes, "/pos/i18n", NULL,       "GET",  ROUTE_EXACT,     serve_pos_i18n);
    route_new(&routes, "/pos/i18n/create", NULL, "POST", ROUTE_EXACT,    serve_pos_i18n_create);
    route_new(&routes, "/pos/i18n", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_i18n_update);
    route_new(&routes, "/pos/i18n", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_i18n_delete);
    route_new(&routes, "/pos/i18n", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_i18n_restore);

    route_new(&routes, "/pos/config", NULL,       "GET",  ROUTE_EXACT,     serve_pos_config);
    route_new(&routes, "/pos/config/create", NULL, "POST", ROUTE_EXACT,    serve_pos_config_create);
    route_new(&routes, "/pos/config", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_config_update);
    route_new(&routes, "/pos/config", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_config_delete);
    route_new(&routes, "/pos/config", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_config_restore);

    route_new(&routes, "/pos/translations/create", NULL, "POST", ROUTE_EXACT,    serve_pos_translations_create);
    route_new(&routes, "/pos/translations", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_translations_update);
    route_new(&routes, "/pos/translations", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_translations_delete);
    route_new(&routes, "/pos/translations", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_translations_restore);

    // Phase 5g MONITOR
    route_new(&routes, "/pos/alerts", NULL,       "GET",  ROUTE_EXACT,     serve_pos_alerts);
    route_new(&routes, "/pos/alerts/create", NULL, "POST", ROUTE_EXACT,    serve_pos_alerts_create);
    route_new(&routes, "/pos/alerts", "/update",  "POST", ROUTE_ID_ACTION, serve_pos_alerts_update);
    route_new(&routes, "/pos/alerts", "/delete",  "POST", ROUTE_ID_ACTION, serve_pos_alerts_delete);
    route_new(&routes, "/pos/alerts", "/restore", "POST", ROUTE_ID_ACTION, serve_pos_alerts_restore);

    // read_only view: GET only
    route_new(&routes, "/pos/audit", NULL, "GET", ROUTE_EXACT, serve_pos_audit);

    // Phase 8 reports: index first (ROUTE_EXACT only matches the bare
    // path), then the prefix grabber parses /reports/<id>.<ext> itself.
    route_new(&routes, "/reports",  NULL, "GET", ROUTE_EXACT,  serve_reports_index);
    route_new(&routes, "/reports/", NULL, "GET", ROUTE_PREFIX, serve_report_download);

    route_new(&routes, "/css/",        NULL, NULL, ROUTE_PREFIX, serve_resource_route);
    route_new(&routes, "/js/",         NULL, NULL, ROUTE_PREFIX, serve_resource_route);
    route_new(&routes, "/resource/",   NULL, NULL, ROUTE_PREFIX, serve_resource_route);
    route_new(&routes, "/favicon.ico", NULL, NULL, ROUTE_EXACT,  serve_resource_route);

    route_init = true;
}

void route_request(Serve_Context *sc) {
    if (!route_init) {
        route_initialize();
    }

    // Phase 12: request-scoped i18n state (tr() and <html lang> read it).
    // Must run before auth_gate() - the login page translates too.
    i18n_begin(sc);

    // Staff gate: /pos, /dashboard and /reports redirect to /login with a
    // validated ?next= when no live session cookie is present.
    if (auth_gate(sc)) return;

    // Phase 14: page-level RBAC - a signed-in role missing the required
    // permission gets the styled 403 instead of the page (/dashboard skips
    // this gate and role-dispatches its own workspace).
    if (perm_gate(sc)) return;

    for (size_t i = 0; i < routes.count; ++i) {
        const route_t *r = &routes.items[i];

        if (r->method && !CMP_URI(sc->method, r->method)) continue;
        switch (r->kind) {
        case ROUTE_EXACT:
            if (CMP_URI(sc->uri, r->prefix)) {
                r->handle(sc);
                return;
            }
        break;
        case ROUTE_PREFIX:
            if (START_URI(sc->uri, r->prefix)) {
                r->handle(sc);
                return;
            }
        break;
        case ROUTE_ID_ACTION: {
            Route_Id id = {0};
            if (parse_uri_id(sv_from_cstr(r->prefix), sc->uri, r->suffix, &id)) {
                sc->route_id = id;
                r->handle(sc);
                return;
            }
        }
        break;
        }
    }

    serve_error(sc, 404);
}
