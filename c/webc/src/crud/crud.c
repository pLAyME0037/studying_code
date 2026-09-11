#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOB_STRIP_PREFIX
#include "../../nob.h"

#include "sqlite3.h"
#include "crud.h"
#include "../db/db.h"

// Build the row-loading query. Result columns:
//   0: m.<pk>
//   for each module column i: raw value at 1+2i, display label at 2+2i
// (FK columns show the joined label; others repeat the raw value).
static void crud_build_select(String_Builder *sql, const Crud_Module *mod, int id) {
    const char *pk = mod->pk_name ? mod->pk_name : "id";
    sb_append_cstr(sql, temp_sprintf("SELECT m.%s, ", pk));
    for (size_t i = 0; i < mod->column_count; ++i) {
        const Crud_Column *col = &mod->columns[i];
        if (col->type == COL_FK_SELECT) {
            sb_append_cstr(sql, temp_sprintf("m.%s, f%zu.%s, ", col->name, i, col->fk_label));
        } else {
            sb_append_cstr(sql, temp_sprintf("m.%s, m.%s, ", col->name, col->name));
        }
    }
    sql->count -= 2; // drop trailing ", "
    sb_append_cstr(sql, temp_sprintf(" FROM %s m", mod->table));
    for (size_t i = 0; i < mod->column_count; ++i) {
        const Crud_Column *col = &mod->columns[i];
        if (col->type == COL_FK_SELECT) {
            sb_append_cstr(sql, temp_sprintf(
                " LEFT JOIN %s f%zu ON f%zu.%s = m.%s",
                col->fk_table, i, i, col->fk_value, col->name));
        }
    }
    if (id >= 0) sb_append_cstr(sql, temp_sprintf(" WHERE m.%s = %d", pk, id));
    sb_append_cstr(sql, temp_sprintf(" ORDER BY m.%s ASC;", pk));
}

bool crud_rows_load(sqlite3 *db, const Crud_Module *mod, int id, Crud_Rows *rows) {
    bool result = true;
    sqlite3_stmt *stmt = NULL;

    String_Builder sql = {0};
    crud_build_select(&sql, mod, id);
    if (sqlite3_prepare_v2(db, sql.items, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }

    int ret = SQLITE_DONE;
    for (ret = sqlite3_step(stmt); ret == SQLITE_ROW; ret = sqlite3_step(stmt)) {
        Crud_Row row = {0};
        row.id = sqlite3_column_int64(stmt, 0);
        row.values = malloc(sizeof(char *) * mod->column_count);
        row.labels = malloc(sizeof(char *) * mod->column_count);
        if (!row.values || !row.labels) {
            free(row.values);
            free(row.labels);
            return_defer(false);
        }
        for (size_t i = 0; i < mod->column_count; ++i) {
            const char *v = (const char *)sqlite3_column_text(stmt, 1 + 2 * i);
            const char *l = (const char *)sqlite3_column_text(stmt, 2 + 2 * i);
            row.values[i] = v ? temp_strdup(v) : NULL;
            row.labels[i] = l ? temp_strdup(l) : NULL;
        }
        da_append(rows, row);
    }

    if (ret != SQLITE_DONE) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }

defer:
    if (stmt) sqlite3_finalize(stmt);
    return result;
}

void crud_rows_free(Crud_Rows *rows) {
    for (size_t i = 0; i < rows->count; ++i) {
        free(rows->items[i].values);
        free(rows->items[i].labels);
    }
    free(rows->items);
    memset(rows, 0, sizeof(*rows));
}

bool crud_form_options_load(sqlite3 *db, const Crud_Module *mod, Crud_Form_Options *fopts) {
    bool result = true;

    fopts->items = calloc(mod->column_count, sizeof(Crud_Options));
    if (!fopts->items) return_defer(false);

    for (size_t i = 0; i < mod->column_count; ++i) {
        const Crud_Column *col = &mod->columns[i];
        if (col->type != COL_FK_SELECT) continue;

        sqlite3_stmt *stmt = NULL;
        String_View sql = sv_from_cstr(temp_sprintf(
            "SELECT %s, %s FROM %s ORDER BY %s ASC;",
            col->fk_value, col->fk_label, col->fk_table, col->fk_label));
        if (sqlite3_prepare_v2(db, sql.data, -1, &stmt, NULL) != SQLITE_OK) {
            LOG_SQLITE3_ERROR(db);
            return_defer(false);
        }
        int ret = SQLITE_DONE;
        for (ret = sqlite3_step(stmt); ret == SQLITE_ROW; ret = sqlite3_step(stmt)) {
            long long opt_id = sqlite3_column_int64(stmt, 0);
            const char *label = (const char *)sqlite3_column_text(stmt, 1);
            da_append(&fopts->items[i], ((Crud_Option) {
                .id    = opt_id,
                .label = label ? temp_strdup(label) : NULL,
            }));
        }
        if (ret != SQLITE_DONE) {
            LOG_SQLITE3_ERROR(db);
            sqlite3_finalize(stmt);
            return_defer(false);
        }
        sqlite3_finalize(stmt);
    }

defer:
    return result;
}

