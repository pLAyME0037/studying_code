#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>

#include <unistd.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>

#define NOB_STRIP_PREFIX
#include "module/nob.h"

#define BUNDLE_IMPLEMENTATION
#include "../../build/bundle.h"
#include "serve.h"
#include "route.h"

#include "../../src/user/user.h"
#include "../../module/coroutine/coroutine.h"
#include "../../src/db/db.h"

Resource *find_resource(const char *file_path) {
    for (size_t i = 0; i < resources_count; ++i) {
        if (strcmp(file_path, resources[i].file_path) == 0) {
            return &resources[i];
        }
    }
    return NULL;
}

static bool write_entire_sv_coro(int fd, String_View sv) {
    String_View untransfered = sv;
    while (untransfered.count > 0) {
        ssize_t transfered = write(fd, untransfered.data, untransfered.count);
        if (transfered < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                coroutine_sleep_write(fd);
                continue;
            }
            fprintf(stderr, "ERROR: Could not write response: %s\n", strerror(errno));
            return false;
        }
        untransfered.data += transfered;
        untransfered.count -= transfered;
    }
    return true;
}

void serve_resource(Serve_Context *sc,
                    const char    *resource_path,
                    const char    *content_type)
{
    Resource *resource = find_resource(resource_path);
    if (!resource) {
        serve_error(sc, 404);
        return;
    }

    String_View body = {
        .data  = (char *)&bundle[resource->offset],
        .count = resource->size,
    };
    http_render_response(sc, 200, content_type, body);
}

static bool read_until_double_crlf(int fd, String_Builder *request) {
    char buffer[1024];
    size_t cur = 0;
    String_View suffix = sv_from_parts("\r\n\r\n", 4);
    bool finish = false;
    ssize_t n = 0;
    do {
        n = read(fd, buffer, sizeof(buffer));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                coroutine_sleep_read(fd);
                continue;
            }
            fprintf(stderr, "ERROR: could not read request: %s", strerror(errno));
            return false;
        }
        if (n == 0) return false;
        sb_append_buf(request, buffer, n);
        for (; cur < request->count && !finish; cur += 1) {
            finish = sv_starts_with(sv_from_parts(request->items + cur, request->count - cur), suffix);
        }
    } while (!finish);
    return true;
}

static long parse_content_length(String_View headers) {
    String_View rest = headers;
    while (rest.count) {
        String_View line = sv_trim(sv_chop_by_delim(&rest, '\n'));
        String_View name = sv_trim(sv_chop_by_delim(&line, ':'));
        if (sv_eq(name, sv_from_cstr("Content-Length"))) {
            char buf[32] = {0};
            snprintf(buf, sizeof(buf), "%.*s", (int)line.count, line.data);
            return strtol(buf, NULL, 10);
        }
    }
    return -1;
}

static bool read_body(int fd, String_Builder *request, size_t body_start, long content_length) {
    char buffer[1024];
    size_t have = request->count > body_start ? request->count - body_start : 0;
    while (content_length > 0 && have < (size_t)content_length) {
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                coroutine_sleep_read(fd);
                continue;
            }
            return false;
        }
        if (n == 0) break;
        sb_append_buf(request, buffer, n);
        have = request->count > body_start ? request->count - body_start : 0;
    }
    return true;
}

