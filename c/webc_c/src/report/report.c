#define NOB_STRIP_PREFIX
#include "report.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "src/layout/footer.h"
#include "src/layout/header.h"
#include "src/db/db.h"
#include "src/helper/cells.h"

// =========================================================================
// Registry: MVP reports (Phase 8). SQL is sqlite-canonical; the MVP
// queries are ANSI or view-backed so the same text works wherever the
// views exist.
// =========================================================================

static const char *const h_sales_summary[] = {
    "លេខបញ្ជាទិញ", "កាលបរិច្ឆេទ", "សាខា", "អតិថិជន",
    "អ្នកលក់", "តម្លៃសរុប", "បានបង់", "ដឹកជញ្ជូន",
};
static const char *const h_daily_orders[] = {
    "លេខបញ្ជាទិញ", "កាលបរិច្ឆេទ", "តម្លៃសរុប", "ស្ថានភាព",
};
static const char *const h_low_stock[] = {
    "ផលិតផល", "SKU", "ចំនួនសល់", "កម្រិតអប្បបរមា", "កម្រិតអតិបរមា",
};
static const char *const h_product_ranking[] = {
    "ផលិតផល", "ចំនួនលក់", "ប្រាក់ចំណូល",
};

// Per-column print types, parallel to each header array above. Ordering is
// positional: NUM/MONEY columns right-align, group by thousands and feed the
// totals row; DATE columns humanize the ISO stamp.
static const Report_Col_Type t_sales_summary[] = {
    REPORT_COL_TEXT, REPORT_COL_DATE, REPORT_COL_TEXT, REPORT_COL_TEXT,
    REPORT_COL_TEXT, REPORT_COL_MONEY, REPORT_COL_MONEY, REPORT_COL_TEXT,
};
static const Report_Col_Type t_daily_orders[] = {
    REPORT_COL_TEXT, REPORT_COL_DATE, REPORT_COL_MONEY, REPORT_COL_TEXT,
};
static const Report_Col_Type t_low_stock[] = {
    REPORT_COL_TEXT, REPORT_COL_TEXT, REPORT_COL_NUM, REPORT_COL_NUM,
    REPORT_COL_NUM,
};
static const Report_Col_Type t_product_ranking[] = {
    REPORT_COL_TEXT, REPORT_COL_NUM, REPORT_COL_MONEY,
};

// Live ODF page fields -- LibreOffice fills these at layout time (current
// page / total pages), which is the only way to get real pagination: the C
// code emits one blob and never knows how it will paginate. `rfooter` embeds
// them, so it is injected as ODF markup rather than escaped text.
#define PAGE_CURR \
    "<text:page-number text:select-page=\"current\">1</text:page-number>"
#define PAGE_LAST "<text:page-count>1</text:page-count>"
#define REPORT_HEADER_LEFT \
    "ហាង POS · POS Shopping សាខាក្រុងតាខ្មៅ ខេត្តកណ្តាល\n" \
    "សាខាក្រុងតាខ្មៅ ខេត្តកណ្តាល ទូរស័ព្ទលេខ ០១២ ៩៩៨ ៦៦៦\n"  // two row header on the left
#define REPORT_HEADER_MID "របាយការណ៏ផ្ទៃក្នុង"

#define REPORT_FOOTER_LEFT \
    "ប្រព័ន្ធគ្រប់គ្រងហាង POS · ឯកសារសម្រាប់ប្រើប្រាស់ខាងក្នុងប៉ុណ្ណោះ"
