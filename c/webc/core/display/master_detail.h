#ifndef CORE_MASTER_DETAIL_H_
#define CORE_MASTER_DETAIL_H_

#include "../http/serve.h"
#include <stddef.h>
#include <sqlite3.h>

typedef struct {
    long long id;
    char     *label;
} MD_Option;

typedef enum {
    COL_TYPE_TEXT,
    COL_TYPE_NUM,
    COL_TYPE_DATE,
    COL_TYPE_TEXTAREA,
    COL_TYPE_FK_SELECT,
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
    long long     id;
    char        **values;   /* raw ids for FK cols (edit prefill) */
    char        **disp;     /* display text (FK cols show label) */
    size_t        value_count;
    MD_ChildRows *children;
    size_t        children_count;
} MD_MasterRow;

typedef struct {
    MD_MasterRow *items;
    size_t        count;
    size_t        capacity;
} MD_MasterRows;

MD_MasterRows *md_master_rows_new(void);
void md_master_rows_free(MD_MasterRows *rows);
bool md_load_master_with_children(sqlite3               *db,
                                  const MD_MasterConfig *config,
                                  MD_MasterRows         *rows);
bool md_form_cols_load(sqlite3 *db, const MD_MasterConfig *config, MD_FormCols *out);

void md_render_master_detail_list(Serve_Context         *sc,
                                  const MD_MasterConfig *config,
                                  MD_MasterRows         *rows);

void serve_master_detail_list(Serve_Context *sc, const MD_MasterConfig *config);

void serve_master_detail_by_table(Serve_Context *sc, const char *master_table);
const MD_MasterConfig *find_master_config(const char *table);

#endif // CORE_MASTER_DETAIL_H_
