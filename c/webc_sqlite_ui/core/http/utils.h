#ifndef HTTP_UTILS
#define HTTP_UTILS

#include "module/nob.h"

bool form_find(String_View body, const char *key, String_View *out);

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
