#ifndef CORE_SERVE_H_
#define CORE_SERVE_H_

#include <stdbool.h>
#include "../nob.h"

typedef struct {
    int client_fd;
    String_Builder request;
    String_Builder response;
    String_Builder body;
} Serve_Context;

void sc_reset(Serve_Context *sc);
void serve_request(Serve_Context *sc);

const char *http_reason_phrase_by_status_code(int status_code);
void http_render_response(Serve_Context *sc, int status_code, const char *content_type, String_View body);
void http_render_redirect(Serve_Context *sc, int status_code, const char *location);
void render_page_shell(Serve_Context *sc, String_View title, String_View content);
void render_page_header(String_Builder *sb, const char *page_title, const char *current_path);
void render_page_footer(String_Builder *sb);
void serve_error(Serve_Context *sc, int status_code);
void serve_resource(Serve_Context *sc, const char *resource_path, const char *content_type);
void serve_ok(Serve_Context *sc);
void sb_append_html_escaped(String_Builder *sb, const char *s);
void sb_append_json_escaped(String_Builder *sb, const char *s);
bool json_find_string(String_View body, const char *key, String_View *out);
bool json_find_int(String_View body, const char *key, long long *out);

#endif // CORE_SERVE_H_
