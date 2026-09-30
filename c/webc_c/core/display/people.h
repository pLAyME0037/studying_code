#ifndef PEOPLE_H_
#define PEOPLE_H_

#include "master_child.h"

// /people module: composes the users-master / notes-child view.
// The reusable types + engine live in master_child.h; the column shapes
// live with their entities (user.c, notes.c).

/* Entity-owned column shapes (defined in user.c / notes.c). */
extern MD_Column md_users_columns[];
extern const size_t md_users_columns_count;
extern MD_Column md_notes_columns[];
extern const size_t md_notes_columns_count;

/* GET /people: users as master, their notes as the child tab. */
void serve_people(Serve_Context *sc);

#endif // !PEOPLE_H_
