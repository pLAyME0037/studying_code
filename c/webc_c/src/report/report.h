#ifndef CORE_REPORT_H_
#define CORE_REPORT_H_

#include "src/http/serve.h"

// How a result column is printed. TEXT is verbatim; NUM/MONEY are treated as
// numbers (right-aligned, thousands-grouped; MONEY keeps 2 decimals) and are
// summed into a totals row; DATE humanizes an ISO stamp ("05 Oct 2026, 11:56").
typedef enum {
    REPORT_COL_TEXT,
    REPORT_COL_NUM,
    REPORT_COL_MONEY,
    REPORT_COL_DATE,
} Report_Col_Type;

// One downloadable report: SQL runs verbatim (sqlite canonical -- the
// MVP queries are ANSI or view-backed), `headers` labels the result
// columns in Khmer (extra result columns fall back to index labels).
typedef struct {
    const char        *id;           // url segment: sales_summary -> /reports/sales_summary.pdf
    const char        *khmer_title;
    const char        *sql;
    const char *const *headers;
    size_t             header_count;
    const Report_Col_Type *col_types; // parallel to headers; NULL => all TEXT

    // Running page chrome, printed on EVERY page via an ODF page-style
    // header/footer (injected into the FODT at convert time). Each band is a
    // 3-column table: left / middle / right. NULL left/mid fall back to the
    // shop name / report title (header) and the row count (footer); a NULL
    // right header falls back to the generation timestamp. `rfooter` is
    // injected as ODF MARKUP (not escaped text) because it embeds the live
    // PAGE_CURR/PAGE_LAST page fields below.
    const char        *lheader;
    const char        *mheader;
    const char        *rheader;
    const char        *lfooter;
    const char        *mfooter;
    const char        *rfooter;
} Report_Def;

extern const Report_Def report_registry[];
extern const size_t     report_registry_count;

const Report_Def *report_find(const char *id);

// Build the print source table: UTF-8 HTML with @page A4 + Khmer fonts (the
// spike picked HTML->soffice over FODT: A4 honored, NotoSansKhmer embedded,
// ~1s, table fidelity ok). Emits ONLY the data table -- the running header/
// footer live in the page style, added during conversion. Writes the row
// count to `*n_rows` (used for the footer). Returns false on db failure.
bool report_build_html(const Report_Def *rep, String_Builder *out,
                       size_t *n_rows);

// Run LibreOffice headless on `html` (NUL-terminated), return malloc'd
// file bytes (`pdf` or `docx`) or NULL on any failure (timeout, crash,
// missing output -- logged; caller answers 502). Both formats route through
// FODT so the injected page-style header/footer + live page numbers survive;
// `rep`/`n_rows` supply the chrome text. No server hang: the child is
// wrapped in `timeout`.
void *report_convert(const Report_Def *rep, size_t n_rows, const char *html,
                     const char *ext, size_t *out_len);

// HTTP handlers
void serve_reports_index(Serve_Context *sc);
void serve_report_download(Serve_Context *sc);

#endif // CORE_REPORT_H_
