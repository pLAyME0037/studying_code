#define NOB_STRIP_PREFIX
#include "module/nob.h"

#include "master_child.h"
#include "src/db/db.h"
#include "core/layout/header.h"
#include "core/layout/footer.h"
#include "core/http/utils.h"

// =========================================================================
// Master-child view engine: loads a configured master table plus its child
// tabs and renders the page. Pure interface implementation -- no knowledge
// of any concrete page (see people.c for the /people composition).
// =========================================================================

// ---- data loading -------------------------------------------------------

static MD_MasterRows *md_master_rows_new(void) {
    MD_MasterRows *rows = malloc(sizeof(MD_MasterRows));
    rows->items = NULL;
    rows->count = 0;
    rows->capacity = 0;
    return rows;
}

static void md_master_rows_free(MD_MasterRows *rows) {
    if (!rows) return;
    for (size_t i = 0; i < rows->count; ++i) {
        MD_MasterRow *row = &rows->items[i];
        free(row->values);
        free(row->disp);
        if (row->children) {
            for (size_t ci = 0; ci < row->children_count; ++ci) {
                MD_ChildRows *crows = &row->children[ci];
                free(crows->items);
            }
            free(row->children);
        }
    }
    free(rows->items);
    free(rows);
}

static char *col_to_str(sql_stmt *stmt, int col) {
    const char *val = sql_column_text(stmt, col);
    return val ? temp_strdup(val) : temp_strdup("");
}

// Quote a TEXT id for literal use in generated SQL (' -> '').
static const char *md_sql_quote(const char *s) {
    String_Builder sb = {0};
    for (const char *p = s; *p; ++p) {
        if (*p == '\'') sb_append_cstr(&sb, "''");
        else sb_append_buf(&sb, p, 1);
    }
    sb_append_null(&sb);
    char *copy = temp_strdup(sb.items);
    sb_free(sb);
    return copy;
}

static char *build_column_list(const MD_ChildTab *child) {
    String_Builder sb = {0};
    sb_append_cstr(&sb, child->id_column);
    for (size_t i = 0; i < child->column_count; ++i) {
        sb_append_cstr(&sb, ", ");
        sb_append_cstr(&sb, child->columns[i].name);
    }
    sb_append_null(&sb);
    return sb.items;
}

static bool load_child_rows(db_t *db,
                            const MD_ChildTab *child,
                            const char        *master_id,
                            MD_ChildRows      *out_rows)
{
    char *cols = build_column_list(child);
    char *sql = temp_sprintf("SELECT %s FROM %s WHERE %s = '%s' ORDER BY %s DESC;",
            cols, child->table, child->fk_column, md_sql_quote(master_id),
            child->id_column);
    sql_stmt stmt = {0};
    if (!sql_prepare(db, sql, &stmt)) {
        return false;
    }
    Sql_Step ret;
    for (ret = sql_step(&stmt); ret == SQL_ROW; ret = sql_step(&stmt)) {
        int col_count = sql_column_count(&stmt);
        MD_ChildRow row = {0};
        row.value_count = col_count;
        row.values = malloc(col_count * sizeof(char *));
        row.disp   = malloc(col_count * sizeof(char *));
        for (int i = 0; i < col_count; ++i) {
            row.values[i] = col_to_str(&stmt, i);
            row.disp[i]   = temp_strdup(row.values[i]);
        }
        da_append(out_rows, row);
    }
    if (ret != SQL_DONE) {
        nob_log(NOB_ERROR, "master_child: %s", db_errmsg(db));
        sql_finalize(&stmt);
        return false;
    }
    sql_finalize(&stmt);
    return true;
}

static void md_col_load_options(db_t *db, MD_Column *col) {
    if (!col || col->type != COL_TYPE_FK_SELECT || !col->fk_table) return;
    char *sql = temp_sprintf("SELECT id, %s FROM %s ORDER BY %s ASC;",
            col->fk_label, col->fk_table, col->fk_label);
    sql_stmt stmt = {0};
    if (!sql_prepare(db, sql, &stmt)) {
        return;
    }
    MD_Option *items = NULL;
    size_t count = 0, cap = 0;
    Sql_Step ret;
    for (ret = sql_step(&stmt); ret == SQL_ROW; ret = sql_step(&stmt)) {
        if (count == cap) {
            cap = cap ? cap * 2 : 8;
            items = realloc(items, cap * sizeof(MD_Option));
        }
        const char *lid = sql_column_text(&stmt, 0);
        items[count].id = temp_strdup(lid ? lid : "");
        const char *l = sql_column_text(&stmt, 1);
        items[count].label = temp_strdup(l ? l : "");
        count++;
    }
    sql_finalize(&stmt);
    col->opt = items;
    col->opt_count = count;
}

static const char *md_fk_display(const MD_Column *col, const char *id) {
    if (!col || col->type != COL_TYPE_FK_SELECT || !id || !id[0]) return id;
    for (size_t i = 0; i < col->opt_count; ++i) {
        if (strcmp(id, col->opt[i].id) == 0) return col->opt[i].label;
    }
    return id;
}

static bool md_form_cols_load(db_t *db, const MD_MasterConfig *config, MD_FormCols *out) {
    for (size_t i = 0; i < config->column_count; ++i) {
        MD_FormCol fc = { .col = &config->columns[i] };
        if (fc.col->type == COL_TYPE_FK_SELECT && fc.col->fk_table) {
            char *sql = temp_sprintf("SELECT id, %s FROM %s ORDER BY %s ASC;",
                    fc.col->fk_label, fc.col->fk_table, fc.col->fk_label);
            sql_stmt stmt = {0};
            if (!sql_prepare(db, sql, &stmt)) {
                return false;
            }
            Sql_Step ret;
            for (ret = sql_step(&stmt); ret == SQL_ROW; ret = sql_step(&stmt)) {
                MD_Option o = {0};
                const char *oid = sql_column_text(&stmt, 0);
                o.id = temp_strdup(oid ? oid : "");
                const char *l = sql_column_text(&stmt, 1);
                o.label = temp_strdup(l ? l : "");
                da_append(&fc, o);
            }
            sql_finalize(&stmt);
        }
        da_append(out, fc);
    }
    return true;
}

