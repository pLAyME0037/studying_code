OUT("", 0);
 
    // Emits one server-rendered pager per page inside <template> blocks for
    // ?fragment=all responses (js/PaginationSwitcher.js swaps the matching
    // one in when a page link is clicked). Expects the same scope as
    // pagination.h.tt plus a `fragment` flag.
    if (fragment) {
        for (size_t pgt = 1; pgt <= page_info.total_pages; ++pgt) {
            page_info.page = pgt;
            STR(temp_sprintf("<template data-pg-pager=\"%s\" data-pg-n=\"%zu\">",
                             page_container, pgt));
            #include "build/h_to_html/component/pagination.h"
            STR("</template>");
        }
    }

OUT("\x0a", 1);
