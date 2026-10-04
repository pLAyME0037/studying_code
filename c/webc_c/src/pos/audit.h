#ifndef POS_AUDIT_H_
#define POS_AUDIT_H_

#include "core/display/master_child.h"

/* /pos/audit: read_only view of audit_logs -- GET route only, no write
 * handlers registered (see route.c). */
void serve_pos_audit(Serve_Context *sc);

#endif // !POS_AUDIT_H_
