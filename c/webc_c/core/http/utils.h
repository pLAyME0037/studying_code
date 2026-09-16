#ifndef HTTP_UTILS
#define HTTP_UTILS

#include "module/nob.h"

bool form_find(String_View body, const char *key, String_View *out);
bool parse_id_from_uri(Nob_String_View  uri,
                       const char      *prefix,
                       const char      *suffix,
                       int             *id);
#endif // !HTTP_UTILS
