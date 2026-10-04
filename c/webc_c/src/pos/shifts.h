#ifndef POS_SHIFTS_H_
#define POS_SHIFTS_H_

#include "core/display/master_child.h"

/* /pos/shifts: cash_shifts master-only page (no children). opened_at
 * rides its column default (form-less); closing happens via the cash
 * cell + status column. */
void serve_pos_shifts(Serve_Context *sc);
void serve_pos_shifts_create(Serve_Context *sc);
void serve_pos_shifts_update(Serve_Context *sc);
void serve_pos_shifts_delete(Serve_Context *sc);
void serve_pos_shifts_restore(Serve_Context *sc);

extern MD_Column md_shifts_columns[];
extern const size_t md_shifts_columns_count;

#endif // !POS_SHIFTS_H_