void crud_form_options_free(Crud_Form_Options *fopts) {
    if (fopts->items) {
        free(fopts->items);
    }
    memset(fopts, 0, sizeof(*fopts));
}

// Bind one form value onto `idx` of a prepared statement, honoring column type.
static bool crud_bind_value(sqlite3_stmt *stmt, const Crud_Column *col,
                            int idx, const char *value) {
    if (!value || value[0] == '\0') {
        if (sqlite3_bind_null(stmt, idx) != SQLITE_OK) return false;
    } else if (col->type == COL_NUM || col->type == COL_FK_SELECT) {
        if (sqlite3_bind_double(stmt, idx, atof(value)) != SQLITE_OK) return false;
    } else {
        if (sqlite3_bind_text(stmt, idx, value, -1, SQLITE_TRANSIENT) != SQLITE_OK) return false;
    }
    return true;
}

bool crud_insert(sqlite3 *db, const Crud_Module *mod, const char *const *values) {
    bool result = true;
    sqlite3_stmt *stmt = NULL;

    String_Builder sql = {0};
    sb_append_cstr(&sql, temp_sprintf("INSERT INTO %s (", mod->table));
    for (size_t i = 0; i < mod->column_count; ++i) {
        sb_append_cstr(&sql, mod->columns[i].name);
        if (i + 1 < mod->column_count) sb_append_cstr(&sql, ", ");
    }
    sb_append_cstr(&sql, ") VALUES (");
    for (size_t i = 0; i < mod->column_count; ++i) {
        sb_append_cstr(&sql, "?");
        if (i + 1 < mod->column_count) sb_append_cstr(&sql, ", ");
    }
    sb_append_cstr(&sql, ");");

    if (sqlite3_prepare_v2(db, sql.items, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    for (size_t i = 0; i < mod->column_count; ++i) {
        if (!crud_bind_value(stmt, &mod->columns[i], (int)i + 1, values[i])) {
            LOG_SQLITE3_ERROR(db);
            return_defer(false);
        }
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }

defer:
    if (stmt) sqlite3_finalize(stmt);
    return result;
}

bool crud_update(sqlite3 *db, const Crud_Module *mod, int id, const char *const *values) {
    bool result = true;
    sqlite3_stmt *stmt = NULL;

    String_Builder sql = {0};
    sb_append_cstr(&sql, temp_sprintf("UPDATE %s SET ", mod->table));
    for (size_t i = 0; i < mod->column_count; ++i) {
        sb_append_cstr(&sql, temp_sprintf("%s = ?", mod->columns[i].name));
        if (i + 1 < mod->column_count) sb_append_cstr(&sql, ", ");
    }
    sb_append_cstr(&sql, temp_sprintf(" WHERE %s = ?;", mod->pk_name ? mod->pk_name : "id"));

    if (sqlite3_prepare_v2(db, sql.items, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    for (size_t i = 0; i < mod->column_count; ++i) {
        if (!crud_bind_value(stmt, &mod->columns[i], (int)i + 1, values[i])) {
            LOG_SQLITE3_ERROR(db);
            return_defer(false);
        }
    }
    if (sqlite3_bind_int(stmt, (int)mod->column_count + 1, id) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }

defer:
    if (stmt) sqlite3_finalize(stmt);
    return result;
}

bool crud_delete(sqlite3 *db, const Crud_Module *mod, int id) {
    bool result = true;
    sqlite3_stmt *stmt = NULL;

    String_View sql = sv_from_cstr(temp_sprintf(
        "DELETE FROM %s WHERE %s = ?;", mod->table, mod->pk_name ? mod->pk_name : "id"));
    if (sqlite3_prepare_v2(db, sql.data, -1, &stmt, NULL) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_bind_int(stmt, 1, id) != SQLITE_OK) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        LOG_SQLITE3_ERROR(db);
        return_defer(false);
    }

defer:
    if (stmt) sqlite3_finalize(stmt);
    return result;
}