static void serve_request_coro(void *arg) {
    int client_fd = (int)(intptr_t)arg;
    Serve_Context sc = {0};
    sc.client_fd = client_fd;

    if (!read_until_double_crlf(client_fd, &sc.request)) {
        goto cleanup;
    }

    size_t body_start = sc.request.count;
    for (size_t i = 0; i + 3 < sc.request.count; ++i) {
        if (memcmp(sc.request.items + i, "\r\n\r\n", 4) == 0) {
            body_start = i + 4;
            break;
        }
    }

    long content_length = parse_content_length(sv_from_parts(sc.request.items, body_start));

    if (!read_body(client_fd, &sc.request, body_start, content_length)) {
        goto cleanup;
    }

    sc.body.count = 0;
    if (content_length > 0 && body_start + (size_t)content_length <= sc.request.count) {
        sb_append_buf(&sc.body, sc.request.items + body_start, (size_t)content_length);
    }
    sb_append_null(&sc.body);

    String_View request     = sb_to_sv(sc.request);
    String_View status_line = sv_trim(sv_chop_by_delim(&request, '\n'));
    String_View method      = sv_trim(sv_chop_by_delim(&status_line, ' '));
    String_View uri         = sv_trim(sv_chop_by_delim(&status_line, ' '));

    sc.query_string = sv_from_cstr("");
    for (size_t i = 0; i < uri.count; ++i) {
        if (uri.data[i] == '?') {
            sc.query_string = sv_from_parts(uri.data + i + 1, uri.count - i - 1);
            uri.count = i;
            break;
        }
    }

    route_request(&sc, method, uri);

    if (!write_entire_sv_coro(client_fd, sb_to_sv(sc.response))) {
    }

cleanup:
    shutdown(client_fd, SHUT_WR);
    struct pollfd pfd = { .fd = client_fd, .events = POLLIN };
    char buffer[4096];
    while (poll(&pfd, 1, 100) > 0) {
        if (read(client_fd, buffer, sizeof(buffer)) <= 0) break;
    }
    close(client_fd);
    sc_reset(&sc);
    return;
}

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void coroutine_server_run(const char *addr, uint16_t port) {
    coroutine_init();
    db_pool_init();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        fprintf(stderr, "ERROR: Could not create socket: %s\n", strerror(errno));
        return;
    }

    int option = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

    if (set_nonblocking(server_fd) < 0) {
        fprintf(stderr, "ERROR: Could not set non-blocking: %s\n", strerror(errno));
        close(server_fd);
        return;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(addr);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
        fprintf(stderr, "ERROR: Could not bind socket: %s\n", strerror(errno));
        close(server_fd);
        return;
    }

    if (listen(server_fd, 80) != 0) {
        fprintf(stderr, "ERROR: Could not listen: %s\n", strerror(errno));
        close(server_fd);
        return;
    }

    printf("Listening to http://%s:%d/ (coroutine mode)\n", addr, port);

    signal(SIGPIPE, SIG_IGN);

    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t client_addrlen = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addrlen);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                coroutine_sleep_read(server_fd);
                continue;
            }
            fprintf(stderr, "ERROR: Could not accept: %s\n", strerror(errno));
            continue;
        }

        if (set_nonblocking(client_fd) < 0) {
            close(client_fd);
            continue;
        }

        coroutine_go(serve_request_coro, (void*)(intptr_t)client_fd);
    }
}

void serve_request(Serve_Context *sc) {
    (void)sc;
}

void sc_reset(Serve_Context *sc) {
    sc->body.count = 0;
    sc->response.count = 0;
    sc->request.count = 0;
    sc->query_string = sv_from_cstr("");
}

const char *http_reason_phrase_by_status_code(int status_code) {
    static const char *reason_phrases[] = {
        [100] = "Continue",
        [101] = "Switching Protocols",
        [200] = "OK",
        [201] = "Created",
        [202] = "Accepted",
        [204] = "No Content",
        [301] = "Moved Permanently",
        [302] = "Found",
        [304] = "Not Modified",
        [400] = "Bad Request",
        [401] = "Unauthorized",
        [403] = "Forbidden",
        [404] = "Not Found",
        [405] = "Method Not Allowed",
        [408] = "Request Time-out",
        [413] = "Payload Too Large",
        [500] = "Internal Server Error",
        [501] = "Not Implemented",
        [503] = "Service Unavailable",
    };

    if (!((size_t)status_code < ARRAY_LEN(reason_phrases)) ||
        reason_phrases[status_code] == NULL) {
        return "Unknown";
    }
    return reason_phrases[status_code];
}

void http_render_response(Serve_Context *sc,
                          int            status_code,
                          const char    *content_type,
                          String_View    body)
{
    String_Builder *response = &sc->response;
    sb_append_cstr(response, temp_sprintf("HTTP/1.1 %d %s\r\n", status_code, http_reason_phrase_by_status_code(status_code)));
    sb_append_cstr(response, temp_sprintf("Content-Type: %s\r\n", content_type));
    sb_append_cstr(response, "Last-Modified: "WEBC_BUILD_TIME"\r\n");
    sb_append_cstr(response, temp_sprintf("Content-Length: %zu\r\n", body.count));
    sb_append_cstr(response, "Connection: close\r\n");
    sb_append_cstr(response, "\r\n");
    sb_append_buf(response, body.data, body.count);
}

void http_render_redirect(Serve_Context *sc,
                          int            status_code,
                          const char    *location)
{
    String_Builder *response = &sc->response;
    sb_append_cstr(response, temp_sprintf("HTTP/1.1 %d %s\r\n", status_code, http_reason_phrase_by_status_code(status_code)));
    sb_append_cstr(response, temp_sprintf("Location: %s\r\n", location));
    sb_append_cstr(response, "Content-Length: 0\r\n");
    sb_append_cstr(response, "Connection: close\r\n");
    sb_append_cstr(response, "\r\n");
}

