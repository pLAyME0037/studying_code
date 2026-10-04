#ifndef POS_H_
#define POS_H_

#include "master_child.h"

// =========================================================================
// POS showcase pages (composite columns): /pos/locations (4-part stack
// cell) and /pos/users (avatar cell). Master-child engine renders the
// lists; SERVE_* macros below generate the create/update/delete handlers.
// =========================================================================

void serve_pos_locations(Serve_Context *sc);
void serve_pos_locations_create(Serve_Context *sc);
void serve_pos_locations_update(Serve_Context *sc);
void serve_pos_locations_delete(Serve_Context *sc);
void serve_pos_locations_restore(Serve_Context *sc);

void serve_pos_users(Serve_Context *sc);
void serve_pos_users_create(Serve_Context *sc);
void serve_pos_users_update(Serve_Context *sc);
void serve_pos_users_delete(Serve_Context *sc);
void serve_pos_users_restore(Serve_Context *sc);

/* users view shared by /pos/customers and /pos/org child tabs */
extern MD_Column md_pos_users_columns[];
extern const size_t md_pos_users_columns_count;

/* read_only view: GET route only, no write routes registered */
void serve_pos_dictionaries(Serve_Context *sc);

#endif // !POS_H_
