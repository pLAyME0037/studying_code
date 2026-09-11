#ifndef HTTP_UTILS
#define HTTP_UTILS

#include "../../nob.h"

/* 1 USD = 4000 KHR */
#define WEBC_FX_RATE 4000.0

bool form_find(String_View body,
              const char  *key,
              char        *out,
              size_t       out_cap);
#endif // !HTTP_UTILS