#define REPORT_FOOTER_MID "រៀបចំដោយក្រុមបច្ចេកទេស"
#define REPORT_FOOTER_RIGHT "PAGE "PAGE_CURR"-"PAGE_LAST

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
        .col_types = t_sales_summary,
        .lheader = REPORT_HEADER_LEFT,
        .mheader = REPORT_HEADER_MID,
        .lfooter = REPORT_FOOTER_LEFT,
        .mfooter = REPORT_FOOTER_MID,
        .rfooter = REPORT_FOOTER_RIGHT,
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
        .col_types = t_daily_orders,
        .lheader = REPORT_HEADER_LEFT,
        .mheader = REPORT_HEADER_MID,
        .lfooter = REPORT_FOOTER_LEFT,
        .mfooter = REPORT_FOOTER_MID,
        .rfooter = REPORT_FOOTER_RIGHT,
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
        .col_types = t_low_stock,
        .lheader = REPORT_HEADER_LEFT,
        .mheader = REPORT_HEADER_MID,
        .lfooter = REPORT_FOOTER_LEFT,
        .mfooter = REPORT_FOOTER_MID,
        .rfooter = REPORT_FOOTER_RIGHT,
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
        .col_types = t_product_ranking,
        .lheader = REPORT_HEADER_LEFT,
        .mheader = REPORT_HEADER_MID,
        .lfooter = REPORT_FOOTER_LEFT,
        .mfooter = REPORT_FOOTER_MID,
        .rfooter = REPORT_FOOTER_RIGHT,
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

// Print styling for the A4 table only. The Cambodian letterhead (Kingdom
// motto + logo + memo) + report title are BODY content starting the document
// on page 1 (see report_append_letterhead); the 3-column imprint header
// (shop / title / date+count) and the footer (live page numbers) repeat on
// every page via report_fodt_decorate. NOTE: LibreOffice's
// html->FODT import DROPS CSS backgrounds and cell borders (only paragraph
// properties survive), so the grid / dark header / zebra are emitted as
// presentational HTML attributes (border, cellpadding, bgcolor, <font color>)
// in report_build_html -- not as CSS. What remains here is @page geometry, the
// font stack, and the .num right-align (a paragraph property that does
// survive).
// Font stack: lead with a Latin face so digits, dates, order numbers and
// status words render with normal advances. Noto Sans Khmer is kept as a
// FALLBACK only -- putting it first made every Latin glyph take a wide
// Khmer advance, printing "2 9 2 4 0" instead of "29240". Latin faces have
// no Khmer glyphs, so Khmer text falls through to Noto Sans Khmer untouched.
#define REPORT_CSS                                                          \
    "@page { size: A4; margin: 1.6cm 1.4cm 1.5cm 1.4cm; }\n"                \
    "body { font-family: \"Noto Sans\", \"Liberation Sans\","               \
    " \"DejaVu Sans\", \"Noto Sans Khmer\", sans-serif;"                    \
    " font-size: 9.5pt; color: #1a1a1a; }\n"                                \
    ".num { text-align: right; font-variant-numeric: tabular-nums;"         \
    " white-space: nowrap; }\n"

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

// Format a decimal string with thousands separators in the integer part and,
// for money, exactly two decimals. Non-numeric input (blank, a stray label)
// is emitted escaped so a mistyped column type never eats data. The fraction
// is preserved as-is for NUM (SUM can be fractional) but pinned to 2 for
// MONEY. Returns a temp-arena string that is safe to print unescaped.
static const char *report_format_number(const char *v, bool money) {
    if (!v || !v[0]) return money ? "0.00" : "0";
    const char *p = v;
    bool neg = false;
    if (*p == '-') { neg = true; ++p; }
    const char *dot = strchr(p, '.');
    size_t int_len = dot ? (size_t)(dot - p) : strlen(p);
    // Guard: anything that is not digits (or one dot) passes through escaped.
    bool plain = true;
    for (size_t i = 0; i < int_len; ++i) {
        if (p[i] < '0' || p[i] > '9') { plain = false; break; }
    }
    String_Builder sb = {0};
    if (!plain) {
        sb_append_html_escaped(&sb, v);
    } else {
        if (neg) sb_append_cstr(&sb, "-");
        for (size_t i = 0; i < int_len; ++i) {
            if (i > 0 && (int_len - i) % 3 == 0) sb_append_cstr(&sb, ",");
            sb_append_buf(&sb, p + i, 1);
        }
        if (money) {
            // Always two decimals: the DB stores "3990.0" (one digit) and
            // money must print 3,990.00 -- pad a short fraction with '0',
            // truncate a long one past the second digit.
            const char *f = dot ? dot + 1 : "";
            sb_appendf(&sb, ".%c%c", f[0] ? f[0] : '0', f[1] ? f[1] : '0');
        } else if (dot) {
            sb_append_cstr(&sb, ".");
            sb_append_cstr(&sb, dot + 1);
        }
    }
    sb_append_null(&sb);
    const char *r = temp_strdup(sb.items ? sb.items : "");
    sb_free(sb);
    return r;
}