static bool md_load_master_with_children(db_t                  *db,
                                         const MD_MasterConfig *config,
                                         MD_MasterRows         *rows)
{
    String_Builder cl = {0};
    sb_append_cstr(&cl, config->id_column);
    for (size_t ci2 = 0; ci2 < config->column_count; ++ci2) {
        sb_append_cstr(&cl, ", ");
        sb_append_cstr(&cl, config->columns[ci2].name);
    }
    sb_append_null(&cl);
    char *sql = temp_sprintf("SELECT %s FROM %s ORDER BY %s DESC;", cl.items, config->table, config->id_column);
    sql_stmt stmt = {0};
    if (!sql_prepare(db, sql, &stmt)) {
        return false;
    }
    Sql_Step ret;
    for (ret = sql_step(&stmt); ret == SQL_ROW; ret = sql_step(&stmt)) {
        int col_count = sql_column_count(&stmt);
        MD_MasterRow row = {0};
        row.value_count = col_count;
        row.values = malloc(col_count * sizeof(char *));
        row.disp   = malloc(col_count * sizeof(char *));
        for (int i = 0; i < col_count; ++i) {
            row.values[i] = col_to_str(&stmt, i);
            row.disp[i]   = temp_strdup(row.values[i]);
        }
        row.id = row.values[0];  /* column list starts with id_column */
        row.children = malloc(config->children_count * sizeof(MD_ChildRows));
        row.children_count = config->children_count;
        for (size_t ci = 0; ci < config->children_count; ++ci) {
            row.children[ci].items = NULL;
            row.children[ci].count = 0;
            row.children[ci].capacity = 0;
            if (!load_child_rows(db, &config->children[ci], row.id, &row.children[ci])) {
                free(row.values);
                for (size_t ci2 = 0; ci2 <= ci; ++ci2) {
                    MD_ChildRows *cr = &row.children[ci2];
                    for (size_t j = 0; j < cr->count; ++j) {
                        free(cr->items[j].values);
                    }
                    free(cr->items);
                }
                free(row.children);
                sql_finalize(&stmt);
                return false;
            }
        }
        da_append(rows, row);
    }
    if (ret != SQL_DONE) {
        nob_log(NOB_ERROR, "master_child: %s", db_errmsg(db));
        sql_finalize(&stmt);
        return false;
    }
    sql_finalize(&stmt);
    return true;
}

// ---- entry point --------------------------------------------------------

void serve_master_child(Serve_Context *sc, const MD_MasterConfig *config) {
    db_t *db = open_webc_db();
    if (!db) {
        serve_error(sc, 500);
        return;
    }
    MD_MasterRows *rows = md_master_rows_new();
    MD_FormCols form_cols = {0};
    bool ok = md_load_master_with_children(db, config, rows)
        && md_form_cols_load(db, config, &form_cols);
    if (ok) {
        for (size_t i = 0; i < config->column_count; ++i) {
            md_col_load_options(db, &config->columns[i]);
        }
        for (size_t ti = 0; ti < config->children_count; ++ti) {
            for (size_t cci = 0; cci < config->children[ti].column_count; ++cci) {
                md_col_load_options(db, &config->children[ti].columns[cci]);
            }
        }
        // FK label substitution into display arrays
        for (size_t ri = 0; ri < rows->count; ++ri) {
            MD_MasterRow *r = &rows->items[ri];
            for (size_t ci = 0; ci < config->column_count; ++ci) {
                r->disp[ci + 1] = (char *)md_fk_display(&config->columns[ci], r->values[ci + 1]);
            }
            for (size_t ti = 0; ti < config->children_count; ++ti) {
                const MD_ChildTab *ct = &config->children[ti];
                MD_ChildRows *cr = &r->children[ti];
                for (size_t cj = 0; cj < cr->count; ++cj) {
                    for (size_t cci = 0; cci < ct->column_count; ++cci) {
                        cr->items[cj].disp[cci + 1] = (char *)md_fk_display(&ct->columns[cci], cr->items[cj].values[cci + 1]);
                    }
                }
            }
        }
    }
    db_close(db);
    if (!ok) {
        md_master_rows_free(rows);
        serve_error(sc, 500);
        return;
    }
    String_Builder *sb = &sc->body;
    sb->count = 0;
    // Route path feeds NAV_ACTIVE highlighting in the sidebar and the
    // ?redirect= targets of every mutation form on this page.
    const char *md_path = temp_sprintf("%.*s", (int)sc->uri.count, sc->uri.data);
    render_page_header(sb, config->title, md_path);
#define OUT(buf, size) sb_append_buf(sb, buf, size);
#define INT(v) sb_append_cstr(sb, temp_sprintf("%zu", v));
#define LLINT(v) sb_append_cstr(sb, temp_sprintf("%lld", v));
#define STR(s) sb_append_cstr(sb, s);
#define ESCAPED(s) sb_append_html_escaped(sb, s ? s : "");
#define PAGE_TITLE(s) sb_append_cstr(sb, s);
#include "build/h_to_html/component/master_child.h"
#undef OUT
#undef INT
#undef LLINT
#undef STR
#undef ESCAPED
#undef PAGE_TITLE
    render_page_footer(sb);
    md_master_rows_free(rows);
    http_render_response(sc, 200, "text/html", sb_to_sv(*sb));
}
