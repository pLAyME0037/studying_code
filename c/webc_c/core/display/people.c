#define NOB_STRIP_PREFIX
#include "module/nob.h"

#include "people.h"

// =========================================================================
// /people: users master, their notes as child rows.
// Composition only -- column shapes come from the entity files, loading
// and rendering from master_child.c.
// =========================================================================

void serve_people(Serve_Context *sc) {
    MD_ChildTab children[] = {
        {
            .table        = "notes",
            .title        = "Notes",
            .fk_column    = "user_id",
            .id_column    = "id",
            .crud_path    = "/notes",
            .columns      = md_notes_columns,
            .column_count = md_notes_columns_count,
        },
    };
    MD_MasterConfig config = {
        .table          = "users",
        .title          = "Users",
        .id_column      = "id",
        .crud_path      = "/users",
        .columns        = md_users_columns,
        .column_count   = md_users_columns_count,
        .children       = children,
        .children_count = ARRAY_LEN(children),
    };
    serve_master_child(sc, &config);
}
