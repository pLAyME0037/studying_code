#include <unistd.h>
#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
// nob.c lives at the project root (like webc), so "third_party/nob/nob.h"
// resolves relative to this file. -I. keeps root-relative includes working.
#define NOB_REBUILD_URSELF(binary_path, source_path) \
    "cc", "-x", "c", "-I.", "-o", binary_path, source_path
#include "third_party/nob/nob.h"

#define BUILD_FOLDER "./build/"
#define SQLITE3_AMALGAMATION_FOLDER "./third_party/sqlite-amalgamation-3460100/"

#define SQLITE3_OBJ_PATH   BUILD_FOLDER"sqlite3.o"
#define COROUTINE_OBJ_PATH BUILD_FOLDER"coroutine.o"
#define BUNDLE_H_PATH      BUILD_FOLDER"bundle.h"
#define BUILD_TIME_PATH    BUILD_FOLDER"build_time.txt"
#define BUILD_TIME_H_PATH  BUILD_FOLDER"build_time.h"
#define TT_BIN_PATH        BUILD_FOLDER"bin/tt"
#define WEBC_BIN_PATH      BUILD_FOLDER"bin/webc"
#define OBJ_FOLDER         BUILD_FOLDER"obj/"
#define LINK_INPUTS_PATH   BUILD_FOLDER"link_inputs.txt"

#include <ctype.h>

// Append formatted text to a String_Builder, tagged with its source line
// (the tag is what makes generated headers diffable against nob.c).
#define genf(sb, ...) \
    do { \
        sb_appendf((sb), __VA_ARGS__); \
        sb_appendf((sb), " // %s:%d\n", __FILE__, __LINE__); \
    } while(0)

// Write `path` only when its bytes differ from what is already on disk, and
// report through *changed whether the file actually moved on.
//
// This matters because every generated header here is #included by a real
// translation unit: rewriting bundle.h unconditionally used to recompile
// serve.c / version.c / webc.c on EVERY run, even when nothing had changed,
// which defeated incremental builds entirely.
static bool write_file_if_changed(const char *path, const void *data,
                                  size_t size, bool *changed)
{
    *changed = true;

    struct stat st = {0};
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
        String_Builder old = {0};
        if (read_entire_file(path, &old)) {
            *changed = old.count != size || (size > 0 && memcmp(old.items, data, size) != 0);
        }
        sb_free(old);
    }

    if (!*changed) return true;
    if (!write_entire_file(path, data, size)) {
        nob_log(NOB_ERROR, "Could not write file %s: %s", path, strerror(errno));
        return false;
    }
    return true;
}

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

