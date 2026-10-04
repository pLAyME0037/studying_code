#ifndef POS_STAFF_H_
#define POS_STAFF_H_

#include "core/display/master_child.h"

/* /pos/staff: staff master (code + name cell + user/org/location FKs)
 * with a cash_shifts child tab. Two extra child shapes live here because
 * the staff module owns every staff column layout:
 *   md_staff_child_columns      - staff under /pos/users   (fk user_id)
 *   md_staff_child_org_columns  - staff under /pos/org     (fk org_unit_id)
 * The fields set is the 4 keys present in all three form bodies;
 * user_id/org_unit_id are opts (body first, query fallback). */
void serve_pos_staff(Serve_Context *sc);
void serve_pos_staff_create(Serve_Context *sc);
void serve_pos_staff_update(Serve_Context *sc);
void serve_pos_staff_delete(Serve_Context *sc);
void serve_pos_staff_restore(Serve_Context *sc);

extern MD_Column md_staff_columns[];
extern const size_t md_staff_columns_count;
extern MD_Column md_staff_child_columns[];
extern const size_t md_staff_child_columns_count;
extern MD_Column md_staff_child_org_columns[];
extern const size_t md_staff_child_org_columns_count;

#endif // !POS_STAFF_H_
