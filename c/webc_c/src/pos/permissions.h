#ifndef POS_PERMISSIONS_H_
#define POS_PERMISSIONS_H_

#include "core/display/master_child.h"

/* /pos/permissions: master-only page for the permissions catalogue
 * (perm_code/perm_name/module_name; no children). */
void serve_pos_permissions(Serve_Context *sc);
void serve_pos_permissions_create(Serve_Context *sc);
void serve_pos_permissions_update(Serve_Context *sc);
void serve_pos_permissions_delete(Serve_Context *sc);
void serve_pos_permissions_restore(Serve_Context *sc);

extern MD_Column md_permissions_columns[];
extern const size_t md_permissions_columns_count;

#endif // !POS_PERMISSIONS_H_
