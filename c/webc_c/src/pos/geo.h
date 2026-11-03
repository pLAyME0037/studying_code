#ifndef POS_GEO_H_
#define POS_GEO_H_

#include "src/layout/master_child.h"

void serve_pos_provinces(Serve_Context *sc);
void serve_pos_provinces_create(Serve_Context *sc);
void serve_pos_provinces_update(Serve_Context *sc);
void serve_pos_provinces_soft_delete(Serve_Context *sc);
void serve_pos_provinces_restore(Serve_Context *sc);

void serve_pos_districts(Serve_Context *sc);
void serve_pos_districts_create(Serve_Context *sc);
void serve_pos_districts_update(Serve_Context *sc);
void serve_pos_districts_soft_delete(Serve_Context *sc);
void serve_pos_districts_restore(Serve_Context *sc);

void serve_pos_communes(Serve_Context *sc);
void serve_pos_communes_create(Serve_Context *sc);
void serve_pos_communes_update(Serve_Context *sc);
void serve_pos_communes_soft_delete(Serve_Context *sc);
void serve_pos_communes_restore(Serve_Context *sc);

void serve_pos_villages(Serve_Context *sc);   /* read_only: GET route only */

/* The cascading village picker (province -> district -> commune ->
 * village). md_cascade_json() - declared in master_child.h, so the layout
 * engine can ask for it without including this header - is the compact
 * ancestor dataset embedded once per form; serve_geo_villages_options() is
 * the lazy village half, served from /geo/villages?commune=<id>. */
void serve_geo_villages_options(Serve_Context *sc);
void serve_pos_geo_workspace(Serve_Context *sc);

extern MD_Column md_provinces_columns[];
extern const size_t md_provinces_columns_count;
extern MD_Column md_districts_columns[];
extern const size_t md_districts_columns_count;
extern MD_Column md_communes_columns[];
extern const size_t md_communes_columns_count;
extern MD_Column md_villages_columns[];
extern const size_t md_villages_columns_count;

#endif // !POS_GEO_H_