// Column type for result column `ci`; extra columns beyond the declared list
// fall back to text.
static Report_Col_Type report_col_type(const Report_Def *rep, int ci) {
    if (!rep->col_types || (size_t)ci >= rep->header_count)
        return REPORT_COL_TEXT;
    return rep->col_types[ci];
}

// =========================================================================
// Cambodian official letterhead (body, page 1). The Kingdom motto is set
// centered in Khmer OS Muol (the formal Khmer face, installed system-wide);
// below it the shop logo + name sit left and the memo "from/to" right, in one
// borderless row; then the report title. This REPLACED the old page-style
// header -- the letterhead carries the identity now, so only the footer
// (live page numbers) repeats per page and nothing doubles up on page 1.
// =========================================================================

// The Cambodian letterhead strings (Kingdom motto, memo from/to, report
// titles) live in display/report/*.h.tt -- design rule: copy the user may
// change belongs to a template, not to C.
#define REPORT_LOGO_PATH "resource/image/user1.png"

// Append `n` bytes of `s` as base64. The logo is embedded as a data: URI so
// the html blob is self-contained -- the soffice child converts a copy in a
// temp dir, where a relative image path would not resolve.
static void sb_append_base64(String_Builder *sb, const unsigned char *s,
                             size_t n) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = (unsigned)s[i] << 16;
        if (i + 1 < n) v |= (unsigned)s[i + 1] << 8;
        if (i + 2 < n) v |= (unsigned)s[i + 2];
        char c[4] = { tbl[(v >> 18) & 63], tbl[(v >> 12) & 63],
                      (i + 1 < n) ? tbl[(v >> 6) & 63] : '=',
                      (i + 2 < n) ? tbl[v & 63] : '=' };
        sb_append_buf(sb, c, 4);
    }
}

// Load the shop logo and sniff its MIME from magic bytes (user1.png is really
// a JPEG). Fills `b64` (raw base64) and `*mime`. Returns false if unreadable,
// in which case the letterhead template emits no <img> (the shop name still
// prints).
static bool report_logo_data(String_Builder *b64, const char **mime) {
    String_Builder img = {0};
    if (!read_entire_file(REPORT_LOGO_PATH, &img)) { sb_free(img); return false; }
    *mime = "image/png";
    if (img.count >= 2 && (unsigned char)img.items[0] == 0xFF
        && (unsigned char)img.items[1] == 0xD8)
        *mime = "image/jpeg";
    sb_append_base64(b64, (const unsigned char *)img.items, img.count);
    sb_free(img);
    return true;
}

// Emit the letterhead + report title at the top of the body (page 1). The
// markup and the Cambodian copy live in display/report/letterhead.h.tt
// (design rule: state-driven HTML belongs to a template); this host feeds it
// the logo bytes, the shop name and the report title.
static void report_append_letterhead(String_Builder *out, const Report_Def *rep) {
#define OUT(buf, size) sb_append_buf(out, buf, size);
#define STR(x) sb_append_cstr(out, (x) ? (x) : "");
#define ESCAPED(x) sb_append_html_escaped(out, (x) ? (x) : "");
#include "build/h_to_html/report/letterhead.h"
#undef OUT
#undef STR
#undef ESCAPED
}

