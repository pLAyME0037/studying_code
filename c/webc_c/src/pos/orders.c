#include "orders.h"

#include <string.h>

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "src/pos/pos_util.h"
#include "core/http/utils.h"

// =========================================================================
// /pos/orders: order_number + org/staff/customer/status FKs + 4-part
// amounts cell. Children: order_items, payments, deliveries.
// shift_id/order_metadata/delivery_fee are not form columns (populated
// by seeds or left on their defaults).
// =========================================================================

static const char *ord_amt_parts[] = {
    "subtotal", "discount_amount", "tax_amount", "total_amount",
};
static const char *ord_amt_labels[] = { "Subtotal", "Discount", "Tax", "Total" };
static const MD_Cell ord_amt_cell = {
    .parts       = ord_amt_parts,
    .part_labels = ord_amt_labels,
    .part_count  = 4,
    .style       = "stack",
};

MD_Column md_orders_columns[] = {
    { .name = "order_number", .label = "Order", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "org_unit_id", .label = "Org", .type = COL_TYPE_FK_SELECT,
      .nullable = false, .fk_table = "org_units", .fk_label = "ou_name" },
    { .name = "staff_id", .label = "Staff", .type = COL_TYPE_FK_SELECT,
      .nullable = false, .fk_table = "staff", .fk_label = "first_name" },
    { .name = "customer_id", .label = "Customer", .type = COL_TYPE_FK_SELECT,
      .nullable = true, .fk_table = "customers",
      .fk_label = "customer_type_dict_id" },
    { .name = "order_status_dict_id", .label = "Status",
      .type = COL_TYPE_FK_SELECT, .nullable = true,
      .fk_table = "dictionaries", .fk_label = "label",
      .fk_where = "category = 'ORDER_STATUS'" },
    { .name = "subtotal", .label = "Amounts", .type = COL_TYPE_NUM,
      .nullable = false, .cell = &ord_amt_cell },
};
const size_t md_orders_columns_count = ARRAY_LEN(md_orders_columns);

// Child tab shapes (view-local).
static const char *oi_line_parts[] = { "unit_price", "quantity", "total_line" };
static const char *oi_line_labels[] = { "Unit", "Qty", "Total" };
static const MD_Cell oi_line_cell = {
    .parts       = oi_line_parts,
    .part_labels = oi_line_labels,
    .part_count  = 3,
    .style       = "stack",
};

static const char *pay_parts[] = { "amount", "payment_status" };
static const char *pay_labels[] = { "Amount", "Status" };
static const MD_Cell pay_cell = {
    .parts       = pay_parts,
    .part_labels = pay_labels,
    .part_count  = 2,
    .style       = "stack",
};

static const char *dl_recipient_parts[] = {
    "recipient_name", "recipient_phone", "delivery_address",
};
static const char *dl_recipient_labels[] = { "Name", "Phone", "Address" };
static const MD_Cell dl_recipient_cell = {
    .parts       = dl_recipient_parts,
    .part_labels = dl_recipient_labels,
    .part_count  = 3,
    .style       = "stack",
};

static const char *dl_status_parts[] = { "delivery_status", "delivery_cost" };
static const char *dl_status_labels[] = { "Status", "Cost" };
static const MD_Cell dl_status_cell = {
    .parts       = dl_status_parts,
    .part_labels = dl_status_labels,
    .part_count  = 2,
    .style       = "stack",
};

static MD_Column md_pos_order_item_columns[] = {
    { .name = "product_id", .label = "Product", .type = COL_TYPE_FK_SELECT,
      .nullable = false, .fk_table = "products", .fk_label = "name" },
    { .name = "variant_id", .label = "Variant", .type = COL_TYPE_FK_SELECT,
      .nullable = true, .fk_table = "product_variants",
      .fk_label = "variant_name" },
    { .name = "unit_price", .label = "Line", .type = COL_TYPE_NUM,
      .nullable = false, .cell = &oi_line_cell },
};
static const size_t md_pos_order_item_columns_count =
    ARRAY_LEN(md_pos_order_item_columns);

static MD_Column md_pos_payment_columns[] = {
    { .name = "payment_method_dict_id", .label = "Method",
      .type = COL_TYPE_FK_SELECT, .nullable = true,
      .fk_table = "dictionaries", .fk_label = "label",
      .fk_where = "category = 'PAYMENT_METHOD'" },
    { .name = "amount", .label = "Payment", .type = COL_TYPE_NUM,
      .nullable = false, .cell = &pay_cell },
    { .name = "transaction_ref", .label = "Ref", .type = COL_TYPE_TEXT,
      .nullable = true },
};
static const size_t md_pos_payment_columns_count =
    ARRAY_LEN(md_pos_payment_columns);

