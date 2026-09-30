#ifndef SRC_CRUD_H_
#define SRC_CRUD_H_

#include <stdbool.h>
#include <stddef.h>
#include "sqlite3.h"

typedef enum {
    COL_TEXT,      // free text input (single line)
    COL_TEXTAREA,  // multiline text
    COL_NUM,       // numeric input
    COL_DATE,      // date input (stores as text "YYYY-MM-DD")
    COL_FK_SELECT, // dropdown populated from another table; value is the foreign key int
} Column_Type;

typedef struct {
    const char *name;       // DB column name
    const char *label;      // display label
    Column_Type type;
    bool nullable;          // can be empty
    // For COL_FK_SELECT:
    const char *fk_table;   // e.g. "IllTypes"
    const char *fk_label;   // display column for the option label, e.g. "type"
    const char *fk_value;   // value column, usually "id"
} Crud_Column;

typedef struct {
    const char *path;       // route prefix, e.g. "/ill-types"
    const char *title;      // page title
    const char *table;      // DB table
    const char *pk_name;    // primary key column name (default "id" when NULL)
    Crud_Column *columns;   // excluding the PK column
    size_t column_count;
} Crud_Module;

// A loaded row. `id` is the primary key; `values` are the raw column values
// (FK columns carry the foreign key as a string); `labels` are the display
// values (FK columns carry the joined label, others equal `values`).
typedef struct {
    long long id;
    char **values;
    char **labels;
} Crud_Row;

typedef struct {
    Crud_Row *items;
    size_t count;
    size_t capacity;
} Crud_Rows;

// One option in an FK dropdown.
typedef struct {
    long long id;
    const char *label;
} Crud_Option;

typedef struct {
    Crud_Option *items;
    size_t count;
    size_t capacity;
} Crud_Options;

// Per-column FK dropdown options for a form. `items[i]` is only filled for
// COL_FK_SELECT columns; others are empty.
typedef struct {
    Crud_Options *items; // length = mod->column_count
} Crud_Form_Options;

bool crud_rows_load(sqlite3 *db, const Crud_Module *mod, int id, Crud_Rows *rows);
void crud_rows_free(Crud_Rows *rows);
bool crud_form_options_load(sqlite3 *db, const Crud_Module *mod, Crud_Form_Options *fopts);
void crud_form_options_free(Crud_Form_Options *fopts);
bool crud_insert(sqlite3 *db, const Crud_Module *mod, const char *const *values);
bool crud_update(sqlite3 *db, const Crud_Module *mod, int id, const char *const *values);
bool crud_delete(sqlite3 *db, const Crud_Module *mod, int id);

#endif // SRC_CRUD_H_
