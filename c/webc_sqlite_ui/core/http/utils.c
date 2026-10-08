#include <stddef.h>
#include <time.h>
#include <unistd.h>

#include "module/nob.h"
#include "id.h"
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

// ---------------------------------------------------------------------------
// Multipart/form-data support
// ---------------------------------------------------------------------------

// Case-insensitive ASCII comparison of a String_View against a cstr.
static bool sv_ieq_cstr(String_View a, const char *b) {
    size_t n = strlen(b);
    if (a.count != n) return false;
    for (size_t i = 0; i < n; ++i) {
        char x = a.data[i], y = b[i];
        if ('A' <= x && x <= 'Z') x = (char)(x + ('a' - 'A'));
        if ('A' <= y && y <= 'Z') y = (char)(y + ('a' - 'A'));
        if (x != y) return false;
    }
    return true;
}

static bool sv_starts_with_ci(String_View a, const char *prefix) {
    size_t n = strlen(prefix);
    if (a.count < n) return false;
    return sv_ieq_cstr(sv_from_parts(a.data, n), prefix);
}

static bool find_sv(String_View hay, String_View needle, size_t from, size_t *idx) {
    if (needle.count == 0) return false;
    for (size_t i = from; i + needle.count <= hay.count; ++i) {
        if (memcmp(hay.data + i, needle.data, needle.count) == 0) {
            *idx = i;
            return true;
        }
    }
    return false;
}

// Find a header value (e.g. "Content-Type") in a header block. Works both on
// the raw request head and on the headers of a single multipart part.
static String_View header_find(String_View headers, const char *name) {
    String_View rest = headers;
    while (rest.count > 0) {
        String_View line = sv_chop_by_delim(&rest, '\n');
        if (line.count > 0 && line.data[line.count - 1] == '\r') line.count -= 1;
        String_View value = line;
        String_View hname = sv_chop_by_delim(&value, ':');
        if (sv_ieq_cstr(hname, name)) return sv_trim(value);
    }
    return (String_View) {0};
}

// Extract `param="value"` (or an unquoted value) from a Content-Disposition
// value. Distinguishes absence ({0}, data == NULL) from an empty value.
static String_View disposition_param(String_View disp, const char *param) {
    size_t n = strlen(param);
    for (size_t i = 0; i + n + 1 <= disp.count; ++i) {
        if (i > 0) {
            char prev = disp.data[i - 1];
            if (prev != ';' && prev != ' ' && prev != '\t') continue;
        }
        if (!sv_ieq_cstr(sv_from_parts(disp.data + i, n), param)) continue;
        if (disp.data[i + n] != '=') continue;
        size_t p = i + n + 1;
        while (p < disp.count && (disp.data[p] == ' ' || disp.data[p] == '\t')) ++p;
        if (p < disp.count && disp.data[p] == '"') {
            size_t start = ++p;
            while (p < disp.count && disp.data[p] != '"') ++p;
            return sv_from_parts(disp.data + start, p - start);
        }
        size_t start = p;
        while (p < disp.count && disp.data[p] != ';') ++p;
        return sv_trim(sv_from_parts(disp.data + start, p - start));
    }
    return (String_View) {0};
}

static String_View multipart_boundary(String_View content_type) {
    String_View rest = content_type;
    while (rest.count > 0) {
        String_View param = sv_trim(sv_chop_by_delim(&rest, ';'));
        String_View pname = sv_chop_by_delim(&param, '=');
        if (sv_ieq_cstr(sv_trim(pname), "boundary")) {
            String_View v = sv_trim(param);
            if (v.count >= 2 && v.data[0] == '"') {
                v.data += 1;
                v.count -= 1;
                if (v.data[v.count - 1] == '"') v.count -= 1;
            }
            return v;
        }
    }
    return (String_View) {0};
}