bool report_build_html(const Report_Def *rep, String_Builder *out,
                       size_t *n_rows_out) {
    db_t *db = open_webc_db();
    if (!db) return false;

    size_t n_rows = 0;
    int ncols = 0;
    bool ok = true;
    bool has_num = false;
    // Markup-free result collection: one formatted value string per cell
    // (flat nrows*ncols), per-column numeric flags, and the bold footer
    // row's formatted totals. The document markup itself lives in
    // display/report/doc.h.tt (design rule: state-driven HTML belongs to a
    // template).
    struct { const char **items; size_t count, capacity; } cells = {0};
    bool *is_num = NULL;
    double *totals = NULL;
    const char **totals_str = NULL;

    sql_stmt stmt = {0};
    if (!sql_prepare(db, rep->sql, &stmt)) {
        ok = false;
        goto done;
    }
    ncols = sql_col_count(&stmt);
    {
        // ncols is fixed once the statement is prepared; size the columns on
        // the stack-free heap so a wide result cannot overflow a VLA.
        is_num = calloc((size_t)ncols, sizeof *is_num);
        totals = calloc((size_t)ncols, sizeof *totals);
        totals_str = calloc((size_t)ncols, sizeof *totals_str);
        if (!is_num || !totals || !totals_str) { ok = false; goto done; }
        for (int ci = 0; ci < ncols; ++ci) {
            Report_Col_Type t = report_col_type(rep, ci);
            is_num[ci] = (t == REPORT_COL_NUM || t == REPORT_COL_MONEY);
            if (is_num[ci]) has_num = true;
        }

        while (sql_step(&stmt) == SQL_ROW) {
            for (int ci = 0; ci < ncols; ++ci) {
                const char *v = sql_col_text(&stmt, ci);
                Report_Col_Type t = report_col_type(rep, ci);
                if (t == REPORT_COL_DATE) {
                    // Human date ("05 Oct 2026, 11:56") whether the stamp is
                    // date-only or carries a time; left-aligned like prose.
                    da_append(&cells, temp_strdup(md_date_human(v ? v : "")));
                } else if (is_num[ci]) {
                    totals[ci] += v ? atof(v) : 0.0;
                    da_append(&cells,
                              report_format_number(v, t == REPORT_COL_MONEY));
                } else {
                    da_append(&cells, temp_strdup(v ? v : ""));
                }
            }
            ++n_rows;
        }
        sql_finalize(&stmt);

        // A bold totals row reads the sum of every numeric column (money to 2
        // decimals); text/date cells in that row stay empty (the template
        // places the "Total" label in the first non-numeric column).
        if (has_num && n_rows > 0) {
            for (int ci = 0; ci < ncols; ++ci) {
                if (!is_num[ci]) continue;
                char buf[64];
                bool money = (report_col_type(rep, ci) == REPORT_COL_MONEY);
                if (money)
                    snprintf(buf, sizeof buf, "%.2f", totals[ci]);
                else if (totals[ci] == (double)(long long)totals[ci])
                    snprintf(buf, sizeof buf, "%lld", (long long)totals[ci]);
                else
                    snprintf(buf, sizeof buf, "%.2f", totals[ci]);
                totals_str[ci] = report_format_number(buf, money);
            }
        }
    }

    // The whole document: head + A4/CSS, the Cambodian letterhead (page 1),
    // the table shell with its header row (in <tbody>, NOT <thead> -- so
    // LibreOffice prints it once instead of repeating it per page), the zebra
    // data rows and the bold totals row. Zebra/grid/dark-header are
    // presentational bgcolor attributes because CSS backgrounds + borders do
    // not survive the html->FODT import.
#define OUT(buf, size) sb_append_buf(out, buf, size);
#define STR(x) sb_append_cstr(out, (x) ? (x) : "");
#define INT(x) sb_appendf(out, "%d", (x));
#define ESCAPED(x) sb_append_html_escaped(out, (x) ? (x) : "");
#define CELL(ri, ci) (cells.items[(size_t)(ri) * (size_t)ncols + (size_t)(ci)])
#include "build/h_to_html/report/doc.h"
#undef OUT
#undef STR
#undef INT
#undef ESCAPED
#undef CELL
    // report_convert() runs strlen() on this buffer, and a String_Builder
    // is NOT NUL-terminated by default -- without this the read runs past
    // the allocation into whatever heap follows (it once dragged geo
    // migration SQL into the last page).
    sb_append_null(out);

    if (n_rows_out) *n_rows_out = n_rows;

done:
    free(cells.items);
    free(is_num);
    free(totals);
    free(totals_str);
    db_close(db);
    return ok;
}

