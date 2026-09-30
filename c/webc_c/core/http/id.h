#ifndef CORE_HTTP_ID_H_
#define CORE_HTTP_ID_H_

#include "module/nob.h"

// Route/resource id carried through the request. `raw` is always the exact
// segment as it appeared (uri slice or JSON body string); `value` is set when
// the segment is all digits, so INTEGER-backed tables can reject uuids while
// TEXT-backed tables use `raw` for both legacy numeric ids and uuid defaults.
typedef enum {
    ID_NONE = 0,  // never parsed
    ID_INT,       // all-digits segment (fits long long, <= 18 digits)
    ID_STRING,    // anything else (uuid, hex, ...)
} Id_Kind;

typedef struct {
    Id_Kind     kind;
    String_View raw;
    long long   value;  // valid iff kind == ID_INT
} Route_Id;

// Validates + classifies an id segment. Empty or >= 128 chars -> false.
bool route_id_parse(String_View seg, Route_Id *out);

#endif  // CORE_HTTP_ID_H_
