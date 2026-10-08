#ifndef POS_GEO_H_
#define POS_GEO_H_

#include "core/display/master_child.h"

/* Phase 16 geo redo: the four Cambodia admin-area masters seeded from the
 * cam-geo locations.json export (src/pos/geo.c). Provinces/districts/
 * communes carry the full soft-delete + trash flow, villages is a
 * read-only catalog. */
void serve_pos_provinces(Serve_Context *sc);
void serve_pos_provinces_create(Serve_Context *sc);
void serve_pos_provinces_update(Serve_Context *sc);
void serve_pos_provinces_delete(Serve_Context *sc);
void serve_pos_provinces_restore(Serve_Context *sc);

void serve_pos_districts(Serve_Context *sc);
void serve_pos_districts_create(Serve_Context *sc);
void serve_pos_districts_update(Serve_Context *sc);
void serve_pos_districts_delete(Serve_Context *sc);
void serve_pos_districts_restore(Serve_Context *sc);

void serve_pos_communes(Serve_Context *sc);
void serve_pos_communes_create(Serve_Context *sc);
void serve_pos_communes_update(Serve_Context *sc);
void serve_pos_communes_delete(Serve_Context *sc);
void serve_pos_communes_restore(Serve_Context *sc);

void serve_pos_villages(Serve_Context *sc);   /* read_only: GET route only */

extern MD_Column md_provinces_columns[];
extern const size_t md_provinces_columns_count;
extern MD_Column md_districts_columns[];
extern const size_t md_districts_columns_count;
extern MD_Column md_communes_columns[];
extern const size_t md_communes_columns_count;
extern MD_Column md_villages_columns[];
extern const size_t md_villages_columns_count;

#endif // !POS_GEO_H_