static bool multipart_find(String_View body, String_View boundary, const char *key, Form_Field *out) {
    char delim_buf[160];
    if (boundary.count + 2 > sizeof(delim_buf)) return false;
    delim_buf[0] = '-';
    delim_buf[1] = '-';
    memcpy(delim_buf + 2, boundary.data, boundary.count);
    String_View delim = sv_from_parts(delim_buf, boundary.count + 2);

    size_t pos = 0;
    while (pos < body.count) {
        size_t d;
        if (!find_sv(body, delim, pos, &d)) return false;
        size_t p = d + delim.count;

        if (p + 1 < body.count && body.data[p] == '-' && body.data[p + 1] == '-') return false;  // epilogue
        if (!(p + 1 < body.count && body.data[p] == '\r' && body.data[p + 1] == '\n')) return false;
        p += 2;

        size_t he;
        if (!find_sv(body, sv_from_cstr("\r\n\r\n"), p, &he)) return false;
        String_View part_headers = sv_from_parts(body.data + p, he - p);
        size_t content_start = he + 4;

        size_t next;
        if (!find_sv(body, delim, content_start, &next)) return false;
        String_View content = sv_from_parts(body.data + content_start, next - content_start);
        if (content.count >= 2) content.count -= 2;  // CRLF that belongs to the delimiter

        String_View disp = header_find(part_headers, "Content-Disposition");
        String_View name = disposition_param(disp, "name");
        if (sv_ieq_cstr(name, key)) {
            String_View filename = disposition_param(disp, "filename");
            out->value = content;
            if (filename.data != NULL) {
                out->kind = content.count > 0 ? FIELD_FILE : FIELD_FILE_EMPTY;
                if (out->kind == FIELD_FILE_EMPTY) out->value = (String_View) {0};
            } else {
                out->kind = FIELD_TEXT;
            }
            return true;
        }
        pos = next;  // re-scan from the boundary that terminated this part;
                     // it starts the next one (find_sv matches at `pos` itself)
    }
    return false;
}

bool form_get(String_View request, String_View body, const char *key, Form_Field *out) {
    // Only look at the header section of the raw request.
    String_View headers = request;
    size_t head_end = 0;
    if (find_sv(request, sv_from_cstr("\r\n\r\n"), 0, &head_end)) headers.count = head_end;

    String_View content_type = header_find(headers, "Content-Type");
    out->value = (String_View) {0};
    out->kind = FIELD_TEXT;

    if (sv_starts_with_ci(content_type, "multipart/form-data")) {
        String_View boundary = multipart_boundary(content_type);
        if (boundary.count == 0) return false;
        return multipart_find(body, boundary, key, out);
    }
    return form_find(body, key, &out->value);
}

// ---------------------------------------------------------------------------
// Image uploads
// ---------------------------------------------------------------------------

static bool looks_like_image(String_View v) {
    const unsigned char *d = (const unsigned char *)v.data;
    if (v.count >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return true;
    if (v.count >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return true;
    if (v.count >= 4 && memcmp(d, "GIF8", 4) == 0) return true;
    if (v.count >= 12 && memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "WEBP", 4) == 0) return true;
    return false;
}

Upload_Result save_uploaded_image(String_View *value) {
    if (value->count > MAX_UPLOAD_IMAGE_SIZE) return UPLOAD_TOO_LARGE;
    if (!looks_like_image(*value)) return UPLOAD_NOT_IMAGE;
    if (!mkdir_if_not_exists(UPLOAD_DIR)) return UPLOAD_IO_ERROR;

    static unsigned long long seq = 0;
    seq += 1;
    unsigned long long stamp = (unsigned long long)time(NULL) ^ ((unsigned long long)getpid() << 32);
    const char *tmp_path = temp_sprintf("%s/.%llu_%llu.tmp", UPLOAD_DIR, stamp, seq);
    const char *out_name = temp_sprintf("%llu_%llu.webp", stamp, seq);
    const char *out_path = temp_sprintf("%s/%s", UPLOAD_DIR, out_name);

    if (!write_entire_file(tmp_path, value->data, value->count)) return UPLOAD_IO_ERROR;

    // Shrink to at most 512px and recompress as webp (keeps transparency).
    Cmd cmd = {0};
    cmd_append(&cmd, "ffmpeg", "-nostdin", "-y", "-loglevel", "error",
               "-i", tmp_path,
               "-vf", "scale='min(512,iw)':-2",
               "-frames:v", "1",
               "-c:v", "libwebp", "-quality", "80", "-compression_level", "6",
               out_path);
    bool ok = cmd_run_sync(cmd);
    da_free(cmd);

    delete_file(tmp_path);

    if (!ok) {
        if (file_exists(out_path) > 0) delete_file(out_path);
        return UPLOAD_NOT_IMAGE;
    }
    *value = sv_from_cstr(temp_sprintf(UPLOAD_URL "/%s", out_name));
    return UPLOAD_OK;
}

bool route_id_parse(String_View seg, Route_Id *out)
{
    if (seg.count == 0 || seg.count >= 128) return false;

    Route_Id id = { .kind = ID_STRING, .raw = seg };

    // All-digits (max 18 so it fits long long without overflow) -> ID_INT.
    long long value = 0;
    size_t digits = 0;
    bool is_int = true;
    for (size_t i = 0; i < seg.count; ++i) {
        char ch = seg.data[i];
        if (ch < '0' || ch > '9') { is_int = false; break; }
        if (++digits > 18) { is_int = false; break; }
        value = value * 10 + (ch - '0');
    }
    if (is_int) {
        id.kind = ID_INT;
        id.value = value;
    }

    if (out) *out = id;
    return true;
}