// Regenerate build/bundle.h from the bundled resources, writing it only when
// the bytes really differ. `changed` reports whether it moved on, which is
// how main() learns that a resource (or tailwind's output.css) is new.
int build_bundle(bool *changed) {
    struct {
        const char *file_path;
        size_t      offset;
        size_t      size;
    } resources[] = {
        { .file_path = "./css/output.css" },
        { .file_path = "./resource/image/user1.png" },
        { .file_path = "./resource/image/know_me.png" },
        { .file_path = "./js/ListExpandSwitcher.js" },
        { .file_path = "./js/PaginationSwitcher.js" },
        { .file_path = "./js/GeoCascade.js" },
        { .file_path = "./js/sidebarSwitcher.js" },
        { .file_path = "./js/themeSwitcher.js" },
    };

    Nob_String_Builder bundle = {0};
    Nob_String_Builder content = {0};
    Nob_String_Builder out = {0};

    mkdir_if_not_exists(BUILD_FOLDER);

    for (size_t i = 0; i < NOB_ARRAY_LEN(resources); ++i) {
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

    genf(&out, "#ifndef BUNDLE_H_");
    genf(&out, "#define BUNDLE_H_");
    genf(&out, "#include <stddef.h>");
    genf(&out, "typedef struct {");
    genf(&out, "    const char *file_path;");
    genf(&out, "    size_t      offset;");
    genf(&out, "    size_t      size;");
    genf(&out, "} Resource;");
    genf(&out, "#ifdef BUNDLE_IMPLEMENTATION");
    genf(&out, "static size_t resources_count = %zu;", NOB_ARRAY_LEN(resources));
    genf(&out, "static Resource resources[] = {");
    for (size_t i = 0; i < NOB_ARRAY_LEN(resources); ++i) {
        genf(&out, "    {.file_path = \"%s\", .offset = %zu, .size = %zu},",
                resources[i].file_path, resources[i].offset, resources[i].size);
    }
    genf(&out, "};");

    genf(&out, "static unsigned char bundle[] = {");
    size_t row_size = 20;
    for (size_t i = 0; i < bundle.count; ) {
        sb_append_cstr(&out, "     ");
        for (size_t col = 0; col < row_size && i < bundle.count; ++col, ++i) {
            sb_appendf(&out, "0x%02X, ", (unsigned char)bundle.items[i]);
        }
        sb_append_cstr(&out, "\n");
    }
    genf(&out, "};");
    genf(&out, "#endif // BUNDLE_IMPLEMENTATION");
    genf(&out, "#endif // BUNDLE_H_");

    // The timestamp deliberately does NOT live in here any more - it moved to
    // build/build_time.h. bundle.h is #included by serve.c and version.c as
    // well, so a timestamp inside it made those recompile on every run.
    *changed = false;
    bool ok = write_file_if_changed(BUNDLE_H_PATH, out.items, out.count, changed);
    if (ok) {
        if (*changed) {
            nob_log(NOB_INFO, "%s: updated (%zu resources, %zu bytes)",
                    BUNDLE_H_PATH, NOB_ARRAY_LEN(resources), bundle.count);
        } else {
            nob_log(NOB_INFO, "%s is up to date", BUNDLE_H_PATH);
        }
    }

    sb_free(out);
    free(content.items);
    free(bundle.items);
    return ok ? 0 : 1;
}

// build/build_time.h carries ONLY the startup-banner timestamp. Keeping it
// out of bundle.h means a run that rebuilds one .c file does not also churn
// bundle.h and drag serve.c + version.c along with it.
static bool write_build_time_header(const char *webc_build_time) {
    String_Builder sb = {0};
    sb_append_cstr(&sb, "#ifndef WEBC_BUILD_TIME_H_\n");
    sb_append_cstr(&sb, "#define WEBC_BUILD_TIME_H_\n");
    sb_appendf(&sb, "#define WEBC_BUILD_TIME \"%s\"\n", webc_build_time);
    sb_append_cstr(&sb, "#endif // WEBC_BUILD_TIME_H_\n");

    bool changed = false;
    bool ok = write_file_if_changed(BUILD_TIME_H_PATH, sb.items, sb.count, &changed);
    if (ok && changed) nob_log(NOB_INFO, "%s: updated", BUILD_TIME_H_PATH);
    sb_free(sb);
    return ok;
}

// Collect `pkg-config <option> <package>` output into `list`. -I paths
// become -isystem so third-party headers (mysql.h) stay warning-free under
// our -Wall/-Wextra/-pedantic flags. Tokens are copied into the temp arena,
// which lives for the whole run, so the list outlives the buffer freed here.
//
// Returned as a list rather than appended straight to a Cmd because each of
// the 55 per-file compiles needs the same flags: running pkg-config once
// per file would be 110 extra process spawns per build.
static bool collect_pkg_config(const char *package, const char *option, Nob_File_Paths *list) {
    const char *flags_path = BUILD_FOLDER"pkgconfig.flags";
    Cmd tmp = {0};
    cmd_append(&tmp, "pkg-config", option, package);
    if (!cmd_run(&tmp, .stdout_path = flags_path)) {
        fprintf(stderr, "ERROR: `pkg-config %s %s` failed - required to build "
                "the MySQL driver (install pkg-config and %s).\n",
                option, package, package);
        return false;
    }
    String_Builder sb = {0};
    if (!read_entire_file(flags_path, &sb)) return false;
    sb_append_null(&sb);
    for (char *p = strtok(sb.items, " \t\r\n"); p; p = strtok(NULL, " \t\r\n")) {
        da_append(list, strncmp(p, "-I", 2) == 0 ? temp_sprintf("-isystem%s", p + 2)
                                                 : temp_sprintf("%s", p));
    }
    sb_free(sb);
    return true;
}

// Append `pkg-config <option> <package>` output to `cmd`.
static bool append_pkg_config(Cmd *cmd, const char *package, const char *option) {
    Nob_File_Paths list = {0};
    if (!collect_pkg_config(package, option, &list)) return false;
    for (size_t i = 0; i < list.count; ++i) cmd_append(cmd, list.items[i]);
    nob_da_free(list);
    return true;
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

bool build_coroutine(void) {
    const char *output_path = COROUTINE_OBJ_PATH;
    const char *input_path = "./third_party/coroutine/coroutine.c";
    int rebuild_is_needed = needs_rebuild1(output_path, input_path);
    if (rebuild_is_needed < 0) return false;
    if (rebuild_is_needed) {
        Cmd cmd = {0};
        cmd_append(&cmd, "cc", "-Wall", "-Wextra", "-Wswitch-enum", "-ggdb",
                   "-I./third_party/coroutine",
                   "-O3", "-c", "-o", output_path, input_path);
        if (!cmd_run_sync_and_reset(&cmd)) return false;
    } else {
        nob_log(NOB_INFO, "%s is up to date", output_path);
    }
    return true;
}

typedef struct {
    Cmd         cmd;
    const char *display_root;
    const char *output_root;
    // Regenerate every output regardless of age (set when ./build/bin/tt
    // itself was just rebuilt, since a different compiler may emit
    // different bytes for an unchanged template).
    bool        force;
    // Set when at least one .h was actually rewritten.
    bool        did_work;
} h_to_html_walk_data_t;

bool h_to_html_walk_func(Nob_Walk_Entry entry) {
    if (entry.type != NOB_FILE_REGULAR) return true;
    if (!sv_ends_with_cstr(sv_from_cstr(entry.path), ".tt")) return true;

    h_to_html_walk_data_t *data = (h_to_html_walk_data_t *)entry.data;

    String_View input = sv_from_cstr(entry.path);
    size_t prefix_len = strlen(data->display_root);
    if (input.count <= prefix_len 
            || strncmp(input.data, data->display_root, prefix_len) != 0) {
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

    // Skip templates whose output is already current. Without this the build
    // forked ./build/bin/tt once per .tt every run - 39 process spawns even
    // when not one template had changed.
    if (!data->force) {
        struct stat in_st = {0}, out_st = {0};
        if (stat(entry.path, &in_st) == 0
            && stat(output.items, &out_st) == 0
            && out_st.st_mtime >= in_st.st_mtime)
        {
            nob_da_free(output);
            return true;
        }
    }
    data->did_work = true;

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
        if (!mkdir_if_not_exists(parent.items)) {
            nob_da_free(parent);
            return false;
        }
        nob_da_free(parent);
    }

    Fd out_fd = fd_open_for_write(output.items);
    if (out_fd == INVALID_FD) return false;

    cmd_append(&data->cmd, "./build/bin/tt", entry.path);
    if (!cmd_run_sync_redirect_and_reset(&data->cmd, (Nob_Cmd_Redirect) {
                .fdout = &out_fd }))
    {
        nob_fd_close(out_fd);
        nob_da_free(output);
        return false;
    }
    nob_fd_close(out_fd);
    nob_da_free(output);
    return true;
}

// Regenerate build/h_to_html/*.h from display/*.tt, skipping templates whose
// output is already newer than the template (or all of them when `force` is
// set, which is how a rebuilt ./build/bin/tt invalidates every prior
// result). Reports through `did_work` whether anything was actually
// rewritten - main() needs that to know if there is anything to do at all.
// Returns 1 on error.
int prepare_cttochtml(Cmd cmd, bool force, bool *did_work) {
    mkdir_if_not_exists("./build/h_to_html");

    h_to_html_walk_data_t data = {
        .cmd = cmd,
        .display_root = "./display",
        .output_root = "./build/h_to_html",
        .force = force,
    };

    if (!nob_walk_dir("./display", h_to_html_walk_func, .data = &data)) return 1;
    if (did_work) *did_work = data.did_work;
    return 0;
}

bool ends_with(const char *str, const char *suffix) {
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (str_len < suffix_len) return false;
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

// One translation unit of webc plus the paths the incremental build tracks
// for it. `src` is relative to the project root (as it always was), and the
// object/dependency paths mirror that tree under build/.
typedef struct {
    const char *src;   // "src/pos/staff.c"
    const char *obj;   // "./build/obj/src/pos/staff.c.o"
    const char *dep;   // "./build/obj/src/pos/staff.c.d" (gcc's -MMD default)
    bool        stale;
} C_Source;

typedef struct {
    C_Source *items;
    size_t    count;
    size_t    capacity;
} C_Sources;

// Create every ancestor directory of `path`. The final component is the file
// we are about to write, so it is left alone.
//
// Walked left to right on purpose: mkdir_if_not_exists() is not recursive,
// so each directory must be attempted only after its own parent exists.
static bool mkdirs_for(const char *path) {
    String_Builder dir = {0};
    sb_append_cstr(&dir, path);

    bool result = true;
    for (size_t i = 0; i < dir.count; ++i) {
        if (dir.items[i] != '/') continue;
        dir.items[i] = '\0';
        // stat() rather than get_file_type(): a missing parent here is the
        // normal case on a fresh tree, and get_file_type() logs it as an
        // ERROR that would drown the real build output.
        struct stat st = {0};
        if (dir.items[0] != '\0' && (stat(dir.items, &st) != 0 || !S_ISDIR(st.st_mode))) {
            if (!mkdir_if_not_exists(dir.items)) { result = false; break; }
        }
        dir.items[i] = '/';
    }

    sb_free(dir);
    return result;
}

// Does this file's object need recompiling?
//
// gcc's -MMD writes a companion .d listing every header the source pulled
// in, so this is an EXACT check rather than a heuristic: touch one header
// and only its includers recompile. Returns 1 when stale, 0 when fresh, -1
// on error.
static int obj_needs_rebuild(const char *obj_path,
                             const char *src_path,
                             const char *dep_path)
{
    struct stat st = {0};
    if (stat(obj_path, &st) < 0) return 1;          // no object yet
    time_t obj_time = st.st_mtime;

    // The source itself is always a prerequisite; the .d lists it too, but
    // checking it here keeps the function correct even before one exists.
    if (stat(src_path, &st) == 0 && st.st_mtime > obj_time) return 1;

    // A .d file that is not there yet is the normal first-build case, so use
    // stat() - get_file_type() would log every one of them as an ERROR.
    struct stat dep_st = {0};
    if (stat(dep_path, &dep_st) < 0 || !S_ISREG(dep_st.st_mode)) {
        // No recorded dependencies: we cannot prove the object is fresh, so
        // rebuild it rather than risk linking a stale binary.
        return 1;
    }

    String_Builder dep = {0};
    if (!read_entire_file(dep_path, &dep)) { sb_free(dep); return -1; }
    sb_append_null(&dep);

    int result = 0;
    // "<obj>: <src> <header> ..." - skip up to and including the first ':',
    // then compare every listed file against the object's timestamp.
    // Line continuations are folded in by treating '\' as a separator.
    char *at = strchr(dep.items, ':');
    for (char *tok = strtok(at ? at + 1 : dep.items, " \t\r\n\\");
         tok != NULL;
         tok = strtok(NULL, " \t\r\n\\"))
    {
        if (strchr(tok, ':') != NULL) continue;     // stray make target
        if (stat(tok, &st) < 0) { result = 1; break; }   // header vanished
        if (st.st_mtime > obj_time) { result = 1; break; }
    }

    sb_free(dep);
    return result;
}

// Walk `dir_path` recording every .c that links into webc, mirroring the
// tree under build/obj and build/dep so each source gets its own object.
// Standalone binaries (tt.c, nob.c) are excluded - they are not part of
// webc, exactly as append_c_files_from_dir() excluded them.
static bool collect_c_sources(const char *dir_path, C_Sources *out) {
    Nob_File_Paths children = {0};
    if (!nob_read_entire_dir(dir_path, &children)) return false;

    bool result = true;
    for (size_t i = 0; i < children.count && result; ++i) {
        const char *name = children.items[i];
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

        const char *full_path = nob_temp_sprintf("%s/%s", dir_path, name);
        Nob_File_Type type = nob_get_file_type(full_path);

        if (type == NOB_FILE_DIRECTORY) {
            result = collect_c_sources(full_path, out);
        } else if (type == NOB_FILE_REGULAR && ends_with(name, ".c")) {
            if (strcmp(name, "tt.c") == 0 || strcmp(name, "nob.c") == 0) continue;

            // gcc's -MMD (with no -MF) drops the .d next to the object it
            // was told to write, i.e. build/obj/.../foo.c.d - so both live
            // under build/obj and mkdirs_for() covers them together.
            C_Source s = {
                .src = full_path,
                .obj = nob_temp_sprintf("%s%s.o", OBJ_FOLDER, full_path),
                .dep = nob_temp_sprintf("%s%s.d", OBJ_FOLDER, full_path),
            };
            if (!mkdirs_for(s.obj)) { result = false; break; }
            da_append(out, s);
        }
    }

    nob_da_free(children);
    return result;
}

// Fill `cmd` with the exact command that compiles one translation unit.
// Shared by compile_c_source() and generate_compile_commands() so the
// command recorded in compile_commands.json can never drift from the one
// the build actually runs.
static void fill_compile_cmd(Cmd *cmd, const C_Source *s,
                             const Nob_File_Paths *cflags)
{
    cmd_append(cmd, "cc", "-Wall", "-Wextra", "-Wswitch-enum", "-ggdb",
               "-I"BUILD_FOLDER,
               "-I"SQLITE3_AMALGAMATION_FOLDER,
               "-I.");
    for (size_t i = 0; i < cflags->count; ++i) cmd_append(cmd, cflags->items[i]);
    // -MMD (not -MD): no system headers in the .d, and we only ever need to
    // know about this project's own headers.
    cmd_append(cmd, "-MMD", "-c", "-o", s->obj, s->src);
}

// Compile one source to its own object, recording header dependencies in the
// matching .d file so the next run can skip it when nothing moved.
static bool compile_c_source(const C_Source *s, const Nob_File_Paths *cflags) {
    Cmd cmd = {0};
    fill_compile_cmd(&cmd, s, cflags);
    return cmd_run_sync_and_reset(&cmd);
}


bool json_append_escaped(String_Builder *sb, const char *s, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        char c = s[i];
        if (c == '"')       sb_append_cstr(sb, "\\\"");
        else if (c == '\\') sb_append_cstr(sb, "\\\\");
        else if (c == '\n') sb_append_cstr(sb, "\\n");
        else if (c == '\r') sb_append_cstr(sb, "\\r");
        else if (c == '\t') sb_append_cstr(sb, "\\t");
        else                sb_append(sb, c);
    }
    return true;
}

// One entry per translation unit - the shape clangd/clang-tidy actually
// consume. The previous form recorded the whole link line a single time with
// a hardcoded "file": "nob.c", which no tool could map back to a source (and
// which, once objects replaced sources in the link, listed .o paths instead).
//
// The JSON is escaped exactly once: escaping the already-escaped string a
// second time turned every \" into \\\".
bool generate_compile_commands(const char *proj_dir,
                               const       Nob_File_Paths *cflags,
                               const       C_Sources *sources)
{
    String_Builder json = {0};
    sb_append_cstr(&json, "[\n");

    for (size_t i = 0; i < sources->count; ++i) {
        Cmd one = {0};
        fill_compile_cmd(&one, &sources->items[i], cflags);

        String_Builder render = {0};
        cmd_render(one, &render);
        sb_append_null(&render);

        sb_appendf(&json, "  {\n    \"directory\": \"%s\",\n    \"command\": \"", proj_dir);
        json_append_escaped(&json, render.items, render.count - 1);
        sb_appendf(&json, "\",\n    \"file\": \"%s\"\n  }%s\n",
                   sources->items[i].src,
                   (i + 1 < sources->count) ? "," : "");

        sb_free(render);
        nob_da_free(one);
    }

    sb_append_cstr(&json, "]\n");

    bool changed = false;
    bool result = write_file_if_changed("compile_commands.json", json.items, json.count, &changed);
    if (result && changed) nob_log(NOB_INFO, "compile_commands.json: updated (%zu entries)", sources->count);

    sb_free(json);
    return result;
}

// The object list the current binary was linked from, one path per line.
//
// A source file that is ADDED is caught by the normal staleness pass (it has
// no object yet), but a source that is DELETED leaves no trace in any .d
// file: without this stamp the orphaned object would silently stay in the
// link forever, and compile_commands.json would keep listing the dead file.
static bool link_inputs_differ(const Nob_File_Paths *objs) {
    String_Builder want = {0};
    for (size_t i = 0; i < objs->count; ++i) {
        sb_append_cstr(&want, objs->items[i]);
        sb_append_cstr(&want, "\n");
    }

    bool differ = true;
    struct stat st = {0};
    if (stat(LINK_INPUTS_PATH, &st) == 0 && S_ISREG(st.st_mode)) {
        String_Builder got = {0};
        if (read_entire_file(LINK_INPUTS_PATH, &got)) {
            differ = got.count != want.count
                  || (want.count > 0 && memcmp(got.items, want.items, want.count) != 0);
        }
        sb_free(got);
    }

    sb_free(want);
    return differ;
}

// Record what we just linked, so the next run can notice a removed source.
static bool write_link_inputs(const Nob_File_Paths *objs) {
    String_Builder sb = {0};
    for (size_t i = 0; i < objs->count; ++i) {
        sb_append_cstr(&sb, objs->items[i]);
        sb_append_cstr(&sb, "\n");
    }
    bool changed = false;
    bool ok = write_file_if_changed(LINK_INPUTS_PATH, sb.items, sb.count, &changed);
    sb_free(sb);
    return ok;
}

int main(int argc, char **argv) {
    // nob__go_rebuild_urself(argc, argv, "src/nob.c", NULL);  // Temporarily disabled
    NOB_GO_REBUILD_URSELF(argc, argv);
    Cmd cmd = {0};

    mkdir_if_not_exists("./build");
    mkdir_if_not_exists("./build/bin");

    // 1. ./build/bin/tt - only when src/tt.c itself moved. This used to be
    //    recompiled unconditionally on every run.
    bool tt_rebuilt = false;
    {
        int needs = needs_rebuild1(TT_BIN_PATH, "./src/tt.c");
        if (needs < 0) return 1;
        if (needs) {
            cmd_append(&cmd, "cc", "-Wall", "-Wextra", "-Wswitch-enum", "-ggdb",
                       "-o", TT_BIN_PATH, "./src/tt.c");
            if (!cmd_run_sync_and_reset(&cmd)) return 1;
            tt_rebuilt = true;
            nob_log(NOB_INFO, "%s: rebuilt", TT_BIN_PATH);
        }
    }

    // 2. build/h_to_html/*.h - one template at a time, skipping the ones
    //    already current. Rebuilding tt itself invalidates every prior
    //    result, hence `force`.
    bool h_to_html_worked = false;
    if (prepare_cttochtml(cmd, tt_rebuilt, &h_to_html_worked)) return 1;

    // 3. tailwindcss. NOT gated on purpose: v4 auto-detects sources across
    //    the entire tree - it reads classes out of build/, nob.c and
    //    README.md just as happily as out of display/ - so no mtime rule can
    //    be both exact and stable here (build/ is rewritten on every run,
    //    which would keep the gate firing forever). Making it incremental
    //    needs an explicit source list in css/input.css
    //    (`@import "tailwindcss" source(none);` + one @source per real
    //    source dir), which is a CSS-side change with its own blast radius
    //    and so is deliberately not part of this build rework. Its result is
    //    picked up for free below: output.css is a bundled resource, so
    //    build_bundle() reports whether it produced anything new.
    cmd_append(&cmd, "tailwindcss-linux-x64", "-i", "./css/input.css", "-o", "./css/output.css", "--minify");
    if (!cmd_run_sync_and_reset(&cmd)) {
        printf("tailwindcss-linux-x64 not found.\n"
                "[Recommend:] Download tailwindcss-linux-x64 and keep in ~/.local/bin\n"
                "Or get href of tailwind web api\n");
        return 1;
    }

    // 4. third-party objects (already incremental - unchanged from before).
    if (!build_sqlite3()) return 1;
    if (!build_coroutine()) return 1;

    // 5. build/bundle.h, written only when a bundled resource really
    //    changed. Because output.css is one of those resources, `bundle_changed`
    //    doubles as "did tailwind emit anything new?".
    bool bundle_changed = false;
    if (build_bundle(&bundle_changed)) return 1;

    // 6. Every translation unit of webc, with its own object and .d file.
    C_Sources sources = {0};
    Nob_File_Paths objs = {0};
    if (!collect_c_sources("src", &sources)) return 1;
    for (size_t i = 0; i < sources.count; ++i) da_append(&objs, sources.items[i].obj);

    // 7. Is anything stale? Source + every header its .d file recorded.
    //    This pass only DECIDES; the stamp below can still invalidate
    //    src/webc.c, which is why the list is re-asked after it moves.
    bool any_stale = false;
    for (size_t i = 0; i < sources.count; ++i) {
        int needs = obj_needs_rebuild(sources.items[i].obj, sources.items[i].src, sources.items[i].dep);
        if (needs < 0) return 1;
        sources.items[i].stale = needs != 0;
        if (needs) any_stale = true;
    }

    // 8. Anything to do at all? If not, stop here WITHOUT stamping
    //    build/build_time.h: that header is #included by src/webc.c, so
    //    touching it would drag the binary into a pointless rebuild on
    //    every single run and the fast path would never fire.
    bool need_build = any_stale || tt_rebuilt || h_to_html_worked || bundle_changed;
    if (!need_build) {
        // Safe to ask about the binary only now: reaching here means every
        // object exists, so nob_needs_rebuild() cannot trip on a missing input.
        int binary_stale = needs_rebuild(WEBC_BIN_PATH, objs.items, objs.count);
        if (binary_stale < 0) return 1;
        need_build = binary_stale != 0;
    }
    // Also relink when the set of objects itself changed (a .c was added or
    // deleted) - no timestamp can see a deletion.
    if (!need_build) need_build = link_inputs_differ(&objs);
    if (!need_build) {
        nob_log(NOB_INFO, "%s is up to date (%zu objects, nothing to do)", WEBC_BIN_PATH, sources.count);
        nob_da_free(objs);
        nob_da_free(sources);
        return 0;
    }

    // 9. We ARE producing a new binary now, so give it a fresh stamp for the
    //    startup banner - then re-ask every source, since build/build_time.h
    //    just moved and src/webc.c includes it.
    {
        mkdir_if_not_exists(BUILD_FOLDER);
        cmd_append(&cmd, "date", "-u", "+%a, %d %b %Y %H:%M:%S GMT");
        if (!cmd_run(&cmd, .stdout_path = BUILD_TIME_PATH)) return 1;
        cmd.count = 0;

        char webc_build_time[64] = {0};
        String_Builder out_sb = {0};
        if (!read_entire_file(BUILD_TIME_PATH, &out_sb)) return 1;
        while (out_sb.count > 0 && isspace(out_sb.items[out_sb.count - 1])) out_sb.count -= 1;
        if (out_sb.count < sizeof(webc_build_time)) memcpy(webc_build_time, out_sb.items, out_sb.count);
        sb_free(out_sb);
        if (webc_build_time[0] == '\0') snprintf(webc_build_time, sizeof(webc_build_time), "Thu, 01 Jan 1970 00:00:00 GMT");

        if (!write_build_time_header(webc_build_time)) return 1;

        for (size_t i = 0; i < sources.count; ++i) {
            int needs = obj_needs_rebuild(sources.items[i].obj, sources.items[i].src, sources.items[i].dep);
            if (needs < 0) return 1;
            sources.items[i].stale = needs != 0;
        }
    }

    // 10. Compile only the stale ones, each into its own object with a .d
    //     file beside it. On an untouched tree this loop compiles nothing.
    Nob_File_Paths cflags = {0};
    if (!collect_pkg_config("libmariadb", "--cflags", &cflags)) return 1;
    if (!collect_pkg_config("openssl", "--cflags", &cflags)) return 1;

    size_t compiled = 0;
    for (size_t i = 0; i < sources.count; ++i) {
        if (!sources.items[i].stale) continue;
        if (!compile_c_source(&sources.items[i], &cflags)) return 1;
        ++compiled;
    }
    if (compiled > 0) nob_log(NOB_INFO, "compiled %zu of %zu objects", compiled, sources.count);
    else nob_log(NOB_INFO, "all %zu objects already up to date", sources.count);

    // 11. Link the objects (skipped entirely by the fast path above).
    Cmd link = {0};
    cmd_append(&link, "cc", "-Wall", "-Wextra", "-Wswitch-enum", "-ggdb",
            "-I"BUILD_FOLDER,
            "-I"SQLITE3_AMALGAMATION_FOLDER,
            "-I.",
            "-o", WEBC_BIN_PATH);

    for (size_t i = 0; i < sources.count; ++i) cmd_append(&link, sources.items[i].obj);
    nob_cmd_append(&link, SQLITE3_OBJ_PATH, COROUTINE_OBJ_PATH);

    if (!append_pkg_config(&link, "libmariadb", "--libs")) return 1;
    if (!append_pkg_config(&link, "openssl", "--libs")) return 1;

    const char *proj_dir = get_current_dir_temp();
    if (!generate_compile_commands(proj_dir, &cflags, &sources)) return 1;

    if (!nob_cmd_run_sync_and_reset(&link)) return 1;
    if (!write_link_inputs(&objs)) return 1;

    nob_da_free(cflags);
    nob_da_free(objs);
    nob_da_free(sources);
    return 0;
}
