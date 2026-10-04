#ifndef POS_ORG_H_
#define POS_ORG_H_

#include "core/display/master_child.h"

/* /pos/org: org_units master (code/name + ORG_TYPE dict FK + self-FK
 * parent) with users and staff child tabs (both shapes come from
 * core/display/pos.h and src/pos/staff.h). */
void serve_pos_org(Serve_Context *sc);
void serve_pos_org_create(Serve_Context *sc);
void serve_pos_org_update(Serve_Context *sc);
void serve_pos_org_delete(Serve_Context *sc);
void serve_pos_org_restore(Serve_Context *sc);

extern MD_Column md_org_columns[];
extern const size_t md_org_columns_count;

#endif // !POS_ORG_H_
