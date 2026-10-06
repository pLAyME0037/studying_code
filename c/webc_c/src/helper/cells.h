#ifndef HELPER_CELLS_H_
#define HELPER_CELLS_H_

#include "../../core/display/master_child.h"

// =========================================================================
// Composite column cell renderers (master_child engine). One column with
// an MD_Cell renders as ONE <td>; these helpers build its body HTML.
// Strings are temp-arena owned (escape first, never free).
// =========================================================================

/* HTML body for a composite cell: values[] at slot, part_count slots. */
const char *md_cell_render(const MD_Cell *cell, char *const *values, size_t slot);

/* Label for edit/create inputs of part `part` (part_labels, else parts). */
const char *md_cell_part_label(const MD_Cell *cell, size_t part);

/* ISO stamp -> "05 Oct 2026, 11:56" (non-ISO passes through). */
const char *md_date_human(const char *iso);

/* ISO stamp -> "YYYY-MM-DD" for <input type="date"> prefills. */
const char *md_date_input(const char *iso);

#endif // !HELPER_CELLS_H_
