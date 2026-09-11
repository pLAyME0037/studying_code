#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crud.h"
#include "../../src/db/db.h"
#include "../../src/crud/crud.h"
#include "../../core/layout/header.h"
#include "../../core/layout/footer.h"
#include "../core/http/utils.h"

// ---- Page rendering ----

static void render_crud_list_page(Serve_Context     *sc,
                                  const Crud_Module *mod,
                                  Crud_Rows         *rows,
                                  Crud_Form_Options *fopts)
{
    String_Builder *sb = &sc->body;
    render_page_header(sb, mod->title, mod->path);
    const char **values = NULL;
#define OUT(buf, size) sb_append_buf(sb, buf, size);
#define INT(x) sb_appendf(sb, "%d", (int)(x));
#define LLINT(x) sb_appendf(sb, "%lld", (long long)(x));
#define STR(x) sb_append_cstr(sb, (x) ? (x) : "");
#define ESCAPED(x) sb_append_html_escaped(sb, (x) ? (x) : "");
#define PAGE_TITLE mod->title
#include "../../build/h_to_html/component/crud_list.h"
#undef PAGE_TITLE
#undef ESCAPED
#undef STR
#undef LLINT
#undef INT
#undef OUT
    render_page_footer(sb);
}

static void render_crud_edit_page(Serve_Context     *sc,
                                  const Crud_Module *mod,
                                  int                id,
                                  const char       **values,
                                  Crud_Form_Options *fopts)
{
    String_Builder *sb = &sc->body;
    render_page_header(sb, temp_sprintf("Edit %s", mod->title), mod->path);
#define OUT(buf, size) sb_append_buf(sb, buf, size);
#define INT(x) sb_appendf(sb, "%d", (int)(x));
#define LLINT(x) sb_appendf(sb, "%lld", (long long)(x));
#define STR(x) sb_append_cstr(sb, (x) ? (x) : "");
#define ESCAPED(x) sb_append_html_escaped(sb, (x) ? (x) : "");
#define PAGE_TITLE mod->title
#include "../../build/h_to_html/component/crud_edit.h"
#undef PAGE_TITLE
#undef ESCAPED
#undef STR
#undef LLINT
#undef INT
#undef OUT
    render_page_footer(sb);
}

void serve_crud_list(Serve_Context *sc, const Crud_Module *mod) {
    sqlite3 *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }

    Crud_Rows rows = {0};
    Crud_Form_Options fopts = {0};
    if (!crud_rows_load(db, mod, -1, &rows)) {
        sqlite3_close(db);
        serve_error(sc, 500);
        return;
    }
    if (!crud_form_options_load(db, mod, &fopts)) {
        crud_rows_free(&rows);
        sqlite3_close(db);
        serve_error(sc, 500);
        return;
    }
    sqlite3_close(db);

    sc->body.count = 0;
    render_crud_list_page(sc, mod, &rows, &fopts);
    crud_rows_free(&rows);
    crud_form_options_free(&fopts);

    http_render_response(sc, 200, "text/html", sb_to_sv(sc->body));
}

// ---- Mutations ----

// Extract all module column values from the form body into `values` (temp-arena).
static bool crud_gather_values(String_View        body,
                               const Crud_Module *mod,
                               const char       **values)
{
    for (size_t i = 0; i < mod->column_count; ++i) {
        const Crud_Column *col = &mod->columns[i];
        char buf[512] = {0};
        if (form_find(body, col->name, buf, sizeof(buf))) {
            values[i] = temp_strdup(buf);
        } else {
            values[i] = NULL;
        }
    }
    return true;
}

// Validate that all non-nullable columns have a non-empty value.
static bool crud_validate(String_View body, String_View query, const Crud_Module *mod) {
    for (size_t i = 0; i < mod->column_count; ++i) {
        if (mod->columns[i].nullable) continue;
        if (mod->columns[i].type == COL_FK_SELECT) continue;
        char buf[512] = {0};
        bool found = form_find(body, mod->columns[i].name, buf, sizeof(buf));
        if (!found && query.count > 0) {
            char param[64];
            snprintf(param, sizeof(param), "%s=", mod->columns[i].name);
            if (sv_starts_with(query, sv_from_cstr(param))) {
                size_t offset = strlen(param);
                size_t remaining = query.count - offset;
                size_t copy_len = remaining < sizeof(buf) - 1 ? remaining : sizeof(buf) - 1;
                memcpy(buf, (char *)query.data + offset, copy_len);
                buf[copy_len] = '\0';
                found = true;
            }
        }
        if (!found || buf[0] == '\0') {
            return false;
        }
    }
    return true;
}

