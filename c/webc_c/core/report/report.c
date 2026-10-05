#define NOB_STRIP_PREFIX
#include "report.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "core/layout/footer.h"
#include "core/layout/header.h"
#include "src/db/db.h"

// =========================================================================
// Registry: MVP reports (Phase 8). SQL is sqlite-canonical; the MVP
// queries are ANSI or view-backed so the same text works wherever the
// views exist.
// =========================================================================

static const char *const h_sales_summary[] = {
    "លេខបញ្ជាទិញ", "កាលបរិច្ឆេទ", "សាខា", "អតិថិជន",
    "អ្នកលក់", "សរុប", "បានបង់", "ដឹកជញ្ជូន",
};
static const char *const h_daily_orders[] = {
    "លេខបញ្ជាទិញ", "កាលបរិច្ឆេទ", "សរុប", "ស្ថានភាព",
};
static const char *const h_low_stock[] = {
    "ផលិតផល", "SKU", "ចំនួនសល់", "កម្រិតអប្បបរមា", "កម្រិតអតិបរមា",
};
static const char *const h_product_ranking[] = {
    "ផលិតផល", "ចំនួនលក់", "ប្រាក់ចំណូល",
};

#define REPORT_FOOTER \
    "ប្រព័ន្ធគ្រប់គ្រងហាង POS · ឯកសារសម្រាប់ប្រើប្រាស់ខាងក្នុងប៉ុណ្ណោះ"

const Report_Def report_registry[] = {
    {
        .id = "sales_summary",
        .khmer_title = "របាយការណ៍លក់និងដឹកជញ្ជូន",
        .sql =
            "SELECT order_number, order_date, branch_name, customer_name, "
            "cashier_name, total_amount, total_paid, delivery_status "
            "FROM v_pos_sales_delivery_report ORDER BY order_date;",
        .headers = h_sales_summary,
        .header_count = ARRAY_LEN(h_sales_summary),
        .footer = REPORT_FOOTER,
    },
    {
        .id = "daily_orders",
        .khmer_title = "របាយការណ៍បញ្ជាទិញប្រចាំថ្ងៃ",
        .sql =
            "SELECT o.order_number, o.created_at, o.total_amount, "
            "COALESCE(d.label, '') "
            "FROM orders o LEFT JOIN dictionaries d "
            "ON o.order_status_dict_id = d.id "
            "WHERE o.deleted_at IS NULL ORDER BY o.created_at DESC;",
        .headers = h_daily_orders,
        .header_count = ARRAY_LEN(h_daily_orders),
        .footer = REPORT_FOOTER,
    },
    {
        .id = "low_stock",
        .khmer_title = "របាយការណ៍ស្តុកទាប",
        .sql =
            "SELECT p.name, v.sku, s.quantity, s.min_threshold, s.max_threshold "
            "FROM inventory_stocks s "
            "JOIN products p ON s.product_id = p.id "
            "LEFT JOIN product_variants v ON s.variant_id = v.id "
            "WHERE s.deleted_at IS NULL AND p.deleted_at IS NULL "
            "AND s.quantity <= s.min_threshold ORDER BY s.quantity;",
        .headers = h_low_stock,
        .header_count = ARRAY_LEN(h_low_stock),
        .footer = REPORT_FOOTER,
    },
    {
        .id = "product_ranking",
        .khmer_title = "ចំណាត់ថ្នាក់កំពូលផលិតផល",
        .sql =
            "SELECT p.name, SUM(oi.quantity), SUM(oi.total_line) "
            "FROM order_items oi JOIN products p ON oi.product_id = p.id "
            "WHERE oi.deleted_at IS NULL AND p.deleted_at IS NULL "
            "GROUP BY p.id, p.name ORDER BY 2 DESC;",
        .headers = h_product_ranking,
        .header_count = ARRAY_LEN(h_product_ranking),
        .footer = REPORT_FOOTER,
    },
};
const size_t report_registry_count = ARRAY_LEN(report_registry);