// =========================================================================
// Converter: soffice --headless wrapped in `timeout` (no hang on a wedged
// LibreOffice). One shared UserInstallation profile dir keeps starts warm
// and avoids a per-request profile litter; the work dir is a fresh
// mkdtemp removed on both paths.
//
// One path for both formats (pdf and docx), because the repeating imprint
// header/footer + live page numbers only exist in a Writer document:
//   step 1 -- open the html as Writer (infilter), export flat ODT (fodt).
//   step 2 -- patch letter -> A4 (Writer's html import ignores @page size),
//             then inject the page-style header/footer (report_fodt_decorate).
//   step 3 -- export that flat ODT to pdf or docx.
// Writer/Web (the old direct html->pdf path) has no page-style headers/
// footers and no page-number fields, so it cannot repeat a footer or
// show "Page X of Y" -- hence the FODT detour for pdf too.
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

// =========================================================================
// Running page footer (ODF page-style footer).
//
// The Cambodian letterhead lives in the body on page 1; only the footer (live
// page numbers) must repeat on every page. A PAGE-STYLE footer only exists in
// Writer (Writer/Web has none) -- so both pdf and docx route through FODT and
// we inject it here as a 3-column borderless table (a table, not tab stops, so
// a multi-line cell lines up), with a divider rule over the footer cells.
// =========================================================================

// Escape `s` for an ODF text run, turning embedded newlines into
// <text:line-break/> so multi-line notes keep their lineation inside a cell.
static void sb_append_odf_text(String_Builder *sb, const char *s) {
    if (!s) return;
    for (const char *p = s; *p; ++p) {
        switch (*p) {
        case '&':  sb_append_cstr(sb, "&amp;"); break;
        case '<':  sb_append_cstr(sb, "&lt;"); break;
        case '>':  sb_append_cstr(sb, "&gt;"); break;
        case '"':  sb_append_cstr(sb, "&quot;"); break;
        case '\'': sb_append_cstr(sb, "&apos;"); break;
        case '\n': sb_append_cstr(sb, "<text:line-break/>"); break;
        default:   sb_append_buf(sb, p, 1); break;
        }
    }
}

// One 3-column header/footer table. `right_markup` is injected raw (it is the
// ODF page-number field, already markup); left/mid/right text is escaped.
static void sb_append_hf_table(String_Builder *sb, const char *cell_style,
                               const char *left, const char *mid,
                               const char *right, bool right_markup) {
    sb_append_cstr(sb,
        "<table:table table:name=\"HF\" table:style-name=\"RptHFT\">"
        "<table:table-column table:style-name=\"RptCol1\"/>"
        "<table:table-column table:style-name=\"RptCol2\"/>"
        "<table:table-column table:style-name=\"RptCol3\"/>"
        "<table:table-row>");
    sb_appendf(sb, "<table:table-cell table:style-name=\"%s\">"
                   "<text:p text:style-name=\"RptL\">", cell_style);
    sb_append_odf_text(sb, left);
    sb_append_cstr(sb, "</text:p></table:table-cell>");
    sb_appendf(sb, "<table:table-cell table:style-name=\"%s\">"
                   "<text:p text:style-name=\"RptC\">", cell_style);
    sb_append_odf_text(sb, mid);
    sb_append_cstr(sb, "</text:p></table:table-cell>");
    sb_appendf(sb, "<table:table-cell table:style-name=\"%s\">"
                   "<text:p text:style-name=\"RptR\">", cell_style);
    if (right_markup) sb_append_cstr(sb, right ? right : "");
    else sb_append_odf_text(sb, right);
    sb_append_cstr(sb, "</text:p></table:table-cell>");
    sb_append_cstr(sb, "</table:table-row></table:table>");
}

