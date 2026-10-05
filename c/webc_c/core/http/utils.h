#ifndef HTTP_UTILS
#define HTTP_UTILS

#include "module/nob.h"

bool form_find(String_View body, const char *key, String_View *out);

// Random RFC-4122-shaped id (v4 bits set) into `out` (36 chars + NUL).
// Reads /dev/urandom, falling back to a time/counter mix (demo-grade).
// Needed where the insert must know the id up front - session cookies and
// checkout rows - because reading a SQL DEFAULT id back is not portable
// across the dialects.
bool webc_uuid(char out[37]);

// What kind of value a form submission carried for a field.
typedef enum {
    FIELD_TEXT,        // regular text input (already url-decoded for
                       // application/x-www-form-urlencoded submissions)
    FIELD_FILE,        // <input type="file"> with data attached
    FIELD_FILE_EMPTY,  // <input type="file"> submitted without choosing a file
} Field_Kind;

typedef struct {
    String_View value;  // field bytes; {0} when nothing was provided
    Field_Kind  kind;
} Form_Field;

// Content-Type aware form field extraction. `request` is the raw request
// (headers + body), `body` is the body alone. Understands both
// application/x-www-form-urlencoded and multipart/form-data submissions.
bool form_get(String_View request, String_View body, const char *key, Form_Field *out);

// Convenience wrapper for plain text fields: returns the value, or an empty
// view when the field is missing / not a text field.
static inline String_View form_text(String_View request, String_View body,
                                    const char *key) {
    Form_Field f = {0};
    form_get(request, body, key, &f);
    return f.kind == FIELD_TEXT ? f.value : (String_View) {0};
}

typedef enum {
    UPLOAD_OK,
    UPLOAD_TOO_LARGE,   // exceeds MAX_UPLOAD_IMAGE_SIZE
    UPLOAD_NOT_IMAGE,   // not a png/jpeg/gif/webp, or ffmpeg could not decode it
    UPLOAD_IO_ERROR,    // could not create/write the upload directory
} Upload_Result;

#define MAX_UPLOAD_IMAGE_SIZE (2 * 1024 * 1024)  // 2 MB
#define UPLOAD_DIR  "./resource/image/upload"
#define UPLOAD_URL  "/resource/image/upload"

// Compresses an uploaded image with ffmpeg (webp, max 512px) into UPLOAD_DIR
// and replaces *value with the URL it will be served from.
Upload_Result save_uploaded_image(String_View *value);

#endif // !HTTP_UTILS
