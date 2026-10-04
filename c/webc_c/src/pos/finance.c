#include "finance.h"

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "core/http/utils.h"

// =========================================================================
// /pos/finance: financial_ledgers, read_only (mirrors /pos/dictionaries).
// Account and amounts are stack cells; no create form, edit/delete or
// restore buttons render and no write routes are registered.
// =========================================================================

static const char *fin_account_parts[] = { "account_code", "entry_type" };
static const char *fin_account_labels[] = { "Account", "Entry" };
static const MD_Cell fin_account_cell = {
    .parts       = fin_account_parts,
    .part_labels = fin_account_labels,
    .part_count  = 2,
    .style       = "stack",
};

static const char *fin_amount_parts[] = { "amount", "balance" };
static const char *fin_amount_labels[] = { "Amount", "Balance" };
static const MD_Cell fin_amount_cell = {
    .parts       = fin_amount_parts,
    .part_labels = fin_amount_labels,
    .part_count  = 2,
    .style       = "stack",
};

static MD_Column md_finance_columns[] = {
    { .name = "org_unit_id", .label = "Org", .type = COL_TYPE_FK_SELECT,
      .nullable = false, .fk_table = "org_units", .fk_label = "ou_name" },
    { .name = "reference_type", .label = "Type", .type = COL_TYPE_TEXT,
      .nullable = false },   // ORDER_PAYMENT | EXPENSE | CASH_IN | CASH_OUT
    { .name = "account_code", .label = "Account", .type = COL_TYPE_TEXT,
      .nullable = false, .cell = &fin_account_cell },
    { .name = "amount", .label = "Amounts", .type = COL_TYPE_NUM,
      .nullable = false, .cell = &fin_amount_cell },
    { .name = "description", .label = "Description", .type = COL_TYPE_TEXT,
      .nullable = true },
};
static const size_t md_finance_columns_count = ARRAY_LEN(md_finance_columns);

void serve_pos_finance(Serve_Context *sc) {
    MD_MasterConfig config = {
        .table          = "financial_ledgers",
        .title          = "Finance",
        .id_column      = "id",
        .crud_path      = "/pos/finance",
        .columns        = md_finance_columns,
        .column_count   = md_finance_columns_count,
        .read_only      = 1,
        .soft_delete    = 1,
    };
    serve_master_child(sc, &config);
}
