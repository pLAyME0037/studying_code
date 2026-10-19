#ifndef MASTER_CHILD_H_
#define MASTER_CHILD_H_

#include "../http/serve.h"
#include <stddef.h>
#include "../../src/db/sql.h"

// =========================================================================
// Master-child view interface.
//
// Stable API: modules compose their own MD_MasterConfig (column shapes come
// from the entity files) and hand it to serve_master_child(). Adding another
// master-child page never requires editing this header or its engine.
// =========================================================================

typedef struct {
    const char *id;     /* TEXT pk (legacy numeric string or uuid) */
    char       *label;
} MD_Option;

typedef enum {
    COL_TYPE_TEXT,
    COL_TYPE_NUM,
    COL_TYPE_DATE,
    COL_TYPE_TEXTAREA,
    COL_TYPE_FK_SELECT,
    COL_TYPE_BLOB,      /* stored image path; shown as its URL text */
} Col_Type;

/* Composite column: ONE <td> (and one edit cell) holding N same-table
 * fields. parts[0] is the leading field (first SELECTed, avatar image
 * slot for "avatar"); style picks the renderer in src/helper/cells.c:
 *   "stack"   - flex-col lines, color-ranked (parts[0] strongest)
 *   "avatar"  - image (parts[0]) + primary (parts[1]) + secondary
 *               (parts[2]) + status (parts[3], rings the picture)
 *   "activity"- created/updated/deleted human dates + DELETED marker
 * part_labels feeds the per-part edit/create input labels (NULL -> parts).
 * part_choices[p] (NULL-terminated list, NULL entry = free text) turns
 * part p's form input into a <select>; create defaults to its first
 * option and edit marks the stored value. */
typedef struct {
    const char **parts;
    const char **part_labels;
    size_t       part_count;
    const char  *style;
    const char ***part_choices;
} MD_Cell;

typedef struct {
    const char *name;
    const char *label;
    Col_Type    type;
    int         nullable;
    const char *fk_table;
    const char *fk_label;
    const char *fk_where;    /* raw WHERE fragment scoping the option query,
                              * e.g. "category = 'ORDER_STATUS'" -- keeps
                              * dict-backed selects to their own category */
    const char *fk_value;
    MD_Option  *opt;        /* runtime: FK dropdown options */
    size_t      opt_count;
    int         hidden;     /* never displayed; preserved via hidden input on edit */
    int         computed;   /* display-only value (server-derived); no form input */
    const MD_Cell *cell;    /* composite: N parts render as one cell (see MD_Cell) */
} MD_Column;

/* values[0] is the row id; column ci starts at its first slot below. Flat
 * columns keep the historic values[ci + 1] identity; a composite column
 * spans part_count slots. Every values[]/disp[] index in the templates
 * goes through this map. */
static inline size_t md_col_slot(const MD_Column *cols, size_t ci) {
    size_t slot = 1;
    for (size_t i = 0; i < ci; ++i)
        slot += cols[i].cell ? cols[i].cell->part_count : 1;
    return slot;
}

typedef struct {
    char  **values;
    char  **disp;
    size_t  value_count;
} MD_ChildRow;

typedef struct {
    MD_ChildRow *items;
    size_t       count;
    size_t       capacity;
} MD_ChildRows;

typedef struct {
    const char *table;
    const char *title;
    const char *fk_column;
    const char *id_column;
    const char *crud_path;
    MD_Column  *columns;
    size_t      column_count;
    const char **sum_columns;
    size_t      sum_column_count;
    int         soft_delete;  /* rows carry deleted_at: loader hides them, add-row/edit gated */
} MD_ChildTab;

typedef struct {
    const char  *table;
    const char  *title;
    const char  *id_column;
    const char  *crud_path;   /* POST base: /create, /{id}/update|delete */
    MD_Column   *columns;
    size_t       column_count;
    const char **sum_columns;
    size_t       sum_column_count;
    MD_ChildTab *children;
    size_t       children_count;
    int          soft_delete; /* rows carry deleted_at: delete route stamps it,
                                 ?deleted=1 shows the trash with a restore
                                 button; child tabs hide their own deleted rows */
    int          read_only;   /* hides add/edit/delete UI; the module simply
                                 registers no write routes for this page */
} MD_MasterConfig;

/* column + resolved FK dropdown options for the generic create form */
typedef struct {
    MD_Column *col;
    MD_Option *items;
    size_t     count;
    size_t     capacity;
} MD_FormCol;

typedef struct {
    MD_FormCol *items;
    size_t      count;
    size_t      capacity;
} MD_FormCols;

typedef struct {
    const char   *id;        /* aliases values[0] (the id column) */
    char        **values;    /* raw ids for FK cols (edit prefill) */
    char        **disp;      /* display text (FK cols show label) */
    size_t        value_count;
    MD_ChildRows *children;
    size_t        children_count;
} MD_MasterRow;

typedef struct {
    MD_MasterRow *items;
    size_t        count;
    size_t        capacity;
} MD_MasterRows;

/* Load, render and respond 200 with the master-child page for `config`. */
void serve_master_child(Serve_Context *sc, const MD_MasterConfig *config);

#endif // MASTER_CHILD_H_
