#ifndef POS_USER_ROLES_H_
#define POS_USER_ROLES_H_

#include "core/display/master_child.h"

/* user_roles: child entity of /pos/users (fk = user_id via the child
 * forms' query string). Surrogate id PK + UNIQUE(user_id, role_id).
 * Write routes only -- no master page. */
void serve_pos_user_roles_create(Serve_Context *sc);
void serve_pos_user_roles_update(Serve_Context *sc);
void serve_pos_user_roles_delete(Serve_Context *sc);
void serve_pos_user_roles_restore(Serve_Context *sc);

/* the same link table from the /pos/roles side (user in body, role in
 * query) -- SERVE_EXTRACT_FIELDS is body-only, so this view needs its
 * own handlers + route prefix */
void serve_pos_role_members_create(Serve_Context *sc);
void serve_pos_role_members_update(Serve_Context *sc);
void serve_pos_role_members_delete(Serve_Context *sc);
void serve_pos_role_members_restore(Serve_Context *sc);

#endif // !POS_USER_ROLES_H_
