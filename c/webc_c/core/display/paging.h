#ifndef CORE_DISPLAY_PAGING_H_
#define CORE_DISPLAY_PAGING_H_

#include "module/nob.h"
#include <stdbool.h>
#include <stddef.h>

// =========================================================================
// Global pagination interface.
//
// One struct + four plain functions, table-agnostic and shared by every
// paged list (/users, /notes, /people master table and child tabs). The
// loaders get a window (or NULL for "load everything"), the renderer gets
// totals; the pager markup lives in display/component/pagination.h.tt.
//
// Query keys:
//   ?page=N        1-based page. Default 1, garbage -> 1, clamped to the
//                  last page once the total is known.
//   ?per_page=M    rows per window. The user value: default 20, clamped
//                  1..100, carried through every pager link.
//   ?fragment=all  JS prefetch mode: bare rows + one pager <template> per
//                  page, no page shell (js/PaginationSwitcher.js).
//
// Per-list page keys are "<table>_page" where lists share a page (child
// tabs on /people); standalone lists use plain "page".
// =========================================================================

#define PAGE_DEFAULT_PER_PAGE 20
#define PAGE_MAX_PER_PAGE     100

typedef struct {
    size_t page;        // 1-based, clamped to [1, total_pages]
    size_t per_page;    // clamped to [1, PAGE_MAX_PER_PAGE]
    size_t offset;      // (page-1)*per_page, set by page_info_finish()
    size_t total;       // row count, set by page_info_finish()
    size_t total_pages; // ceil(total/per_page), always >= 1
} Page_Info;

// Parse ?<page_key> and ?per_page out of `query` (garbage -> defaults).
void page_info_parse(String_View query, const char *page_key, Page_Info *out);

// Call once the row count is known: fills total/total_pages/offset and
// clamps page into range. Call BEFORE loading when the count comes from a
// count(*) query (the window drives SQL LIMIT/OFFSET); call AFTER loading
// all rows when rows.count IS the total.
void page_info_finish(Page_Info *out, size_t total);

// True for the JS prefetch request (?fragment=all).
bool page_fragment_requested(String_View query);

// Append `base?<page_key>=<page>&per_page=<M>` to sb, keeping every other
// pair already present in `query` (user_id, redirect, ...). Pair bytes are
// copied verbatim - the query string is already url-encoded.
void page_href(String_Builder *sb, const char *base, String_View query,
               const char *page_key, size_t page, size_t per_page);

#endif // CORE_DISPLAY_PAGING_H_