void render_page_shell(Serve_Context *sc,
                       String_View    title,
                       String_View    content)
{
    String_Builder *sb = &sc->body;
    sb_append_cstr(sb, "<!DOCTYPE html>\n");
    sb_append_cstr(sb, "<html lang=\"en\"><head>");
    sb_append_cstr(sb, "<meta charset=\"utf-8\">");
    sb_append_cstr(sb, "<title>");
    sb_append_sv(sb, title);
    sb_append_cstr(sb, "</title>");
    sb_append_cstr(sb, "<link rel=\"stylesheet\" href=\"/css/output.css\">");
    sb_append_cstr(sb, "</head><body>");
    sb_append_sv(sb, content);
    sb_append_cstr(sb, "</body></html>");
}

void serve_error(Serve_Context *sc, int status_code) {
    String_View title = sv_from_cstr(temp_sprintf("%d %s", status_code, http_reason_phrase_by_status_code(status_code)));
    String_Builder content = {0};
    sb_append_cstr(&content, "<div class=\"p-8\"><h1 class=\"text-3xl font-bold\">");
    sb_append_sv(&content, title);
    sb_append_cstr(&content, "</h1></div>");
    sc->body.count = 0;
    render_page_shell(sc, title, sb_to_sv(content));
    http_render_response(sc, status_code, "text/html", sb_to_sv(sc->body));
}

void sb_append_html_escaped(String_Builder *sb, const char *s) {
    if (!s) return;
    for (; *s; ++s) {
        switch (*s) {
        case '&':  sb_append_cstr(sb, "&");  break;
        case '<':  sb_append_cstr(sb, "<");   break;
        case '>':  sb_append_cstr(sb, ">");   break;
        case '"':  sb_append_cstr(sb,""""); break;
        case '\'': sb_append_cstr(sb, "'");  break;
        default:   sb_append_buf(sb, s, 1);      break;
        }
    }
}

void serve_ok(Serve_Context *sc) {
    String_Builder resp = {0};
    sb_append_cstr(&resp, "{\"ok\":true}");
    http_render_response(sc, 200, "application/json", sb_to_sv(resp));
}

void sb_append_json_escaped(String_Builder *sb, const char *s) {
    sb_append_cstr(sb, "\"");
    if (s) {
        for (const char *p = s; *p; ++p) {
            switch (*p) {
            case '"':  sb_append_cstr(sb, "\\\""); break;
            case '\\': sb_append_cstr(sb, "\\\\"); break;
            case '\n': sb_append_cstr(sb, "\\n"); break;
            case '\r': sb_append_cstr(sb, "\\r"); break;
            case '\t': sb_append_cstr(sb, "\\t"); break;
            default:   sb_append_buf(sb, p, 1); break;
            }
        }
    }
    sb_append_cstr(sb, "\"");
}

bool json_find_string(String_View body, const char *key, String_View *out) {
    size_t key_len = strlen(key);
    for (size_t i = 0; i + key_len + 2 <= body.count; ++i) {
        if (memcmp(body.data + i, key, key_len) == 0                &&
            (i == 0 || body.data[i-1] != '_')                       &&
            i + key_len < body.count && body.data[i+key_len] == '"' &&
            body.data[i+key_len+1] == ':')
        {
            size_t start = i + key_len + 2;
            while (start < body.count && isspace(body.data[start])) start += 1;
            if (start >= body.count || body.data[start] != '"') return false;
            size_t begin = start + 1;
            size_t end = begin;
            while (end < body.count && body.data[end] != '"') {
                if (body.data[end] == '\\') end += 1;
                end += 1;
            }
            if (end >= body.count) return false;
            out->data = body.data + begin;
            out->count = end - begin;
            return true;
        }
    }
    return false;
}

bool json_find_int(String_View body, const char *key, long long *out) {
    size_t key_len = strlen(key);
    for (size_t i = 0; i + key_len + 2 <= body.count; ++i) {
        if (memcmp(body.data + i, key, key_len) == 0                &&
            i + key_len < body.count && body.data[i+key_len] == '"' &&
            body.data[i+key_len+1] == ':')
        {
            size_t start = i + key_len + 2;
            while (start < body.count && isspace(body.data[start])) start += 1;
            if (start >= body.count) return false;
            char *end = NULL;
            long long value = strtoll(body.data + start, &end, 10);
            if (end == body.data + start) return false;
            *out = value;
            return true;
        }
    }
    return false;
}
