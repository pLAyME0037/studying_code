#define NOB_STRIP_PREFIX
#include "module/nob.h"

#include "cells.h"
#include "core/http/serve.h"

// =========================================================================
// Composite column cell renderers. Two styles (MD_Cell.style):
//   stack  - flex-col lines, color-ranked so province reads strongest and
//            the smallest admin unit fades; empty parts keep their line
//            (&nbsp;) so rows stay the same height.
//   avatar - image (parts[0], a stored URL) + primary (parts[1]) + optional
//            secondary (parts[2]) beside it; empty image -> placeholder box.
// Layout follows the site rules: hierarchy from color, no radius/padding/
// margin nesting in the wrappers themselves.
// =========================================================================

const char *md_cell_part_label(const MD_Cell *cell, size_t part) {
    if (cell->part_labels && cell->part_labels[part]) return cell->part_labels[part];
    return cell->parts[part];
}

static void cell_append_value(String_Builder *sb, const char *v) {
    if (v && *v) sb_append_html_escaped(sb, v);
    else sb_append_cstr(sb, "&nbsp;");
}

static void cell_render_stack(String_Builder *sb, const MD_Cell *cell,
                              char *const *values, size_t slot)
{
    // Color ranks, strongest first (index = part, clamped to the last).
    static const char *const rank[] = {
        "text-gray-900 dark:text-gray-100 font-medium",
        "text-gray-700 dark:text-gray-300",
        "text-gray-500 dark:text-gray-400",
        "text-gray-400 dark:text-gray-500",
    };
    const size_t n = sizeof(rank) / sizeof(rank[0]);
    sb_append_cstr(sb, "<div class=\"flex flex-col leading-tight\">");
    for (size_t p = 0; p < cell->part_count; ++p) {
        sb_append_cstr(sb, temp_sprintf("<span class=\"text-xs %s\">",
                                        rank[p < n ? p : n - 1]));
        cell_append_value(sb, values[slot + p]);
        sb_append_cstr(sb, "</span>");
    }
    sb_append_cstr(sb, "</div>");
}

static void cell_render_avatar(String_Builder *sb, const MD_Cell *cell,
                               char *const *values, size_t slot)
{
    const char *img = cell->part_count > 0 && values[slot] ? values[slot] : "";
    sb_append_cstr(sb, "<div class=\"flex items-center gap-1.5\">");
    if (img[0]) {
        sb_append_cstr(sb, "<img src=\"");
        sb_append_html_escaped(sb, img);
        sb_append_cstr(sb,
            "\" alt=\"\" loading=\"lazy\" "
            "class=\"w-6 h-6 rounded-full object-cover "
            "bg-gray-100 dark:bg-gray-700\">");
    } else {
        sb_append_cstr(sb,
            "<span class=\"w-6 h-6 rounded-full inline-block "
            "bg-gray-200 dark:bg-gray-600\"></span>");
    }
    sb_append_cstr(sb, "<div class=\"flex flex-col leading-tight min-w-0\">");
    sb_append_cstr(sb, "<span class=\"text-sm font-medium text-gray-900 dark:text-gray-100\">");
    if (cell->part_count > 1) cell_append_value(sb, values[slot + 1]);
    sb_append_cstr(sb, "</span>");
    if (cell->part_count > 2) {
        sb_append_cstr(sb, "<span class=\"text-xs text-gray-500 dark:text-gray-400\">");
        cell_append_value(sb, values[slot + 2]);
        sb_append_cstr(sb, "</span>");
    }
    sb_append_cstr(sb, "</div></div>");
}

const char *md_cell_render(const MD_Cell *cell, char *const *values, size_t slot) {
    if (!cell || cell->part_count == 0) return "";
    String_Builder sb = {0};
    if (cell->style && strcmp(cell->style, "avatar") == 0) {
        cell_render_avatar(&sb, cell, values, slot);
    } else {
        cell_render_stack(&sb, cell, values, slot);
    }
    sb_append_null(&sb);
    const char *out = temp_strdup(sb.items);
    sb_free(sb);
    return out;
}
