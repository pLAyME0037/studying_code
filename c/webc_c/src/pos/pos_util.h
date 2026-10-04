#ifndef POS_UTIL_H_
#define POS_UTIL_H_

#include "src/db/db.h"

/* INSERT binders shared by the POS modules. Form values arrive as text
 * views; "" must not leak into typed or nullable columns.
 *   pos_sv  - nullable text/FK: empty -> SQL NULL (a cleared select sends
 *             "" for "None"; a missing opt field keeps data == NULL and
 *             sql_bind already turns that into SQL NULL).
 *   pos_num - NOT NULL numeric: empty -> 0 (the column default).
 * UPDATE statements never use these: they COALESCE(NULLIF(?,''), col) so
 * both "missing" (a child edit form that does not carry the column) and
 * "cleared" keep the stored value. */
static inline sql_val pos_sv(String_View sv) {
    return sv.count ? SQL_SV(sv) : SQL_NIL();
}

static inline sql_val pos_num(String_View sv) {
    return sv.count ? SQL_SV(sv) : SQL_I(0);
}

#endif // !POS_UTIL_H_