const Report_Def *report_find(const char *id) {
    for (size_t i = 0; i < report_registry_count; ++i) {
        if (strcmp(report_registry[i].id, id) == 0) return &report_registry[i];
    }
    return NULL;
}

// =========================================================================
// Source markup: UTF-8 HTML, @page A4 (spike winner over FODT -- A4
// honored by Writer/Web, NotoSansKhmer embedded, ~1 s, table fidelity ok)
// =========================================================================

#define REPORT_CSS                                                    \
    "@page { size: A4; margin: 1.4cm 1.2cm; }\n"                      \
    "body { font-family: \"Noto Sans Khmer\", \"Khmer OS Siemreap\","  \
    " sans-serif; font-size: 11pt; color: #111; }\n"                  \
    "h1 { font-size: 15pt; margin: 0 0 2px; text-align: center; }\n"   \
    "h2 { font-size: 11pt; margin: 0 0 6px; font-weight: 600;"        \
    " text-align: center; }\n"                                        \
    ".meta { font-size: 9pt; color: #444; margin-bottom: 8px;"        \
    " text-align: center; }\n"                                        \
    "table { width: 100%; border-collapse: collapse; }\n"             \
    "th, td { border: 1px solid #555; padding: 3px 6px;"              \
    " font-size: 9.5pt; text-align: left; }\n"                        \
    "th { background: #eceff1; font-weight: 600; }\n"                 \
    ".footer { margin-top: 10px; font-size: 8.5pt; color: #444;"      \
    " border-top: 1px solid #999; padding-top: 4px; }\n"

static const char *report_shop_name(void) {
    const char *name = NULL;
    db_t *db = open_webc_db();
    if (db) {
        sql_stmt stmt = {0};
        if (sql_prepare(db,
                "SELECT config_value FROM system_configs "
                "WHERE config_key = 'pos.shop_name' "
                "AND deleted_at IS NULL;", &stmt)) {
            if (sql_step(&stmt) == SQL_ROW) {
                const char *v = sql_col_text(&stmt, 0);
                if (v && v[0]) name = temp_strdup(v);
            }
            sql_finalize(&stmt);
        }
        db_close(db);
    }
    return name && name[0] ? name : "ហាង POS";
}

bool report_build_html(const Report_Def *rep, String_Builder *out) {
    db_t *db = open_webc_db();
    if (!db) return false;

    String_Builder rows = {0};
    size_t n_rows = 0;
    int ncols = 0;
    bool ok = true;

    sql_stmt stmt = {0};
    if (!sql_prepare(db, rep->sql, &stmt)) {
        ok = false;
        goto done;
    }
    ncols = sql_col_count(&stmt);
    while (sql_step(&stmt) == SQL_ROW) {
        sb_append_cstr(&rows, "<tr>");
        for (int ci = 0; ci < ncols; ++ci) {
            const char *v = sql_col_text(&stmt, ci);
            sb_append_cstr(&rows, "<td>");
            sb_append_html_escaped(&rows, v ? v : "");
            sb_append_cstr(&rows, "</td>");
        }
        sb_append_cstr(&rows, "</tr>\n");
        ++n_rows;
    }
    sql_finalize(&stmt);

    {
        char when[32] = {0};
        time_t now = time(NULL);
        struct tm tm_buf;
        localtime_r(&now, &tm_buf);
        strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tm_buf);

        sb_append_cstr(out, "<!DOCTYPE html>\n<html lang=\"km\">"
                            "<head><meta charset=\"utf-8\">");
        sb_append_cstr(out, "<title>");
        sb_append_html_escaped(out, rep->khmer_title);
        sb_append_cstr(out, "</title><style>");
        sb_append_cstr(out, REPORT_CSS);
        sb_append_cstr(out, "</style></head><body>");

        sb_append_cstr(out, "<h1>");
        sb_append_html_escaped(out, report_shop_name());
        sb_append_cstr(out, "</h1><h2>");
        sb_append_html_escaped(out, rep->khmer_title);
        sb_append_cstr(out, "</h2>");
        sb_append_cstr(out, "<div class=\"meta\">ថ្ងៃទាញយក: ");
        sb_append_cstr(out, when);
        sb_append_cstr(out, " · ចំនួនស៊េរី: ");
        sb_appendf(out, "%zu", n_rows);
        sb_append_cstr(out, "</div>");

        sb_append_cstr(out, "<table><thead><tr>");
        for (int ci = 0; ci < ncols; ++ci) {
            const char *label =
                (size_t)ci < rep->header_count ? rep->headers[ci] : NULL;
            sb_append_cstr(out, "<th>");
            if (label) sb_append_html_escaped(out, label);
            else sb_appendf(out, "%d", ci + 1);
            sb_append_cstr(out, "</th>");
        }
        sb_append_cstr(out, "</tr></thead><tbody>");
        sb_append_sv(out, sb_to_sv(rows));
        sb_append_cstr(out, "</tbody></table>");

        if (rep->footer && rep->footer[0]) {
            sb_append_cstr(out, "<div class=\"footer\">");
            sb_append_html_escaped(out, rep->footer);
            sb_append_cstr(out, "</div>");
        }
        sb_append_cstr(out, "</body></html>");
    }

