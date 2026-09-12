// template_runtime.h
#pragma once

#include <string.h>

// All helper macros defined ONCE for all pages
#define OUT(buf, size)       sb_append_buf(sb, buf, size)
#define STR(x)               sb_append_cstr(sb, (x) ? (x) : "")
#define INT(x)               sb_appendf(sb, "%d", (x))
#define CLS(cond, t, f)      sb_append_cstr(sb, (cond) ? (t) : (f))
#define NAV_ACTIVE(prefix)   (strncmp((current_path), (prefix), strlen(prefix)) == 0)
#define ESCAPED(x)           sb_append_html_escaped(sb, (x) ? (x) : "")
#define CURRENT_PATH         current_path
#define PAGE_TITLE           page_title

// Layout helpers that create the expected scope variables
#define PAGE_BEGIN(sc, title, route)                             \
    String_Builder *sb = &(sc)->body;                            \
    const char *page_title = (title);                            \
    const char *current_path = (route);                          \
    render_page_header(sb, page_title, current_path)

#define PAGE_END(sc)                                             \
    render_page_footer(&(sc)->body)