static MD_Column md_pos_delivery_columns[] = {
    { .name = "driver_staff_id", .label = "Driver",
      .type = COL_TYPE_FK_SELECT, .nullable = true,
      .fk_table = "staff", .fk_label = "first_name" },
    { .name = "recipient_name", .label = "Recipient", .type = COL_TYPE_TEXT,
      .nullable = false, .cell = &dl_recipient_cell },
    { .name = "delivery_status", .label = "Delivery", .type = COL_TYPE_TEXT,
      .nullable = false, .cell = &dl_status_cell },
};
static const size_t md_pos_delivery_columns_count =
    ARRAY_LEN(md_pos_delivery_columns);

// DB mutations only -- list loading lives in the master_child engine.
// shift_id, delivery_fee and order_metadata are not form columns: they
// come in as missing opts (-> NULL/0) and are never UPDATEd.
static bool create_order(db_t *db, String_View *fields, size_t count) {
    if (count < 12) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "INSERT INTO orders "
                         "(order_number, org_unit_id, staff_id, customer_id, "
                          "order_status_dict_id, subtotal, discount_amount, "
                          "tax_amount, delivery_fee, total_amount, shift_id, "
                          "order_metadata) "
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
        [SQL_MYSQL]    = "INSERT INTO orders "
                         "(order_number, org_unit_id, staff_id, customer_id, "
                          "order_status_dict_id, subtotal, discount_amount, "
                          "tax_amount, delivery_fee, total_amount, shift_id, "
                          "order_metadata) "
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
        [SQL_POSTGRES] = "INSERT INTO orders "
                         "(order_number, org_unit_id, staff_id, customer_id, "
                          "order_status_dict_id, subtotal, discount_amount, "
                          "tax_amount, delivery_fee, total_amount, shift_id, "
                          "order_metadata) "
                         "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12);",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1,  SQL_SV(fields[0])))  return_defer(false);  // number
    if (!sql_bind(&stmt, 2,  SQL_SV(fields[1])))  return_defer(false);  // org
    if (!sql_bind(&stmt, 3,  SQL_SV(fields[2])))  return_defer(false);  // staff
    if (!sql_bind(&stmt, 4,  pos_sv(fields[3])))  return_defer(false);  // customer
    if (!sql_bind(&stmt, 5,  pos_sv(fields[4])))  return_defer(false);  // status
    if (!sql_bind(&stmt, 6,  pos_num(fields[5]))) return_defer(false);  // subtotal
    if (!sql_bind(&stmt, 7,  pos_num(fields[6]))) return_defer(false);  // discount
    if (!sql_bind(&stmt, 8,  pos_num(fields[7]))) return_defer(false);  // tax
    if (!sql_bind(&stmt, 9,  pos_num(fields[10]))) return_defer(false); // delivery
    if (!sql_bind(&stmt, 10, pos_num(fields[8]))) return_defer(false);  // total
    if (!sql_bind(&stmt, 11, pos_sv(fields[9])))  return_defer(false);  // shift
    if (!sql_bind(&stmt, 12, pos_sv(fields[11]))) return_defer(false);  // metadata
    if (!sql_final_step(&stmt))                   return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

