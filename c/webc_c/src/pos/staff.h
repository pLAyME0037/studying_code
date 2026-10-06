#ifndef POS_STAFF_H_
#define POS_STAFF_H_

#include "core/display/master_child.h"

/* /pos/staff: staff master (code + name cell + user/org/location FKs +
 * phone/hire_date/staff_type) with a cash_shifts child tab. Staff is a
 * master table (Phase 13): the former staff child tabs under /pos/users
 * and /pos/org are gone - users/org show their own FK columns instead. */
void serve_pos_staff(Serve_Context *sc);
void serve_pos_staff_create(Serve_Context *sc);
void serve_pos_staff_update(Serve_Context *sc);
void serve_pos_staff_delete(Serve_Context *sc);
void serve_pos_staff_restore(Serve_Context *sc);

extern MD_Column md_staff_columns[];
extern const size_t md_staff_columns_count;

#endif // !POS_STAFF_H_
