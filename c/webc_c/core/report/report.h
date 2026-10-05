#ifndef CORE_REPORT_H_
#define CORE_REPORT_H_

#include "core/http/serve.h"

// One downloadable report: SQL runs verbatim (sqlite canonical -- the
// MVP queries are ANSI or view-backed), `headers` labels the result
// columns in Khmer (extra result columns fall back to index labels).
typedef struct {
    const char        *id;           // url segment: sales_summary -> /reports/sales_summary.pdf
    const char        *khmer_title;
    const char        *sql;
    const char *const *headers;
    size_t             header_count;
    const char        *footer;       // Khmer note printed under the table
} Report_Def;

extern const Report_Def report_registry[];
extern const size_t     report_registry_count;

const Report_Def *report_find(const char *id);

// Build the print source: UTF-8 HTML with @page A4 + Khmer fonts (the
// spike picked HTML->soffice over FODT: A4 honored, NotoSansKhmer
// embedded, ~1s, table fidelity ok). Returns false on db failure.
bool report_build_html(const Report_Def *rep, String_Builder *out);

// Run LibreOffice headless on `html` (NUL-terminated), return malloc'd
// file bytes (`pdf` or `docx`) or NULL on any failure (timeout, crash,
// missing output -- logged; caller answers 502). No server hang: the
// child is wrapped in `timeout`.
void *report_convert(const char *html, const char *ext, size_t *out_len);

// HTTP handlers
void serve_reports_index(Serve_Context *sc);
void serve_report_download(Serve_Context *sc);

#endif // CORE_REPORT_H_
