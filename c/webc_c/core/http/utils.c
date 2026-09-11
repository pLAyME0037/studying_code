#include <stddef.h>

#include "../../nob.h"
#include "utils.h"

static int hex_to_int(char c) {
    if ('0' <= c && c <= '9') return c - '0';
    if ('a' <= c && c <= 'f') return 10 + (c - 'a');
    if ('A' <= c && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static char *url_decode(String_View encoded) {
    char *out = nob_temp_sprintf("%.*s", (int)encoded.count, encoded.data);
    size_t j = 0;
    for (size_t i = 0; i < encoded.count; ++i) {
        if (encoded.data[i] == '%' && i + 2 < encoded.count) {
            int hi = hex_to_int(encoded.data[i + 1]);
            int lo = hex_to_int(encoded.data[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out[j++] = (char)(hi * 16 + lo);
                i += 2;
                continue;
            }
        }
        if (encoded.data[i] == '+') {
            out[j++] = ' ';
            continue;
        }
        out[j++] = encoded.data[i];
    }
    out[j] = '\0';
    return out;
}

bool form_find(String_View body,
               const char *key,
               char       *out,
               size_t      out_cap)
{
    size_t key_len = strlen(key);
    String_View rest = body;
    while (rest.count > 0) {
        String_View pair = sv_chop_by_delim(&rest, '&');
        String_View name = sv_chop_by_delim(&pair, '=');
        if (name.count == key_len && memcmp(name.data, key, key_len) == 0) {
            char *decoded = url_decode(pair);
            snprintf(out, out_cap, "%s", decoded);
            return true;
        }
    }
    return false;
}