done:
    sb_free(rows);
    db_close(db);
    return ok;
}

// =========================================================================
// Converter: soffice --headless wrapped in `timeout` (no hang on a wedged
// LibreOffice). One shared UserInstallation profile dir keeps starts warm
// and avoids a per-request profile litter; the work dir is a fresh
// mkdtemp removed on both paths.
//
// Two output paths, decided by the spike (Phase 8):
//   pdf  -- html opens as Writer/Web, which honors @page A4 directly.
//   docx -- Writer/Web has NO docx export filter ("no export filter"),
//          so step 1 opens the html as Writer (infilter), exports flat
//          ODT (plain text), step 2 patches letter -> A4 (Writer's html
//          import ignores @page), step 3 turns that into the docx.
// =========================================================================

static bool soffice_run(const char *soffice, const char *profile_url,
                        const char *dir, const char *infilter, // nullable
                        const char *convert_to, const char *in_path,
                        char *log, size_t log_sz)
{
    char cmd[1400];
    snprintf(cmd, sizeof cmd,
             "timeout 25 '%s' --headless -env:UserInstallation=%s %s"
             "--convert-to %s --outdir '%s' '%s' 2>&1",
             soffice, profile_url,
             infilter ? temp_sprintf("--infilter=\"%s\" ", infilter) : "",
             convert_to, dir, in_path);
    log[0] = 0;
    FILE *p = popen(cmd, "r");
    if (!p) {
        nob_log(NOB_ERROR, "report_convert: popen failed");
        return false;
    }
    size_t got = fread(log, 1, log_sz - 1, p);
    log[got] = 0;
    int rc = pclose(p);
    if (rc != 0) {
        nob_log(NOB_ERROR, "report_convert: rc=%d: %s", rc, log);
        return false;
    }
    return true;
}

static void sb_replace_str(String_Builder *out, const char *s,
                           const char *from, const char *to)
{
    size_t fl = strlen(from);
    for (const char *p = s; *p;) {
        const char *hit = NULL;
        for (const char *q = p; *q; ++q) {
            if (strncmp(q, from, fl) == 0) { hit = q; break; }
        }
        if (!hit) { sb_append_cstr(out, p); break; }
        sb_append_buf(out, p, (size_t)(hit - p));
        sb_append_cstr(out, to);
        p = hit + fl;
    }
}

