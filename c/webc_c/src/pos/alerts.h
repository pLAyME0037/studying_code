#ifndef POS_ALERTS_H_
#define POS_ALERTS_H_

#include "core/display/master_child.h"

/* /pos/alerts: system_alerts master-only (user/order FKs + type + payload
 * + {seen,sent} flags cell). */
void serve_pos_alerts(Serve_Context *sc);
void serve_pos_alerts_create(Serve_Context *sc);
void serve_pos_alerts_update(Serve_Context *sc);
void serve_pos_alerts_delete(Serve_Context *sc);
void serve_pos_alerts_restore(Serve_Context *sc);

extern MD_Column md_alerts_columns[];
extern const size_t md_alerts_columns_count;

#endif // !POS_ALERTS_H_