// ODF styles for the header/footer tables: fixed column widths, a divider
// rule (bottom border on header cells, top border on footer cells) and small
// grey text. Injected into <office:automatic-styles>.
#define REPORT_HF_STYLES                                                     \
    "<style:style style:name=\"RptHFT\" style:family=\"table\">"             \
    "<style:table-properties style:width=\"18.2cm\" table:align=\"left\"/>"   \
    "</style:style>"                                                         \
    "<style:style style:name=\"RptCol1\" style:family=\"table-column\">"      \
    "<style:table-column-properties style:column-width=\"8.5cm\"/>"          \
    "</style:style>"                                                         \
    "<style:style style:name=\"RptCol2\" style:family=\"table-column\">"      \
    "<style:table-column-properties style:column-width=\"4.5cm\"/>"          \
    "</style:style>"                                                         \
    "<style:style style:name=\"RptCol3\" style:family=\"table-column\">"      \
    "<style:table-column-properties style:column-width=\"5.2cm\"/>"          \
    "</style:style>"                                                         \
    "<style:style style:name=\"RptCellHdr\" style:family=\"table-cell\">"     \
    "<style:table-cell-properties fo:border-bottom=\"0.75pt solid #334155\"" \
    " fo:padding=\"0.01in 0.04in 0.05in 0.04in\"/></style:style>"            \
    "<style:style style:name=\"RptCellFtr\" style:family=\"table-cell\">"     \
    "<style:table-cell-properties fo:border-top=\"0.75pt solid #334155\""    \
    " fo:padding=\"0.05in 0.04in 0.01in 0.04in\"/></style:style>"            \
    "<style:style style:name=\"RptL\" style:family=\"paragraph\">"            \
    "<style:paragraph-properties fo:text-align=\"start\"/>"                  \
    "<style:text-properties fo:font-size=\"8pt\" fo:color=\"#475569\"/>"     \
    "</style:style>"                                                         \
    "<style:style style:name=\"RptC\" style:family=\"paragraph\">"            \
    "<style:paragraph-properties fo:text-align=\"center\"/>"                 \
    "<style:text-properties fo:font-size=\"8pt\" fo:color=\"#475569\"/>"     \
    "</style:style>"                                                         \
    "<style:style style:name=\"RptR\" style:family=\"paragraph\">"            \
    "<style:paragraph-properties fo:text-align=\"end\"/>"                    \
    "<style:text-properties fo:font-size=\"8pt\" fo:color=\"#475569\"/>"     \
    "</style:style>"

