#ifndef POS_ROLES_H_
#define POS_ROLES_H_

#include "core/display/master_child.h"

/* /pos/roles: role_code/name + org FK + description, with
 * role_permissions and user_roles child tabs (both link shapes are
 * view-local here; user_roles handlers are shared with /pos/users). */
void serve_pos_roles(Serve_Context *sc);
void serve_pos_roles_create(Serve_Context *sc);
void serve_pos_roles_update(Serve_Context *sc);
void serve_pos_roles_delete(Serve_Context *sc);
void serve_pos_roles_restore(Serve_Context *sc);

extern MD_Column md_roles_columns[];
extern const size_t md_roles_columns_count;

#endif // !POS_ROLES_H_
