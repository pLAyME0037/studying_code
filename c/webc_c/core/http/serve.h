#ifndef CORE_SERVE_H_
#define CORE_SERVE_H_

#include <stdbool.h>
#include "module/nob.h"
#include "id.h"

typedef struct {
    int            client_fd;
    Route_Id       route_id;   // id segment of ROUTE_ID_ACTION URIs (parsed once)
    String_Builder request;
    String_Builder response;
    String_Builder body;
    String_View    method;
    String_View    uri;
    String_View    query_string;
} Serve_Context;

void sc_reset(Serve_Context *sc);
void serve_request(Serve_Context *sc);
void coroutine_server_run(const char *addr, uint16_t port);

const char *http_reason_phrase_by_status_code(int status_code);
void http_render_response(Serve_Context *sc, int status_code, const char *content_type, String_View body);
// As http_render_response, plus a Content-Disposition: attachment header
// (report downloads).
void http_render_response_attachment(Serve_Context *sc, const char *content_type, const char *filename, String_View body);
void http_render_redirect(Serve_Context *sc, int status_code, const char *location);
// Target for post-mutation redirects: a validated ?redirect= path when the
// form supplied one (master-detail page actions), else `fallback`.
const char *http_redirect_target(Serve_Context *sc, const char *fallback);
void render_page_shell(Serve_Context *sc, String_View title, String_View content);
void serve_error(Serve_Context *sc, int status_code);
void serve_resource(Serve_Context *sc, const char *resource_path, const char *content_type);
void serve_ok(Serve_Context *sc);
void sb_append_html_escaped(String_Builder *sb, const char *s);
void sb_append_json_escaped(String_Builder *sb, const char *s);
bool json_find_string(String_View body, const char *key, String_View *out);
bool json_find_int(String_View body, const char *key, long long *out);

#endif // CORE_SERVE_H_
