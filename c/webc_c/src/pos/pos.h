#ifndef POS_H_
#define POS_H_

#include "src/layout/master_child.h"

// =========================================================================
// POS showcase pages (composite columns): /pos/users (avatar cell).
// Master-child engine renders the lists; SERVE_* macros below generate the
// create/update/delete handlers. The geo masters live in src/pos/geo.c.
// =========================================================================

void serve_pos_users(Serve_Context *sc);
void serve_pos_users_create(Serve_Context *sc);
void serve_pos_users_update(Serve_Context *sc);
void serve_pos_users_delete(Serve_Context *sc);
void serve_pos_users_soft_delete(Serve_Context *sc);
void serve_pos_users_restore(Serve_Context *sc);

/* users view shared by /pos/customers and /pos/org child tabs */
extern MD_Column md_pos_users_columns[];
extern const size_t md_pos_users_columns_count;

/* create-only view: the add form + POST /create; no row actions */
void serve_pos_dict(Serve_Context *sc);
void serve_pos_dict_create(Serve_Context *sc);

#endif // !POS_H_
