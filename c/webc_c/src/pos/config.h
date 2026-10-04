#ifndef POS_CONFIG_H_
#define POS_CONFIG_H_

#include "core/display/master_child.h"

/* /pos/config: system_configs, master-only (config_key/value + JSON
 * payload + is_encrypted flag). */
void serve_pos_config(Serve_Context *sc);
void serve_pos_config_create(Serve_Context *sc);
void serve_pos_config_update(Serve_Context *sc);
void serve_pos_config_delete(Serve_Context *sc);
void serve_pos_config_restore(Serve_Context *sc);

extern MD_Column md_configs_columns[];
extern const size_t md_configs_columns_count;

#endif // !POS_CONFIG_H_