// Inject the running header/footer into the generated FODT (already
// letter->A4 patched by report_patch_a4; page margins come from the @page
// rule in REPORT_CSS, which the html import honors). We add the header/footer
// styles to <office:automatic-styles> and expand the body's self-closing
// "HTML" master-page with a <style:header>/<style:footer> holding the
// 3-column tables -- LibreOffice repeats them on every printed page.
static bool report_fodt_decorate(const char *path, const Report_Def *rep,
                                 size_t n_rows) {
    String_Builder sb = {0};
    if (!read_entire_file(path, &sb)) return false;
    sb_append_buf(&sb, "", 1); // NUL-terminate for the scanners below

    // Imprint header: shop block (left) / report title (mid) / generated
    // date + row count (right), repeated on every page. The Kingdom/logo/
    // memo letterhead is separate BODY content starting the document on
    // page 1 (report_append_letterhead); the page style carries both the
    // imprint header and the footer (live page numbers).
    const char *shop = report_shop_name();
    char right_hdr[96];
    {
        char when[32] = {0};
        time_t now = time(NULL);
        struct tm tm_buf;
        localtime_r(&now, &tm_buf);
        strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tm_buf);
        snprintf(right_hdr, sizeof right_hdr,
                 "ថ្ងៃទាញយក: %s · ចំនួនស៊េរី: %zu", when, n_rows);
    }

    String_Builder hdr = {0};
    sb_append_cstr(&hdr, "<style:header>");
    sb_append_hf_table(&hdr, "RptCellHdr",
                       rep->lheader ? rep->lheader : shop,
                       rep->mheader ? rep->mheader : rep->khmer_title,
                       rep->rheader ? rep->rheader : right_hdr, false);
    sb_append_cstr(&hdr, "</style:header>");

    String_Builder ftr = {0};
    sb_append_cstr(&ftr, "<style:footer>");
    sb_append_hf_table(&ftr, "RptCellFtr",
                       rep->lfooter ? rep->lfooter : "",
                       rep->mfooter ? rep->mfooter : "",
                       rep->rfooter ? rep->rfooter : "", true);
    sb_append_cstr(&ftr, "</style:footer>");

    // Scan the FODT: inject the styles after <office:automatic-styles> and
    // expand the body's "HTML" master-page (the one the body uses) with the
    // chrome; other master-pages are copied through untouched.
    const char *text = (const char *)sb.items;
    String_Builder out = {0};
    const char *p = text;
    while (*p) {
        const char *a = strstr(p, "<office:automatic-styles>");
        const char *m = strstr(p, "<style:master-page");
        if (!a && !m) { sb_append_cstr(&out, p); break; }
        if (m && (!a || m < a)) {
            sb_append_buf(&out, p, (size_t)(m - p));
            const char *sc = strstr(m, "/>");
            if (!sc) { sb_append_cstr(&out, m); break; }
            bool is_html = false;
            for (const char *q = m; q < sc; ++q)
                if (strncmp(q, "style:name=\"HTML\"", 17) == 0) { is_html = true; break; }
            if (is_html) {
                sb_append_buf(&out, m, (size_t)(sc - m)); // "<style:master-page ... "
                sb_append_cstr(&out, ">");
                sb_append_sv(&out, sb_to_sv(hdr));
                sb_append_sv(&out, sb_to_sv(ftr));
                sb_append_cstr(&out, "</style:master-page>");
            } else {
                sb_append_buf(&out, m, (size_t)(sc - m) + 2); // keep "/>"
            }
            p = sc + 2;
        } else {
            sb_append_buf(&out, p, (size_t)(a - p));
            sb_append_cstr(&out, "<office:automatic-styles>");
            sb_append_cstr(&out, REPORT_HF_STYLES);
            p = a + strlen("<office:automatic-styles>");
        }
    }
    sb_append_buf(&out, "", 1);

    bool ok = false;
    FILE *f = fopen(path, "we");
    if (f && out.count > 1
        && fwrite(out.items, 1, out.count - 1, f) == out.count - 1) ok = true;
    if (f) fclose(f);
    sb_free(sb); sb_free(hdr); sb_free(ftr); sb_free(out);
    if (!ok) nob_log(NOB_ERROR, "report_fodt_decorate: write failed on %s", path);
    return ok;
}

void *report_convert(const Report_Def *rep, size_t n_rows, const char *html,
                     const char *ext, size_t *out_len) {
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
    char fodt_path[336];
    snprintf(fodt_path, sizeof fodt_path, "%s/report.fodt", dir);
    // Both pdf and docx route through FODT: Writer/Web (the direct html->pdf
    // path) has no page-style headers/footers or page-number fields, so the
    // running chrome + "Page X of Y" only survive via a Writer (flat ODT)
    // document. Step 1 opens the html as Writer and exports flat ODT; step 2
    // patches letter->A4 and injects the header/footer; step 3 exports the
    // requested format.
    bool made = soffice_run(soffice, profile_url, dir, "HTML (StarWriter)",
                            "fodt", html_path, soffice_log,
                            sizeof soffice_log)
                && report_patch_a4(fodt_path)
                && report_fodt_decorate(fodt_path, rep, n_rows)
                && soffice_run(soffice, profile_url, dir, NULL, ext,
                               fodt_path, soffice_log, sizeof soffice_log);
    unlink(fodt_path);

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

    // The list markup lives in display/report/index.h.tt (design rule:
    // state-driven HTML belongs to a template); this host feeds it the
    // registry of reports.
#define OUT(buf, size) sb_append_buf(sb, buf, size);
#define STR(x) sb_append_cstr(sb, (x) ? (x) : "");
#define ESCAPED(x) sb_append_html_escaped(sb, (x) ? (x) : "");
#include "build/h_to_html/report/index.h"
#undef OUT
#undef STR
#undef ESCAPED

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
    size_t n_rows = 0;
    if (!report_build_html(rep, &html, &n_rows)) {
        sb_free(html);
        serve_error(sc, 500);
        return;
    }
    size_t len = 0;
    void *bytes = report_convert(rep, n_rows, (const char *)html.items, ext,
                                 &len);
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