// Letter -> A4 in the plain-text ODT (whole attribute replaced, so body
// styles can't be touched by accident).
static bool report_patch_a4(const char *path) {
    String_Builder sb = {0};
    if (!read_entire_file(path, &sb)) return false;
    sb_append_buf(&sb, "", 1); // NUL-terminate for the scanners below
    String_Builder out = {0};
    const char *text = (const char *)sb.items;
    sb_replace_str(&out, text,
                   "fo:page-width=\"8.5in\"", "fo:page-width=\"21cm\"");
    sb_append_buf(&out, "", 1);
    String_Builder out2 = {0};
    sb_replace_str(&out2, (const char *)out.items,
                   "fo:page-height=\"11in\"", "fo:page-height=\"29.7cm\"");
    sb_append_buf(&out2, "", 1);
    bool ok = false;
    FILE *f = fopen(path, "we");
    if (f && out2.count > 1
        && fwrite(out2.items, 1, out2.count - 1, f) == out2.count - 1) {
        ok = true;
    }
    if (f) fclose(f);
    sb_free(sb);
    sb_free(out);
    sb_free(out2);
    if (!ok) nob_log(NOB_ERROR, "report_convert: a4 patch failed on %s", path);
    return ok;
}

void *report_convert(const char *html, const char *ext, size_t *out_len) {
    const char *tmp_root = getenv("TMPDIR");
    if (!tmp_root || !tmp_root[0]) tmp_root = "/tmp";
    const char *soffice = getenv("WEBC_SOFFICE");
    if (!soffice || !soffice[0]) soffice = "soffice";

    char dir[256];
    snprintf(dir, sizeof dir, "%s/webc_report_XXXXXX", tmp_root);
    if (!mkdtemp(dir)) {
        nob_log(NOB_ERROR, "report_convert: mkdtemp failed");
        return NULL;
    }
    char html_path[320];
    char out_path[336];
    snprintf(html_path, sizeof html_path, "%s/report.html", dir);
    snprintf(out_path, sizeof out_path, "%s/report.%s", dir, ext);

    FILE *f = fopen(html_path, "we");
    if (!f || fwrite(html, 1, strlen(html), f) != strlen(html)) {
        if (f) fclose(f);
        nob_log(NOB_ERROR, "report_convert: cannot write %s", html_path);
        unlink(html_path);
        rmdir(dir);
        return NULL;
    }
    fclose(f);

    const char *profile_url = temp_sprintf("file://%s/webc_lo_profile",
                                           tmp_root);
    char soffice_log[4096];
    bool made = false;
    if (strcmp(ext, "docx") == 0) {
        char fodt_path[336];
        snprintf(fodt_path, sizeof fodt_path, "%s/report.fodt", dir);
        made = soffice_run(soffice, profile_url, dir, "HTML (StarWriter)",
                           "fodt", html_path, soffice_log,
                           sizeof soffice_log)
               && report_patch_a4(fodt_path)
               && soffice_run(soffice, profile_url, dir, NULL, "docx",
                              fodt_path, soffice_log, sizeof soffice_log);
        unlink(fodt_path);
    } else {
        made = soffice_run(soffice, profile_url, dir, NULL, ext, html_path,
                           soffice_log, sizeof soffice_log);
    }

    String_Builder file = {0};
    bool loaded = made && read_entire_file(out_path, &file);
    if (!loaded) {
        nob_log(NOB_ERROR, "report_convert: no output %s (%s)", out_path,
                soffice_log);
        sb_free(file);
    }
    unlink(html_path);
    unlink(out_path);
    rmdir(dir);
    if (!loaded) return NULL;

    *out_len = file.count;
    return file.items; // realloc'd buffer, caller frees
}

// =========================================================================
// Handlers
// =========================================================================

