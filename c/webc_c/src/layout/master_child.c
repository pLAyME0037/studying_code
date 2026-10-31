#define NOB_STRIP_PREFIX
#include "third_party/nob/nob.h"

#include "master_child.h"
#include "src/db/db.h"
#include "src/helper/cells.h"
#include "src/layout/header.h"
#include "src/layout/footer.h"
#include "src/http/utils.h"
#include "src/layout/paging.h"

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

static bool load_child_rows(db_t              *db,
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
    char *sql = temp_sprintf("SELECT %s FROM %s WHERE %s = '%s'%s "
                             "ORDER BY %s DESC;",
                             cols, child->table, child->fk_column,
                             md_sql_quote(master_id),
                             trash, child->id_column);
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

// WHERE clause offering only LIVE parents to a FK dropdown / header filter:
// a soft-deleted staff member is not a valid cashier to pick. The fk_where
// filter (e.g. "user_type_dict_id = 'CUSTOMER'") is ANDed in, never replaced.
// Note md_fk_display deliberately does NOT use this gate - see there.
static const char *md_fk_options_where(const MD_Column *col) {
    return col->fk_where
        ? temp_sprintf(" WHERE (%s) AND deleted_at IS NULL", col->fk_where)
        : " WHERE deleted_at IS NULL";
}

static void md_col_load_options(db_t *db, MD_Column *col) {
    if (!col || col->type != COL_TYPE_FK_SELECT || !col->fk_table) return;
    char *sql = temp_sprintf("SELECT id, %s FROM %s%s ORDER BY %s ASC;",
            col->fk_label, col->fk_table, md_fk_options_where(col),
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

// Label for one FK cell. Looks in the (live-only) option list first, then
// falls back to its own ungated query: a row that still points at a
// soft-deleted parent - pre-cascade legacy data, or a parent the fk_where
// filter hides - must render its name, not a raw uuid.
static const char *md_fk_display(db_t *db, const MD_Column *col, const char *id) {
    if (!col || col->type != COL_TYPE_FK_SELECT || !id || !id[0]) return id;
    for (size_t i = 0; i < col->opt_count; ++i) {
        if (strcmp(id, col->opt[i].id) == 0) return col->opt[i].label;
    }
    if (!db || !col->fk_label) return id;
    char *sql = temp_sprintf("SELECT %s FROM %s WHERE id = '%s';",
                             col->fk_label, col->fk_table, md_sql_quote(id));
    sql_stmt stmt = {0};
    if (!sql_prepare(db, sql, &stmt)) return id;
    const char *result = id;
    if (sql_step(&stmt) == SQL_ROW) {
        const char *l = sql_column_text(&stmt, 0);
        if (l && l[0]) result = temp_strdup(l);
    }
    sql_finalize(&stmt);
    return result;
}

static bool md_form_cols_load(db_t *db, const MD_MasterConfig *config, MD_FormCols *out) {
    for (size_t i = 0; i < config->column_count; ++i) {
        MD_FormCol fc = { .col = &config->columns[i] };
        if (fc.col->type == COL_TYPE_FK_SELECT && fc.col->fk_table) {
            char *sql = temp_sprintf("SELECT id, %s FROM %s%s ORDER BY %s ASC;",
                    fc.col->fk_label, fc.col->fk_table,
                    md_fk_options_where(fc.col), fc.col->fk_label);
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

// ---- data query (the Data query tab's search) ---------------------------

// A column the search may cover: every displayed column; hidden internals
// (deleted_at ...) never appear in the scope dropdown. A composite cell
// counts as ONE scope matching any of its parts.
static bool md_col_queryable(const MD_Column *col) {
    return col && !col->hidden;
}

#define MD_MAX_SEARCH_TERMS 8

// One filter term: LIKE '%pattern%' over one queryable column (col), or
// over every displayed column when col == NULL. The per-column filter
// row submits ?q_<col>=<text> -- one term per filled box, ANDed; the
// legacy ?q + ?qcol pair is a single term whose col is NULL when the
// scope is empty or unknown (all-columns fallback).
typedef struct {
    const MD_Column *col;      /* scoped column; NULL = all columns */
    String_View      pattern;  /* "%text%" -- bound to this term's LIKEs */
    int              nbinds;   /* out: LIKE placeholders this term owns */
} MD_SearchTerm;

typedef struct {
    MD_SearchTerm terms[MD_MAX_SEARCH_TERMS];
    size_t        term_count;
} MD_Search;

// Append one ANDed term; the pattern is capped at 100 chars and never
// stored empty, so term_count > 0 is the "searching" test.
static void md_search_add(MD_Search *search, const MD_Column *col,
                          String_View pattern) {
    if (!pattern.count || search->term_count >= MD_MAX_SEARCH_TERMS) return;
    if (pattern.count > 100) pattern.count = 100;
    String_Builder pat = {0};
    sb_append_cstr(&pat, "%");
    sb_append_buf(&pat, pattern.data, pattern.count);
    sb_append_cstr(&pat, "%");
    sb_append_null(&pat);
    MD_SearchTerm *t = &search->terms[search->term_count++];
    t->col = col;
    t->pattern = sv_from_cstr(temp_strdup(pat.items));
    sb_free(pat);
}

static bool md_search_on(const MD_Search *s) {
    return s && s->term_count > 0;
}

// Positional placeholders: $1..$N on postgres, ? elsewhere (bind indexes
// stay 1-based in every driver).
static const char *md_ph(db_t *db, int *n) {
    ++*n;
    return db->lang == SQL_POSTGRES ? temp_sprintf("$%d", *n) : "?";
}

// CAST keeps LIKE type-safe on numeric (and any) columns; mysql spells it
// AS CHAR, sqlite/postgres AS TEXT.
static void md_append_like_cast(String_Builder *sb, db_t *db, const char *expr) {
    sb_append_cstr(sb, "CAST(");
    sb_append_cstr(sb, expr);
    sb_append_cstr(sb, db->lang == SQL_MYSQL
                          ? " AS CHAR) LIKE "
                          : " AS TEXT) LIKE ");
}

// The whole WHERE for one master view: the soft-delete scope plus the
// table search -- each search term ANDed as its own parenthesized group
// (LIKE over the scoped column, or every displayed one, and for FK
// columns over the label the cell shows: a correlated EXISTS, so "PAID"
// finds its orders without a join). Returns "" or " WHERE ..."; nbinds
// counts LIKE placeholders: bind each term's pattern to the slots it
// owns right after sql_prepare (md_bind_search).
static const char *md_where(db_t *db, const MD_MasterConfig *config,
                            bool show_deleted, MD_Search *search,
                            int *nbinds)
{
    if (nbinds) *nbinds = 0;
    String_Builder sb = {0};
    const char *joiner = " WHERE ";
    if (config->soft_delete) {
        sb_append_cstr(&sb, show_deleted ? " WHERE deleted_at IS NOT NULL"
                                         : " WHERE deleted_at IS NULL");
        joiner = " AND ";
    }
    if (md_search_on(search)) {
        for (size_t t = 0; t < search->term_count; ++t) {
            const MD_SearchTerm *term = &search->terms[t];
            String_Builder cond = {0};
            int n = 0;
            for (size_t ci = 0; ci < config->column_count; ++ci) {
                const MD_Column *col = &config->columns[ci];
                if (!md_col_queryable(col)) continue;
                if (term->col && term->col != col) continue;
                if (cond.count) sb_append_cstr(&cond, " OR ");
                if (col->cell) {
                    for (size_t p = 0; p < col->cell->part_count; ++p) {
                        if (p) sb_append_cstr(&cond, " OR ");
                        md_append_like_cast(&cond, db, col->cell->parts[p]);
                        sb_append_cstr(&cond, md_ph(db, &n));
                    }
                } else if (col->type == COL_TYPE_FK_SELECT
                           && col->fk_table && col->fk_label) {
                    sb_append_cstr(&cond, "(");
                    md_append_like_cast(&cond, db, col->name);
                    sb_append_cstr(&cond, md_ph(db, &n));
                    sb_append_cstr(&cond, " OR EXISTS (SELECT 1 FROM ");
                    sb_append_cstr(&cond, col->fk_table);
                    // Live parents only: typing the name of a soft-deleted
                    // staff member must not surface its rows.
                    sb_append_cstr(&cond, " mdq WHERE mdq.deleted_at IS NULL "
                                          "AND mdq.id = ");
                    sb_append_cstr(&cond, config->table);
                    sb_append_cstr(&cond, ".");
                    sb_append_cstr(&cond, col->name);
                    sb_append_cstr(&cond, " AND ");
                    md_append_like_cast(&cond, db,
                                        temp_sprintf("mdq.%s", col->fk_label));
                    sb_append_cstr(&cond, md_ph(db, &n));
                    sb_append_cstr(&cond, "))");
                } else {
                    md_append_like_cast(&cond, db, col->name);
                    sb_append_cstr(&cond, md_ph(db, &n));
                }
            }
            if (cond.count) {
                sb_append_cstr(&sb, joiner);
                sb_append_cstr(&sb, "(");
                sb_append_buf(&sb, cond.items, cond.count);
                sb_append_cstr(&sb, ")");
                joiner = " AND ";
            }
            search->terms[t].nbinds = n;
            if (nbinds) *nbinds += n;
            sb_free(cond);
        }
    }
    sb_append_null(&sb);
    char *out = temp_strdup(sb.items);
    sb_free(sb);
    return out;
}

// Bind each term's pattern to the LIKE slots it owns (md_where emits the
// terms' placeholders in order, so the slots run consecutively per term).
// A NULL search (the trash-badge count ignores the filter) has no slots.
static bool md_bind_search(sql_stmt *stmt, const MD_Search *search,
                           int nbinds) {
    if (!search) return true;
    int slot = 1;
    for (size_t t = 0; t < search->term_count && slot <= nbinds; ++t) {
        for (int b = 0; b < search->terms[t].nbinds && slot <= nbinds;
             ++b, ++slot) {
            if (!sql_bind(stmt, slot, SQL_SV(search->terms[t].pattern)))
                return false;
        }
    }
    return true;
}

static bool md_load_master_with_children(db_t                  *db,
                                         const MD_MasterConfig *config,
                                         MD_MasterRows         *rows,
                                         const Page_Info       *slice,
                                         bool                   show_deleted,
                                         MD_Search             *search)
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
    // ORDER BY created_at DESC is the single line that decides master
    // order: newest first, so freshly created rows stay on page 1 even
    // once seeded/backfill rows exist (id DESC put lexicographically
    // large seed ids above random uuids). Every master table carries
    // created_at; views are never master lists. Child lists are always
    // loaded in full (they are bounded per master and the template
    // windows them); only the master list takes a window.
    int nbinds = 0;
    char *sql = temp_sprintf("SELECT %s FROM %s%s ORDER BY %s DESC%s;",
            cl.items, config->table,
            md_where(db, config, show_deleted, search, &nbinds),
            "created_at",
            slice ? temp_sprintf(" LIMIT %zu OFFSET %zu", slice->per_page, slice->offset)
                  : "");
    sql_stmt stmt = {0};
    if (!sql_prepare(db, sql, &stmt)) {
        return false;
    }
    if (!md_bind_search(&stmt, search, nbinds)) {
        sql_finalize(&stmt);
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
                             bool show_deleted, MD_Search *search,
                             size_t *out) {
    int nbinds = 0;
    const char *where = md_where(db, config, show_deleted, search, &nbinds);
    char *sql = temp_sprintf("SELECT count(*) FROM %s%s;",
            config->table, where);
    sql_stmt stmt = {0};
    if (!sql_prepare(db, sql, &stmt)) return false;
    bool ok = false;
    if (md_bind_search(&stmt, search, nbinds)
        && sql_step(&stmt) == SQL_ROW) {
        *out = (size_t)sql_column_int64(&stmt, 0);
        ok = true;
    } else {
        nob_log(NOB_ERROR, "master_child count: %s", db_errmsg(db));
    }
    sql_finalize(&stmt);
    return ok;
}

// The per-column filter row: the first <tr> inside the master table's
// header row. Lookup columns (COL_TYPE_FK_SELECT) get a <select> of the
// same choices the add form offers (labels are the query values); plain
// columns get a search input; a composite cell queries its main part
// (col->name == parts[0]) and the placeholder names that part. The last
// cell is submit + clear only -- no box. Inputs post into the empty
// #md-query-form anchor (form attribute) as ?q_<col>=<text>, one ANDed
// term per filled box (md_where).
static void md_query_row(String_Builder *sb, const MD_MasterConfig *config,
                         const MD_FormCols *form_cols, const char *md_path,
                         String_View query, const char *q_text,
                         const char *qcol_name, bool search_active) {
#define OUT(buf, size) sb_append_buf(sb, buf, size);
#define STR(s) sb_append_cstr(sb, s);
#define ESCAPED(s) sb_append_html_escaped(sb, s ? s : "");
#include "build/h_to_html/component/md_child_query_row.h"
#undef OUT
#undef STR
#undef ESCAPED
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
    // The view strip is Live + Trash on a soft_delete page (?deleted=1 =
    // the trash view; no_live hides Live on tables that are never a
    // directly-filled list, and non-soft_delete pages get no strip). The
    // per-column filter row in the table's <thead> (?q_<col> per filled
    // box) filters whichever view is active.
    String_View del_sv = {0};
    bool show_deleted = config->soft_delete
        && form_find(sc->query_string, "deleted", &del_sv)
        && sv_eq(del_sv, sv_from_cstr("1"));
    // Table search: the per-column filter row submits ?q_<col>=<text>
    // per filled box (one ANDed term each); the legacy ?q + ?qcol pair
    // still works as one term. Columns come from this page's config
    // (never interpolated from the URL); every pattern is bound. A qcol
    // that is empty or unknown searches every displayed column.
    String_View q = sv_from_cstr(""), qcol_sv = sv_from_cstr("");
    form_find(sc->query_string, "q", &q);
    form_find(sc->query_string, "qcol", &qcol_sv);
    if (q.count > 100) q.count = 100;
    const char *q_text = q.count
        ? temp_sprintf("%.*s", (int)q.count, q.data) : "";
    const char *qcol_name = "";
    MD_Search search = {0};
    if (q.count > 0) {
        const MD_Column *legacy_col = NULL;
        for (size_t ci = 0; ci < config->column_count; ++ci) {
            const MD_Column *col = &config->columns[ci];
            if (col->name && md_col_queryable(col)
                && sv_eq(qcol_sv, sv_from_cstr(col->name))) {
                legacy_col = col;
                qcol_name = col->name;
                break;
            }
        }
        md_search_add(&search, legacy_col, q);
    }
    for (size_t ci = 0; ci < config->column_count; ++ci) {
        const MD_Column *col = &config->columns[ci];
        if (!col->name || !md_col_queryable(col)) continue;
        String_View cv = sv_from_cstr("");
        if (form_find(sc->query_string, temp_sprintf("q_%s", col->name), &cv)) {
            md_search_add(&search, col, cv);
        }
    }
    Page_Info page_info = {0}; // master list window
    page_info_parse(sc->query_string, "page", &page_info);
    bool windowed = !fragment;
    if (windowed && !md_count_masters(db, config, show_deleted, &search,
                                      &page_info.total)) {
        db_close(db);
        serve_error(sc, 500);
        return;
    }
    if (windowed) page_info_finish(&page_info, page_info.total);
    // View strip badge: the trash total (never narrowed by the search).
    size_t trash_total = 0;
    if (config->soft_delete) {
        md_count_masters(db, config, true, NULL, &trash_total);
    }
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
                                           show_deleted, &search)
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
        // for disp -- its parts are plain fields, never FK labels). Flat
        // date columns humanize here ("05 Oct 2026, 11:56" instead of the
        // raw ISO stamp); values[] keep the raw form for edit prefills.
        for (size_t ri = 0; ri < rows->count; ++ri) {
            MD_MasterRow *r = &rows->items[ri];
            for (size_t ci = 0; ci < config->column_count; ++ci) {
                if (config->columns[ci].cell) continue;
                size_t slot = md_col_slot(config->columns, ci);
                r->disp[slot] = config->columns[ci].type == COL_TYPE_DATE
                    ? (char *)md_date_human(r->values[slot])
                    : (char *)md_fk_display(db, &config->columns[ci],
                                            r->values[slot]);
            }
            for (size_t ti = 0; ti < config->children_count; ++ti) {
                const MD_ChildTab *ct = &config->children[ti];
                MD_ChildRows *cr = &r->children[ti];
                for (size_t cj = 0; cj < cr->count; ++cj) {
                    for (size_t cci = 0; cci < ct->column_count; ++cci) {
                        if (ct->columns[cci].cell) continue;
                        size_t slot = md_col_slot(ct->columns, cci);
                        cr->items[cj].disp[slot] =
                            ct->columns[cci].type == COL_TYPE_DATE
                                ? (char *)md_date_human(cr->items[cj].values[slot])
                                : (char *)md_fk_display(db,
                                                        &ct->columns[cci],
                                                        cr->items[cj].values[slot]);
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
