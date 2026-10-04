#ifndef POS_ROLE_PERMISSIONS_H_
#define POS_ROLE_PERMISSIONS_H_

#include "core/display/master_child.h"

/* role_permissions: child entity of /pos/roles (fk = role_id via the
 * child forms' query string). UNIQUE(role_id, permission_id).
 * Write routes only -- no master page. */
void serve_pos_role_permissions_create(Serve_Context *sc);
void serve_pos_role_permissions_update(Serve_Context *sc);
void serve_pos_role_permissions_delete(Serve_Context *sc);
void serve_pos_role_permissions_restore(Serve_Context *sc);

#endif // !POS_ROLE_PERMISSIONS_H_
