#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"

#define BUILD_FOLDER "./build/"
#define SQLITE3_AMALGAMATION_FOLDER "./src/sqlite-amalgamation-3460100/"
#define SQLITE3_OBJ_PATH BUILD_FOLDER"sqlite3.o"
#define BUNDLE_H_PATH BUILD_FOLDER"bundle.h"
#define BUILD_TIME_PATH BUILD_FOLDER"build_time.txt"

#define genf(out, ...) \
    do { \
        fprintf((out), __VA_ARGS__); \
        fprintf((out), " // %s:%d\n", __FILE__, __LINE__); \
    } while(0)

static const char *const SNIPPET =
    "\n<script>\n"
    "(function() {\n"
    "  var last = null;\n"
    "  (function tick() { fetch('index.html', {cache:'no-store'})\n"
    "    .then(function(r) { var lm = r.headers.get('Last-Modified');\n"
    "      if (last && lm && lm !== last) { location.reload(); return; }\n"
    "      last = lm;\n"
    "    }).catch(function(){}).finally(function(){ setTimeout(tick, 1500); });\n"
    "  })();\n"
    "})();\n"
    "</script>\n";

int inject_reload_snippet(const char *path) {
    String_Builder sb = {0};
    if (!read_entire_file(path, &sb)) return 1;

    String_View html = sb_to_sv(sb);
    const char *needle = "</body>";
    size_t nl = strlen(needle);
    size_t at = (size_t)-1;
    for (size_t i = 0; i + nl <= html.count; ++i) {
        if (html.data[i] == needle[0] && memcmp(html.data + i, needle, nl) == 0) {
            at = i;
            break;
        }
    }
    if (at == (size_t)-1) {
        nob_log(NOB_WARNING, "%s: no </body>, skipping dev snippet", path);
        return 0;
    }

    String_Builder out = {0};
    sb_append_buf(&out, html.data, at);
    sb_append_buf(&out, SNIPPET, strlen(SNIPPET));
    sb_append_buf(&out, html.data + at, html.count - at);

    if (!write_entire_file(path, out.items, out.count)) return 1;
    nob_log(NOB_INFO, "%s: dev snippet injected", path);
    return 0;
}

int build_bundle(const char *webc_build_time) {
    struct {
        const char *file_path;
        size_t offset;
        size_t size;
    } resources[] = {
        { .file_path = "./css/output.css" },
        { .file_path = "./resource/image/user1.png" },
    };

    Nob_String_Builder bundle = {0};
    Nob_String_Builder content = {0};
    FILE *out = NULL;

    mkdir_if_not_exists(BUILD_FOLDER);

    for (size_t i = 0; i < NOB_ARRAY_LEN(resources); ++i) {
        nob_log(NOB_INFO, "Bundling %s into %s", resources[i].file_path, BUNDLE_H_PATH);
        content.count = 0;
        if (!nob_read_entire_file(resources[i].file_path, &content)) {
            nob_log(NOB_ERROR, "Could not read %s for bundling", resources[i].file_path);
            return 1;
        }
        resources[i].offset = bundle.count;
        resources[i].size = content.count;
        nob_da_append_many(&bundle, content.items, content.count);
        nob_da_append(&bundle, 0);
    }

    out = fopen(BUNDLE_H_PATH, "wb");
    if (out == NULL) {
        nob_log(NOB_ERROR, "Could not open file %s for writing: %s", BUNDLE_H_PATH, strerror(errno));
        return 1;
    }

    genf(out, "#ifndef BUNDLE_H_");
    genf(out, "#define BUNDLE_H_");
    genf(out, "#define WEBC_BUILD_TIME \"%s\"", webc_build_time);
    genf(out, "typedef struct {");
    genf(out, "    const char *file_path;");
    genf(out, "    size_t offset;");
    genf(out, "    size_t size;");
    genf(out, "} Resource;");
    genf(out, "#ifdef BUNDLE_IMPLEMENTATION");
    genf(out, "static size_t resources_count = %zu;", NOB_ARRAY_LEN(resources));
    genf(out, "static Resource resources[] = {");
    for (size_t i = 0; i < NOB_ARRAY_LEN(resources); ++i) {
        genf(out, "    {.file_path = \"%s\", .offset = %zu, .size = %zu},",
             resources[i].file_path, resources[i].offset, resources[i].size);
    }
    genf(out, "};");

    genf(out, "static unsigned char bundle[] = {");
    size_t row_size = 20;
    for (size_t i = 0; i < bundle.count; ) {
        fprintf(out, "     ");
        for (size_t col = 0; col < row_size && i < bundle.count; ++col, ++i) {
            fprintf(out, "0x%02X, ", (unsigned char)bundle.items[i]);
        }
        fprintf(out, "\n");
    }
    genf(out, "};");
    genf(out, "#endif // BUNDLE_IMPLEMENTATION");
    genf(out, "#endif // BUNDLE_H_");

    fclose(out);
    free(content.items);
    free(bundle.items);
    return 0;
}

