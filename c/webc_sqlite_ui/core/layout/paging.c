#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "paging.h"
#include "core/http/utils.h"

// Copy a query value into a NUL-terminated scratch buffer and parse it as
// an unsigned long. Returns false when the value is absent, empty or not a
// plain number (caller keeps its default).
static bool page_query_size(String_View query, const char *key, size_t *out) {
    String_View v = {0};
    if (!form_find(query, key, &v) || v.count == 0) return false;
    char buf[32];
    size_t n = v.count < sizeof(buf) - 1 ? v.count : sizeof(buf) - 1;
    memcpy(buf, v.data, n);
    buf[n] = '\0';
    char *end = NULL;
    unsigned long val = strtoul(buf, &end, 10);
    if (end == buf || *end != '\0') return false;
    *out = (size_t)val;
    return true;
}

void page_info_parse(String_View query, const char *page_key, Page_Info *out) {
    out->page = 1;
    out->per_page = PAGE_DEFAULT_PER_PAGE;
    size_t v = 0;
    if (page_query_size(query, page_key, &v) && v >= 1) out->page = v;
    if (page_query_size(query, "per_page", &v) && v >= 1) {
        out->per_page = v <= PAGE_MAX_PER_PAGE ? v : PAGE_MAX_PER_PAGE;
    }
}

void page_info_finish(Page_Info *out, size_t total) {
    out->total = total;
    out->total_pages = (total + out->per_page - 1) / out->per_page;
    if (out->total_pages < 1) out->total_pages = 1;
    if (out->page < 1) out->page = 1;
    if (out->page > out->total_pages) out->page = out->total_pages;
    out->offset = (out->page - 1) * out->per_page;
}

bool page_fragment_requested(String_View query) {
    String_View v = {0};
    return form_find(query, "fragment", &v)
        && v.count == 3 && memcmp(v.data, "all", 3) == 0;
}

void page_href(String_Builder *sb,
               const char     *base,
               String_View     query,
               const char     *page_key,
               size_t          page,
               size_t          per_page)
{
    size_t key_len = strlen(page_key);
    char num[32];

    sb_append_cstr(sb, base);
    char sep = '?';
    String_View rest = query;
    while (rest.count > 0) {
        const char *amp  = memchr(rest.data, '&', rest.count);
        size_t plen      = amp ? (size_t)(amp - rest.data) : rest.count;
        String_View pair = { .data = rest.data, .count = plen };
        rest.data  += plen + (amp ? 1 : 0);
        rest.count -= plen + (amp ? 1 : 0);
        if (pair.count == 0) continue;

        const char *eq = memchr(pair.data, '=', pair.count);
        String_View name = {
            .data  = pair.data,
            .count = eq ? (size_t)(eq - pair.data) : pair.count,
        };
        // this pair is replaced below
        if (name.count == key_len && memcmp(name.data, page_key, key_len) == 0) continue;
        if (name.count == 8 && memcmp(name.data, "per_page", 8) == 0) continue;

        sb_append_buf(sb, &sep, 1);
        sep = '&';
        sb_append_sv(sb, pair);
    }

    sb_append_buf(sb, &sep, 1);
    sb_append_cstr(sb, page_key);
    sb_append_cstr(sb, "=");
    snprintf(num, sizeof(num), "%zu", page);
    sb_append_cstr(sb, num);
    sb_append_cstr(sb, "&per_page=");
    snprintf(num, sizeof(num), "%zu", per_page);
    sb_append_cstr(sb, num);
}