void serve_reports_index(Serve_Context *sc) {
    String_Builder *sb = &sc->body;
    sb->count = 0;
    render_page_header(sb, "Reports", "/reports");

    sb_append_cstr(sb,
        "<section class=\"px-4 py-3 max-w-5xl\">"
        "<p class=\"text-xs text-gray-500 dark:text-gray-400 mb-2\">"
        "ឯកសារ PDF/DOCX បង្កើតតាមរយៈ LibreOffice · A4 · ភាសាខ្មែរ</p>"
        "<table class=\"w-full text-sm border border-gray-200 "
        "dark:border-gray-700\">"
        "<thead><tr class=\"bg-slate-100 dark:bg-slate-800 text-xs "
        "uppercase tracking-wide text-slate-500\">"
        "<th class=\"px-2 py-1.5 text-left\">របាយការណ៍</th>"
        "<th class=\"px-2 py-1.5 text-left\">ID</th>"
        "<th class=\"px-2 py-1.5 text-left\">ទម្រង់</th>"
        "</tr></thead><tbody>");
    for (size_t i = 0; i < report_registry_count; ++i) {
        const Report_Def *rep = &report_registry[i];
        sb_append_cstr(sb,
            "<tr class=\"border-t border-gray-200 dark:border-gray-700 "
            "hover:bg-gray-50 dark:hover:bg-gray-800/60\">"
            "<td class=\"px-2 py-1.5\">");
        sb_append_html_escaped(sb, rep->khmer_title);
        sb_append_cstr(sb, "</td><td class=\"px-2 py-1.5 font-mono "
                           "text-xs\">");
        sb_append_cstr(sb, rep->id);
        sb_append_cstr(sb, "</td><td class=\"px-2 py-1.5\">"
                           "<a class=\"text-indigo-600 hover:underline "
                           "font-medium\" href=\"/reports/");
        sb_append_cstr(sb, rep->id);
        sb_append_cstr(sb,
            ".pdf\">PDF</a>"
            "<span class=\"text-gray-300 dark:text-gray-600 mx-1\">·</span>"
            "<a class=\"text-indigo-600 hover:underline font-medium\" "
            "href=\"/reports/");
        sb_append_cstr(sb, rep->id);
        sb_append_cstr(sb, ".docx\">DOCX</a></td></tr>");
    }
    sb_append_cstr(sb, "</tbody></table></section>");

    render_page_footer(sb);
    http_render_response(sc, 200, "text/html", sb_to_sv(*sb));
}

void serve_report_download(Serve_Context *sc) {
    static const char prefix[] = "/reports/";
    const size_t prefix_len = sizeof(prefix) - 1;
    if (sc->uri.count <= prefix_len + 3
        || memcmp(sc->uri.data, prefix, prefix_len) != 0) {
        serve_error(sc, 404);
        return;
    }
    // /reports/<id>.<ext>; id charset-guarded below (no traversal, and a
    // clean filename for Content-Disposition).
    char id_ext[128];
    size_t n = sc->uri.count - prefix_len;
    if (n >= sizeof id_ext) { serve_error(sc, 404); return; }
    memcpy(id_ext, sc->uri.data + prefix_len, n);
    id_ext[n] = 0;

    char *dot = strrchr(id_ext, '.');
    if (!dot || dot == id_ext) { serve_error(sc, 404); return; }
    const char *ext = dot + 1;
    bool is_pdf = strcmp(ext, "pdf") == 0;
    bool is_docx = strcmp(ext, "docx") == 0;
    if (!is_pdf && !is_docx) { serve_error(sc, 404); return; }
    *dot = 0;
    for (char *q = id_ext; *q; ++q) {
        char ch = *q;
        bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                  || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
        if (!ok) { serve_error(sc, 404); return; }
    }
    const Report_Def *rep = report_find(id_ext);
    if (!rep) { serve_error(sc, 404); return; }

    String_Builder html = {0};
    if (!report_build_html(rep, &html)) {
        sb_free(html);
        serve_error(sc, 500);
        return;
    }
    size_t len = 0;
    void *bytes = report_convert((const char *)html.items, ext, &len);
    sb_free(html);
    if (!bytes) {
        serve_error(sc, 502); // soffice failed/timed out -- logged above
        return;
    }
    http_render_response_attachment(
        sc,
        is_pdf ? "application/pdf"
               : "application/vnd.openxmlformats-officedocument."
                 "wordprocessingml.document",
        temp_sprintf("%s.%s", rep->id, is_pdf ? "pdf" : "docx"),
        (String_View) { .data = bytes, .count = len });
    free(bytes);
}
