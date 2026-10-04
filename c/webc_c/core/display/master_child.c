#define NOB_STRIP_PREFIX
#include "module/nob.h"

#include "master_child.h"
#include "src/db/db.h"
#include "src/helper/cells.h"
#include "core/layout/header.h"
#include "core/layout/footer.h"
#include "core/http/utils.h"
#include "core/display/paging.h"

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
        const MD_Column *col = &child->columns[i];
        if (col->cell) {
            // Composite column: SELECT one column per part; the slot map
            // (md_col_slot) lines values[] up with them.
            for (size_t p = 0; p < col->cell->part_count; ++p) {
                sb_append_cstr(&sb, ", ");
                sb_append_cstr(&sb, col->cell->parts[p]);
            }
        } else {
            sb_append_cstr(&sb, ", ");
            sb_append_cstr(&sb, col->name);
        }
    }
    sb_append_null(&sb);
    return sb.items;
}

static bool load_child_rows(db_t *db,
                            const MD_ChildTab *child,
                            const char        *master_id,
                            bool               show_deleted,
                            MD_ChildRows      *out_rows)
{
    char *cols = build_column_list(child);
    // Same live/trash rule as the master list: soft_delete tabs hide the
    // trash, ?deleted=1 shows ONLY the trash (restore buttons gate on it).
    const char *trash = "";
    if (child->soft_delete) {
        trash = show_deleted ? " AND deleted_at IS NOT NULL"
                             : " AND deleted_at IS NULL";
    }
    char *sql = temp_sprintf("SELECT %s FROM %s WHERE %s = '%s'%s ORDER BY %s DESC;",
            cols, child->table, child->fk_column, md_sql_quote(master_id),
            trash,
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
    char *sql = temp_sprintf("SELECT id, %s FROM %s%s ORDER BY %s ASC;",
            col->fk_label, col->fk_table,
            col->fk_where ? temp_sprintf(" WHERE %s", col->fk_where) : "",
            col->fk_label);
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
            char *sql = temp_sprintf("SELECT id, %s FROM %s%s ORDER BY %s ASC;",
                    fc.col->fk_label, fc.col->fk_table,
                    fc.col->fk_where ? temp_sprintf(" WHERE %s", fc.col->fk_where) : "",
                    fc.col->fk_label);
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

// Soft-delete visibility filter for a table with a deleted_at column.
// Live view hides the trash; ?deleted=1 shows ONLY the trash.
static const char *md_soft_where(int soft_delete, bool show_deleted) {
    if (!soft_delete) return "";
    return show_deleted ? " WHERE deleted_at IS NOT NULL"
                        : " WHERE deleted_at IS NULL";
}

static bool md_load_master_with_children(db_t                  *db,
                                         const MD_MasterConfig *config,
                                         MD_MasterRows         *rows,
                                         const Page_Info       *slice,
                                         bool                   show_deleted)
{
    String_Builder cl = {0};
    sb_append_cstr(&cl, config->id_column);
    for (size_t ci2 = 0; ci2 < config->column_count; ++ci2) {
        const MD_Column *col = &config->columns[ci2];
        if (col->cell) {
            for (size_t p = 0; p < col->cell->part_count; ++p) {
                sb_append_cstr(&cl, ", ");
                sb_append_cstr(&cl, col->cell->parts[p]);
            }
        } else {
            sb_append_cstr(&cl, ", ");
            sb_append_cstr(&cl, col->name);
        }
    }
    sb_append_null(&cl);
    // ORDER BY %s DESC is the single line that decides master order.
    // Child lists are always loaded in full (they are bounded per master
    // and the template windows them); only the master list takes a window.
    char *sql = temp_sprintf("SELECT %s FROM %s%s ORDER BY %s DESC%s;",
            cl.items, config->table, md_soft_where(config->soft_delete, show_deleted),
            config->id_column,
            slice ? temp_sprintf(" LIMIT %zu OFFSET %zu", slice->per_page, slice->offset)
                  : "");
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
            if (!load_child_rows(db, &config->children[ci], row.id,
                                 show_deleted, &row.children[ci])) {
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

// Total master rows (drives the pager; the window itself is applied by
// md_load_master_with_children's LIMIT/OFFSET).
static bool md_count_masters(db_t *db, const MD_MasterConfig *config,
                             bool show_deleted, size_t *out) {
    char *sql = temp_sprintf("SELECT count(*) FROM %s%s;",
            config->table, md_soft_where(config->soft_delete, show_deleted));
    sql_stmt stmt = {0};
    if (!sql_prepare(db, sql, &stmt)) return false;
    bool ok = false;
    if (sql_step(&stmt) == SQL_ROW) {
        *out = (size_t)sql_column_int64(&stmt, 0);
        ok = true;
    } else {
        nob_log(NOB_ERROR, "master_child count: %s", db_errmsg(db));
    }
    sql_finalize(&stmt);
    return ok;
}

void serve_master_child(Serve_Context *sc, const MD_MasterConfig *config) {
    db_t *db = open_webc_db();
    if (!db) {
        serve_error(sc, 500);
        return;
    }
    // ?fragment=all: load everything, render the bare pagination store
    // (no header/footer) -- the payload js/PaginationSwitcher.js caches.
    bool fragment = page_fragment_requested(sc->query_string);
    // ?deleted=1 on a soft_delete page: the trash view (restorable rows).
    // Declared here so every template branch below sees the same flag.
    String_View del_sv = {0};
    bool show_deleted = config->soft_delete
        && form_find(sc->query_string, "deleted", &del_sv)
        && sv_eq(del_sv, sv_from_cstr("1"));
    Page_Info page_info = {0};                    // master list window
    page_info_parse(sc->query_string, "page", &page_info);
    bool windowed = !fragment;
    if (windowed && !md_count_masters(db, config, show_deleted, &page_info.total)) {
        db_close(db);
        serve_error(sc, 500);
        return;
    }
    if (windowed) page_info_finish(&page_info, page_info.total);
    // Child tabs share one page key per table (?notes_page=N for every
    // master); totals are per master and computed in the template.
    size_t child_slot_count = config->children_count ? config->children_count : 1;
    Page_Info child_pages[child_slot_count];
    for (size_t ti = 0; ti < config->children_count; ++ti) {
        page_info_parse(sc->query_string,
                        temp_sprintf("%s_page", config->children[ti].table),
                        &child_pages[ti]);
    }
    MD_MasterRows *rows = md_master_rows_new();
    MD_FormCols form_cols = {0};
    bool ok = md_load_master_with_children(db, config, rows,
                                           windowed ? &page_info : NULL,
                                           show_deleted)
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
        // FK label substitution into display arrays (slot map: a composite
        // column spans part_count slots whose raw copies already stand in
        // for disp -- its parts are plain fields, never FK labels).
        for (size_t ri = 0; ri < rows->count; ++ri) {
            MD_MasterRow *r = &rows->items[ri];
            for (size_t ci = 0; ci < config->column_count; ++ci) {
                if (config->columns[ci].cell) continue;
                size_t slot = md_col_slot(config->columns, ci);
                r->disp[slot] = (char *)md_fk_display(&config->columns[ci], r->values[slot]);
            }
            for (size_t ti = 0; ti < config->children_count; ++ti) {
                const MD_ChildTab *ct = &config->children[ti];
                MD_ChildRows *cr = &r->children[ti];
                for (size_t cj = 0; cj < cr->count; ++cj) {
                    for (size_t cci = 0; cci < ct->column_count; ++cci) {
                        if (ct->columns[cci].cell) continue;
                        size_t slot = md_col_slot(ct->columns, cci);
                        cr->items[cj].disp[slot] =
                            (char *)md_fk_display(&ct->columns[cci], cr->items[cj].values[slot]);
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
    if (fragment) page_info_finish(&page_info, rows->count);
    String_Builder *sb = &sc->body;
    sb->count = 0;
    // Route path feeds NAV_ACTIVE highlighting in the sidebar and the
    // ?redirect= targets of every mutation form on this page.
    const char *md_path = temp_sprintf("%.*s", (int)sc->uri.count, sc->uri.data);
    // Pager scope contract (see display/component/pagination.h.tt).
    String_View page_query = sc->query_string;
    const char *page_base = md_path;
    const char *page_key = "page";
    const char *page_container = "mc-tbody";
    if (!fragment) render_page_header(sb, config->title, md_path);
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
    if (!fragment) render_page_footer(sb);
    md_master_rows_free(rows);
    http_render_response(sc, 200, "text/html", sb_to_sv(*sb));
}
