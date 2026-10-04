#include "audit.h"

#include "core/display/master_child.h"
#include "module/webc_template.h"
#include "src/db/db.h"
#include "core/http/utils.h"

// =========================================================================
// /pos/audit: audit_logs, read_only (mirrors /pos/finance). Old/new JSON
// live in one stack cell; no create form, edit/delete/restore buttons
// render and no write routes are registered.
// =========================================================================

static const char *audit_diff_parts[] = { "old_values", "new_values" };
static const char *audit_diff_labels[] = { "Old", "New" };
static const MD_Cell audit_diff_cell = {
    .parts       = audit_diff_parts,
    .part_labels = audit_diff_labels,
    .part_count  = 2,
    .style       = "stack",
};

static MD_Column md_audit_columns[] = {
    { .name = "user_id", .label = "User", .type = COL_TYPE_FK_SELECT,
      .nullable = true, .fk_table = "users", .fk_label = "name" },
    { .name = "table_name", .label = "Table", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "record_id", .label = "Record", .type = COL_TYPE_TEXT,
      .nullable = false },
    { .name = "action", .label = "Action", .type = COL_TYPE_TEXT,
      .nullable = false },   // INSERT|UPDATE|DELETE|SOFT_DELETE|RESTORE (CHECK)
    { .name = "old_values", .label = "Diff", .type = COL_TYPE_TEXT,
      .nullable = true, .cell = &audit_diff_cell },
    { .name = "created_at", .label = "When", .type = COL_TYPE_DATE,
      .nullable = false },
};
static const size_t md_audit_columns_count = ARRAY_LEN(md_audit_columns);

void serve_pos_audit(Serve_Context *sc) {
    MD_MasterConfig config = {
        .table          = "audit_logs",
        .title          = "Audit",
        .id_column      = "id",
        .crud_path      = "/pos/audit",
        .columns        = md_audit_columns,
        .column_count   = md_audit_columns_count,
        .read_only      = 1,
        .soft_delete    = 1,
    };
    serve_master_child(sc, &config);
}