void serve_crud_create(Serve_Context *sc, const Crud_Module *mod) {
    String_View body = sb_to_sv(sc->body);
    if (!crud_validate(body, sc->query_string, mod)) {
        serve_error(sc, 400);
        return;
    }

    const char **values = malloc(sizeof(char *) * mod->column_count);
    if (!values) { serve_error(sc, 500); return; }
    crud_gather_values(body, mod, values);

    if (sc->query_string.count > 0) {
        for (size_t i = 0; i < mod->column_count; ++i) {
            const Crud_Column *col = &mod->columns[i];
            if (col->type == COL_FK_SELECT && values[i] == NULL) {
                char param[64];
                snprintf(param, sizeof(param), "%s=", col->name);
                if (sv_starts_with(sc->query_string, sv_from_cstr(param))) {
                    size_t offset = strlen(param);
                    size_t remaining = sc->query_string.count - offset;
                    values[i] = temp_strndup((char *)sc->query_string.data + offset, remaining);
                }
            }
        }
    }

    sqlite3 *db = open_webc_db();
    if (!db) { free(values); serve_error(sc, 500); return; }
    bool ok = crud_insert(db, mod, values);
    sqlite3_close(db);
    free(values);

    if (!ok) { serve_error(sc, 500); return; }
    
    const char *redirect_path = mod->path;
    if (sc->query_string.count > 0) {
        redirect_path = "/patients";
        if (sv_starts_with(sc->query_string, sv_from_cstr("organization_id="))) redirect_path = "/organizations";
        else if (sv_starts_with(sc->query_string, sv_from_cstr("medicine_import_id="))) redirect_path = "/medicine-imports";
        else if (sv_starts_with(sc->query_string, sv_from_cstr("patient_daily_invoice_id="))) redirect_path = "/patient-daily-invoices";
        else if (sv_starts_with(sc->query_string, sv_from_cstr("patient_medicine_invoice_id="))) redirect_path = "/patient-medicine-invoices";
        else if (sv_starts_with(sc->query_string, sv_from_cstr("patient_invoice_out_id="))) redirect_path = "/patient-invoice-out";
        else if (sv_starts_with(sc->query_string, sv_from_cstr("organization_daily_id="))) redirect_path = "/organization-daily-invoices";
        else if (sv_starts_with(sc->query_string, sv_from_cstr("organization_invoice_id="))) redirect_path = "/organization-invoices";
        else if (sv_starts_with(sc->query_string, sv_from_cstr("organization_invoice_out_id="))) redirect_path = "/organization-invoice-out";
    }
    http_render_redirect(sc, 302, redirect_path);
}

void serve_crud_edit(Serve_Context *sc, const Crud_Module *mod, int id) {
    sqlite3 *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }

    Crud_Rows rows = {0};
    if (!crud_rows_load(db, mod, id, &rows)) {
        sqlite3_close(db);
        serve_error(sc, 500);
        return;
    }
    Crud_Form_Options fopts = {0};
    if (!crud_form_options_load(db, mod, &fopts)) {
        crud_rows_free(&rows);
        sqlite3_close(db);
        serve_error(sc, 500);
        return;
    }
    sqlite3_close(db);

    if (rows.count == 0) {
        crud_rows_free(&rows);
        crud_form_options_free(&fopts);
        serve_error(sc, 404);
        return;
    }
    Crud_Row *row = &rows.items[0];

    sc->body.count = 0;
    render_crud_edit_page(sc, mod, id, (const char **)row->values, &fopts);
    crud_rows_free(&rows);
    crud_form_options_free(&fopts);

    http_render_response(sc, 200, "text/html", sb_to_sv(sc->body));
}

void serve_crud_update(Serve_Context *sc, const Crud_Module *mod, int id) {
    String_View body = sb_to_sv(sc->body);
    if (!crud_validate(body, sv_from_cstr(""), mod)) {
        serve_error(sc, 400);
        return;
    }

    const char **values = malloc(sizeof(char *) * mod->column_count);
    if (!values) { serve_error(sc, 500); return; }
    crud_gather_values(body, mod, values);

    sqlite3 *db = open_webc_db();
    if (!db) { free(values); serve_error(sc, 500); return; }
    bool ok = crud_update(db, mod, id, values);
    sqlite3_close(db);
    free(values);

    if (!ok) { serve_error(sc, 500); return; }
    http_render_redirect(sc, 302, mod->path);
}

void serve_crud_delete(Serve_Context *sc, const Crud_Module *mod, int id) {
    sqlite3 *db = open_webc_db();
    if (!db) { serve_error(sc, 500); return; }

    bool ok = crud_delete(db, mod, id);
    sqlite3_close(db);

    if (!ok) { serve_error(sc, 500); return; }
    http_render_redirect(sc, 302, mod->path);
}
