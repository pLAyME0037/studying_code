#include <stddef.h>

#include "module/nob.h"
#include "utils.h"

static int hex_to_int(char c) {
    if ('0' <= c && c <= '9') return c - '0';
    if ('a' <= c && c <= 'f') return 10 + (c - 'a');
    if ('A' <= c && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static String_View url_decode(String_View encoded) {
    char *out = (char *)nob_temp_alloc(encoded.count + 1);
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
    return (String_View) { .data = out, .count = j };
}

bool form_find(String_View body, const char *key, String_View *out) {
    size_t key_len = strlen(key);
    String_View rest = body;
    while (rest.count > 0) {
        String_View pair = sv_chop_by_delim(&rest, '&');
        String_View name = sv_chop_by_delim(&pair, '=');
        if (name.count == key_len && memcmp(name.data, key, key_len) == 0) {
            if (out) *out = url_decode(pair);
            return true;
        }
    }
    return false;
}

bool parse_id_from_uri(Nob_String_View  uri,
                       const char      *prefix,
                       const char      *suffix,
                       int             *id)
{
    size_t plen = strlen(prefix);
    size_t slen = strlen(suffix);

    // Normalize prefix: ensure it ends with '/' (like parse_uri_id does)
    bool prefix_has_slash = (plen > 0 && prefix[plen - 1] == '/');
    size_t effective_plen = prefix_has_slash ? plen : plen + 1;

    if (uri.count < effective_plen + slen) return false;
    if (memcmp(uri.data, prefix, plen) != 0) return false;
    if (!prefix_has_slash) {
        if (uri.data[plen] != '/') return false;
    }
    if (!nob_sv_ends_with(uri, nob_sv_from_cstr(suffix))) return false;

    Nob_String_View id_sv = {
        .data  = uri.data + effective_plen,
        .count = uri.count - effective_plen - slen,
    };

    if (id_sv.count == 0 || id_sv.count >= 32) return false;

    char buf[32] = {0};
    memcpy(buf, id_sv.data, id_sv.count);
    buf[id_sv.count] = '\0';

    char *end = NULL;
    long value = strtol(buf, &end, 10);
    if (end == buf || *end != '\0' || value < 0) return false;

    if (id) *id = (int)value;
    return true;
}
