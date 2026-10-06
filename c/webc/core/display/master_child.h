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

typedef struct {
    const char *name;
    const char *label;
    Col_Type    type;
    int         nullable;
    const char *fk_table;
    const char *fk_label;
    const char *fk_value;
    MD_Option  *opt;        /* runtime: FK dropdown options */
    size_t      opt_count;
    int         hidden;     /* never displayed; preserved via hidden input on edit */
    int         computed;   /* display-only value (server-derived); no form input */
} MD_Column;

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