bool build_sqlite3(void) {
    const char *output_path = SQLITE3_OBJ_PATH;
    const char *input_path = SQLITE3_AMALGAMATION_FOLDER"sqlite3.c";
    int rebuild_is_needed = needs_rebuild1(output_path, input_path);
    if (rebuild_is_needed < 0) return false;
    if (rebuild_is_needed) {
        // NOTE: We are omitting extension loading because it depends on dlopen which prevents us from
        // making webc statically linked
        Cmd cmd = {0};
        cmd_append(&cmd, "cc", "-Wall", "-Wextra", "-Wswitch-enum", "-ggdb",
                   "-I"SQLITE3_AMALGAMATION_FOLDER,
                   "-DSQLITE_OMIT_LOAD_EXTENSION",
                   "-O3", "-c", "-o", output_path, input_path);
        if (!cmd_run_sync_and_reset(&cmd)) return false;
    } else {
        nob_log(NOB_INFO, "%s is up to date", output_path);
    }
    return true;
}

typedef struct {
    Cmd cmd;
    const char *display_root;
    const char *output_root;
} Cttochtml_Walk_Data;

bool cttochtml_walk_func(Nob_Walk_Entry entry) {
    if (entry.type != NOB_FILE_REGULAR) return true;
    if (!sv_ends_with_cstr(sv_from_cstr(entry.path), ".tt")) return true;

    Cttochtml_Walk_Data *data = (Cttochtml_Walk_Data *)entry.data;

    String_View input = sv_from_cstr(entry.path);
    size_t prefix_len = strlen(data->display_root);
    if (input.count <= prefix_len || strncmp(input.data, data->display_root, prefix_len) != 0) {
        nob_log(NOB_ERROR, "Unexpected path %s outside %s", entry.path, data->display_root);
        return false;
    }

    // "./display/component/sidebar.h.tt" -> "component/sidebar.h"
    String_View rel = {
        .data  = input.data + prefix_len + 1,
        .count = input.count - prefix_len - 1,
    };
    String_View base = rel;
    sv_chop_suffix(&base, sv_from_cstr(".tt"));

    String_Builder output = {0};
    sb_append_cstr(&output, data->output_root);
    sb_append_cstr(&output, "/");
    sb_append_sv(&output, base);
    sb_append_null(&output);

    // Make sure the parent directory of the output exists
    size_t slash = output.count;
    for (size_t i = output.count - 1; i > 0; --i) {
        if (output.items[i] == '/') {
            slash = i;
            break;
        }
    }
    if (slash < output.count - 1) {
        String_Builder parent = {0};
        sb_append_buf(&parent, output.items, slash);
        sb_append_null(&parent);
        if (!mkdir_if_not_exists(parent.items)) return false;
    }

    Fd out_fd = fd_open_for_write(output.items);
    if (out_fd == INVALID_FD) return false;

    cmd_append(&data->cmd, "./bin/tt", entry.path);
    if (!cmd_run_sync_redirect_and_reset(&data->cmd, (Nob_Cmd_Redirect) {
        .fdout = &out_fd }))
    {
        return false;
    }
    return true;
}

int prepare_cttochtml(Cmd cmd) {
    mkdir_if_not_exists("./auto_ctrl");
    mkdir_if_not_exists("./auto_ctrl/cttochtml");

    Cttochtml_Walk_Data data = {
        .cmd = cmd,
        .display_root = "./display",
        .output_root = "./auto_ctrl/cttochtml",
    };

    if (!nob_walk_dir("./display", cttochtml_walk_func, .data = &data)) return 1;

    return 0;
}

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);
    Cmd cmd = {0};

    mkdir_if_not_exists("./bin");

    cmd_append(&cmd, "cc", "-Wall", "-Wextra", "-Wswitch-enum", "-ggdb", "-o", "./bin/tt", "tt.c");
    if (!cmd_run_sync_and_reset(&cmd)) return 1;

    if (prepare_cttochtml(cmd)) return 1;

    cmd_append(&cmd, "./bin/tailwindcss-linux-x64", "-i", "css/input.css", "-o", "css/output.css", "--minify");
    if (!cmd_run_sync_and_reset(&cmd)) return 1;

    char webc_build_time[64] = {0};
    {
        mkdir_if_not_exists(BUILD_FOLDER);
        cmd_append(&cmd, "date", "-u", "+%a, %d %b %Y %H:%M:%S GMT");
        if (!cmd_run(&cmd, .stdout_path = BUILD_TIME_PATH)) return 1;
        cmd.count = 0;

        String_Builder out_sb = {0};
        if (!read_entire_file(BUILD_TIME_PATH, &out_sb)) return 1;
        while (out_sb.count > 0 && isspace(out_sb.items[out_sb.count - 1])) out_sb.count -= 1;
        if (out_sb.count < sizeof(webc_build_time)) {
            memcpy(webc_build_time, out_sb.items, out_sb.count);
        }
        if (webc_build_time[0] == '\0') snprintf(webc_build_time, sizeof(webc_build_time), "Thu, 01 Jan 1970 00:00:00 GMT");
    }

    if (!build_sqlite3()) return 1;

    if (build_bundle(webc_build_time)) return 1;

    cmd_append(&cmd, "cc", "-Wall", "-Wextra", "-Wswitch-enum", "-ggdb",
               "-I"BUILD_FOLDER,
               "-I"SQLITE3_AMALGAMATION_FOLDER,
               "-o", "./bin/webc",
               "webc.c", "core/serve.c", "core/route.c",
               "core/notes_controller.c", "core/version_controller.c",
               "src/db.c", "src/notes.c", SQLITE3_OBJ_PATH);
    if (!cmd_run_sync_and_reset(&cmd)) return 1;

    return 0;
}