// shift_id/order_metadata/delivery_fee are intentionally not UPDATEd:
// they are not form columns, so a bind could only ever keep them.
static bool update_order(db_t *db, String_View *fields, size_t count,
                         String_View id)
{
    if (count < 12) return false;
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE orders SET "
                         "order_number = COALESCE(NULLIF(?, ''), order_number), "
                         "org_unit_id = COALESCE(NULLIF(?, ''), org_unit_id), "
                         "staff_id = COALESCE(NULLIF(?, ''), staff_id), "
                         "customer_id = COALESCE(NULLIF(?, ''), customer_id), "
                         "order_status_dict_id = COALESCE(NULLIF(?, ''), "
                         "order_status_dict_id), "
                         "subtotal = COALESCE(NULLIF(?, ''), subtotal), "
                         "discount_amount = COALESCE(NULLIF(?, ''), discount_amount), "
                         "tax_amount = COALESCE(NULLIF(?, ''), tax_amount), "
                         "total_amount = COALESCE(NULLIF(?, ''), total_amount) "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE orders SET "
                         "order_number = COALESCE(NULLIF(?, ''), order_number), "
                         "org_unit_id = COALESCE(NULLIF(?, ''), org_unit_id), "
                         "staff_id = COALESCE(NULLIF(?, ''), staff_id), "
                         "customer_id = COALESCE(NULLIF(?, ''), customer_id), "
                         "order_status_dict_id = COALESCE(NULLIF(?, ''), "
                         "order_status_dict_id), "
                         "subtotal = COALESCE(NULLIF(?, ''), subtotal), "
                         "discount_amount = COALESCE(NULLIF(?, ''), discount_amount), "
                         "tax_amount = COALESCE(NULLIF(?, ''), tax_amount), "
                         "total_amount = COALESCE(NULLIF(?, ''), total_amount) "
                         "WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE orders SET "
                         "order_number = COALESCE(NULLIF($1, ''), order_number), "
                         "org_unit_id = COALESCE(NULLIF($2, ''), org_unit_id), "
                         "staff_id = COALESCE(NULLIF($3, ''), staff_id), "
                         "customer_id = COALESCE(NULLIF($4, ''), customer_id), "
                         "order_status_dict_id = COALESCE(NULLIF($5, ''), "
                         "order_status_dict_id), "
                         "subtotal = COALESCE(NULLIF($6, ''), subtotal), "
                         "discount_amount = COALESCE(NULLIF($7, ''), discount_amount), "
                         "tax_amount = COALESCE(NULLIF($8, ''), tax_amount), "
                         "total_amount = COALESCE(NULLIF($9, ''), total_amount) "
                         "WHERE id = $10;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    for (int i = 1; i <= 9; ++i) {
        if (!sql_bind(&stmt, i, SQL_SV(fields[i - 1]))) return_defer(false);
    }
    if (!sql_bind(&stmt, 10, SQL_SV(id))) return_defer(false);
    if (!sql_final_step(&stmt))           return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool soft_delete_order(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE orders "
                         "SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
                         "WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE orders SET deleted_at = UTC_TIMESTAMP() WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE orders SET deleted_at = now() WHERE id = $1;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(id)))      return_defer(false);
    if (!sql_final_step(&stmt))               return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

static bool restore_order(db_t *db, String_View id) {
    static const char *const q[SQL_LANG_COUNT] = {
        [SQL_SQLITE]   = "UPDATE orders SET deleted_at = NULL WHERE id = ?;",
        [SQL_MYSQL]    = "UPDATE orders SET deleted_at = NULL WHERE id = ?;",
        [SQL_POSTGRES] = "UPDATE orders SET deleted_at = NULL WHERE id = $1;",
    };
    sql_stmt stmt = {0};
    bool result = true;
    if (!sql_prepare(db, q[db->lang], &stmt)) return_defer(false);
    if (!sql_bind(&stmt, 1, SQL_SV(id)))      return_defer(false);
    if (!sql_final_step(&stmt))               return_defer(false);
defer:
    sql_finalize(&stmt);
    return result;
}

void serve_pos_orders(Serve_Context *sc) {
    MD_ChildTab children[] = {
        {
            .table        = "order_items",
            .title        = "Items",
            .fk_column    = "order_id",
            .id_column    = "id",
            .crud_path    = "/pos/order_items",
            .columns      = md_pos_order_item_columns,
            .column_count = md_pos_order_item_columns_count,
            .soft_delete  = 1,
        },
        {
            .table        = "payments",
            .title        = "Payments",
            .fk_column    = "order_id",
            .id_column    = "id",
            .crud_path    = "/pos/payments",
            .columns      = md_pos_payment_columns,
            .column_count = md_pos_payment_columns_count,
            .soft_delete  = 1,
        },
        {
            .table        = "deliveries",
            .title        = "Delivery",
            .fk_column    = "order_id",
            .id_column    = "id",
            .crud_path    = "/pos/deliveries",
            .columns      = md_pos_delivery_columns,
            .column_count = md_pos_delivery_columns_count,
            .soft_delete  = 1,
        },
    };
    MD_MasterConfig config = {
        .table          = "orders",
        .title          = "Orders",
        .id_column      = "id",
        .crud_path      = "/pos/orders",
        .columns        = md_orders_columns,
        .column_count   = md_orders_columns_count,
        .children       = children,
        .children_count = ARRAY_LEN(children),
        .soft_delete    = 1,
    };
    serve_master_child(sc, &config);
}

static const char *ord_fields[] = {
    "order_number", "org_unit_id", "staff_id", "customer_id",
    "order_status_dict_id", "subtotal", "discount_amount", "tax_amount",
    "total_amount",
};
static const char *ord_opt_fields[] = {
    "shift_id", "delivery_fee", "order_metadata",
};
SERVE_CREATE(pos_orders, order, ord_fields, ord_opt_fields)
SERVE_UPDATE(pos_orders, order, ord_fields, ord_opt_fields)
SERVE_SOFT_DELETE(pos_orders, order)
