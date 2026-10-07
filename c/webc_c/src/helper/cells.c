#define NOB_STRIP_PREFIX
#include "module/nob.h"

#include <time.h>

#include "cells.h"
#include "core/http/serve.h"

// =========================================================================
// Composite column cell renderers. Three styles (MD_Cell.style):
//   stack    - flex-col lines, color-ranked so province reads strongest and
//              the smallest admin unit fades; empty parts keep their line
//              (&nbsp;) so rows stay the same height.
//   avatar   - image (parts[0]) + primary (parts[1]) + optional secondary
//              (parts[2]) + optional status (parts[3]) that rings the
//              picture (ACTIVE blue / INACTIVE peach / SUSPENDED red) and
//              prints a colored status line; empty image -> initials circle.
//   activity - created/updated/deleted stamps rendered human-readable with
//              a DELETED marker when the row carries deleted_at.
// Layout follows the site rules: hierarchy from color, no radius/padding/
// margin nesting in the wrappers themselves.
// =========================================================================

// "2026-10-05T11:56:45.245Z", "2026-10-05 11:56:45" or "2026-10-05" ->
// "05 Oct 2026, 11:56" (time only when present, seconds dropped). Anything
// that is not an ISO stamp passes through untouched.
//
// created_at/updated_at/deleted_at are stamped in UTC (SQL 'now', column
// defaults, update triggers - the workflow owns them end to end), while
// the shop lives on local wall-clock time (Asia/Phnom_Penh, +07): a value
// WITH a time is a moment and is converted to local before formatting so
// every cell, activity card and report reads the clock the staff does.
// Date-only values (hire date, delivery date) are calendar dates, not
// moments - they pass through unchanged.
const char *md_date_human(const char *iso) {
    if (!iso || !iso[0]) return "";
    int y, mo, d, h = 0, mi = 0;
    int got = sscanf(iso, "%d-%d-%d%*c%d:%d", &y, &mo, &d, &h, &mi);
    if (got < 3 || mo < 1 || mo > 12 || d < 1 || d > 31) return iso;
    if (got >= 5) {
        struct tm utc = {0};
        utc.tm_year = y - 1900;
        utc.tm_mon  = mo - 1;
        utc.tm_mday = d;
        utc.tm_hour = h;
        utc.tm_min  = mi;
        utc.tm_isdst = -1;
        time_t ep = timegm(&utc);
        struct tm loc = {0};
        if (ep != (time_t)-1 && localtime_r(&ep, &loc) != NULL) {
            y  = loc.tm_year + 1900;
            mo = loc.tm_mon + 1;
            d  = loc.tm_mday;
            h  = loc.tm_hour;
            mi = loc.tm_min;
        }
    }
    static const char *const mon[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    if (got >= 5) {
        return temp_sprintf("%02d %s %04d, %02d:%02d",
                            d, mon[mo - 1], y, h, mi);
    }
    return temp_sprintf("%02d %s %04d", d, mon[mo - 1], y);
}

// Value for <input type="date">: browsers reject the full ISO stamp, so
// the edit form sends the date part only. values[] stay raw for handlers.
const char *md_date_input(const char *iso) {
    if (!iso || iso[0] == '\0') return "";
    size_t n = strlen(iso);
    return n < 10 ? iso : temp_sprintf("%.10s", iso);
}

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

// Status -> Catppuccin accent (blue / peach / red) for the avatar ring and
// the status line; unknown or empty status stays neutral.
static const char *status_accent(const char *status) {
    if (status && status[0]) {
        if (strcmp(status, "ACTIVE") == 0)        return "blue";
        if (strcmp(status, "INACTIVE") == 0)      return "peach";
        if (strcmp(status, "SUSPENDED") == 0)      return "red";
    }
    return NULL;
}

static void cell_render_avatar(String_Builder *sb, const MD_Cell *cell,
                               char *const *values, size_t slot)
{
    const char *img = cell->part_count > 0 && values[slot] ? values[slot] : "";
    const char *name = cell->part_count > 1 && values[slot + 1]
                     ? values[slot + 1] : "";
    const char *sub = cell->part_count > 2 && values[slot + 2]
                    ? values[slot + 2] : "";
    const char *status = cell->part_count > 3 && values[slot + 3]
                       ? values[slot + 3] : "";
    const char *accent = status_accent(status);

    // 40px rounded picture, ringed by account status at a glance.
    const char *ring = accent ? temp_sprintf("ring-2 ring-%s", accent) : "";
    sb_append_cstr(sb, "<div class=\"flex items-center gap-2\">");
    if (img[0]) {
        sb_append_cstr(sb, "<img src=\"");
        sb_append_html_escaped(sb, img);
        sb_append_cstr(sb, temp_sprintf(
            "\" alt=\"\" loading=\"lazy\""
            " class=\"h-10 w-10 rounded-full object-cover shrink-0 %s\">",
            ring));
    } else {
        // Initials circle (first UTF-8 char of the name) instead of a
        // gray box: a missing picture still reads as a person.
        char initial[8] = {0};
        if (name[0]) {
            const unsigned char *p = (const unsigned char *) name;
            size_t n = 1;
            if ((p[0] & 0xE0) == 0xC0)      n = 2;
            else if ((p[0] & 0xF0) == 0xE0) n = 3;
            else if ((p[0] & 0xF8) == 0xF0) n = 4;
            else if (p[0] & 0x80)           n = 1;   // invalid lead byte
            memcpy(initial, name, n < sizeof(initial) ? n : sizeof(initial) - 1);
        }
        sb_append_cstr(sb, temp_sprintf(
            "<span class=\"h-10 w-10 rounded-full shrink-0 inline-flex"
            " items-center justify-center text-sm font-semibold"
            " bg-surface1 text-subtext0 %s\">", ring));
        if (initial[0]) sb_append_html_escaped(sb, initial);
        else            sb_append_cstr(sb, "?");
        sb_append_cstr(sb, "</span>");
    }
    sb_append_cstr(sb, "<div class=\"flex flex-col leading-tight min-w-0\">");
    sb_append_cstr(sb,
        "<span class=\"text-sm font-medium text-gray-900 dark:text-gray-100\">");
    cell_append_value(sb, name);
    sb_append_cstr(sb, "</span>");
    if (cell->part_count > 2) {
        sb_append_cstr(sb,
            "<span class=\"text-xs text-gray-500 dark:text-gray-400\">");
        cell_append_value(sb, sub);
        sb_append_cstr(sb, "</span>");
    }
    if (cell->part_count > 3) {
        sb_append_cstr(sb, temp_sprintf(
            "<span class=\"text-[10px] font-semibold uppercase tracking-wide %s\">",
            accent ? temp_sprintf("text-%s", accent) : "text-overlay0"));
        cell_append_value(sb, status);
        sb_append_cstr(sb, "</span>");
    }
    sb_append_cstr(sb, "</div></div>");
}

// created/updated/deleted stamps: human dates, color-ranked, and a red
// DELETED line when deleted_at is set (trash view / cascade victims).
static void cell_render_activity(String_Builder *sb, const MD_Cell *cell,
                                 char *const *values, size_t slot)
{
    const char *created = values[slot];
    const char *updated = cell->part_count > 1 && values[slot + 1]
                        ? values[slot + 1] : "";
    const char *deleted = cell->part_count > 2 && values[slot + 2]
                        ? values[slot + 2] : "";
    sb_append_cstr(sb, "<div class=\"flex flex-col leading-tight text-xs\">");
    bool wrote = false;
    if (created && *created) {
        wrote = true;
        sb_append_cstr(sb, "<span class=\"text-overlay0\">Joined "
                           "<span class=\"text-text font-medium\">");
        sb_append_cstr(sb, md_date_human(created));
        sb_append_cstr(sb, "</span></span>");
    }
    if (updated && *updated && (!created || strcmp(updated, created) != 0)) {
        wrote = true;
        sb_append_cstr(sb, "<span class=\"text-overlay0\">Edited "
                           "<span class=\"text-subtext0\">");
        sb_append_cstr(sb, md_date_human(updated));
        sb_append_cstr(sb, "</span></span>");
    }
    if (deleted && *deleted) {
        wrote = true;
        sb_append_cstr(sb, "<span class=\"text-red font-semibold\">DELETED "
                           "<span class=\"font-normal\">");
        sb_append_cstr(sb, md_date_human(deleted));
        sb_append_cstr(sb, "</span></span>");
    }
    if (!wrote) sb_append_cstr(sb, "&nbsp;");
    sb_append_cstr(sb, "</div>");
}

const char *md_cell_render(const MD_Cell *cell, char *const *values, size_t slot) {
    if (!cell || cell->part_count == 0) return "";
    String_Builder sb = {0};
    if (cell->style && strcmp(cell->style, "avatar") == 0) {
        cell_render_avatar(&sb, cell, values, slot);
    } else if (cell->style && strcmp(cell->style, "activity") == 0) {
        cell_render_activity(&sb, cell, values, slot);
    } else {
        cell_render_stack(&sb, cell, values, slot);
    }
    sb_append_null(&sb);
    const char *out = temp_strdup(sb.items);
    sb_free(sb);
    return out;
}
