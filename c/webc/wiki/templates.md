# Templates

How HTML becomes a C function. This is the idea the whole project is built
around, so it is worth reading before touching any page code.

## The problem it solves

The two usual answers both cost something:

- **Interpret templates at runtime** — parse strings on every request, and
  maintain a second language that lives outside the compiler's reach.
- **Write HTML as C string literals** — unreadable at any size, and `\"`
  escaping everywhere.

webc does neither. A template is HTML with C islands in it. At build time `tt`
emits a header that is `#include`d **inside a function**, so the page becomes
straight-line compiled code. No engine ships in the binary.

## The format

Two delimiters:

| | |
|---|---|
| `({` | open a C island |
| `})` | close it |

Everything outside an island is literal HTML. Everything inside is C, copied
verbatim.

```
Build time: ({ STR(WEBC_BUILD_TIME); })
```

There is no escaping, no `{{ }}`, no filters and no expression grammar. If you
want a conditional, write `if`. If you want a loop, write `for`. The template
language *is* C, which means it is fully typed, has no surprises, and is
checked by the compiler like any other code.

## The compiler — `src/tt.c`

The whole compiler is 38 lines and works by alternating `sv_chop_by_sv`:

```c
int c_code_mode = 0;
while (tmplat.count) {
    String_View token = sv_chop_by_sv(&tmplat, c_code_mode ? "})" : "({");
    if (c_code_mode) compile_c_code(token);     // printed verbatim
    else             compile_byte_array(token); // → OUT("\x..\x..", N);
    c_code_mode = !c_code_mode;
}
```

It starts in HTML mode, so the first token (text before the first `({`) is
treated as literal HTML. Each token flips the mode.

`compile_byte_array` writes the literal as a hex-escaped `OUT(...)` call:

```c
printf("OUT(\"");
for (uint64_t i = 0; i < s.count; ++i) printf("\\x%02x", s.data[i]);
printf("\", %lu);\n", s.count);
```

Byte-for-byte, with an explicit length — so the HTML never has to be scanned
or re-parsed at runtime, and embedded NULs are impossible.

## Output naming

`display/<path>/X.h.tt` → `build/h_to_html/<path>/X.h` (the `.tt` is stripped,
the directory layout is preserved):

```
display/user.h.tt              → build/h_to_html/user.h
display/layout/header.h.tt     → build/h_to_html/layout/header.h
display/component/pager_templates.h.tt → build/h_to_html/component/pager_templates.h
```

`nob.c` creates the parent directory and redirects `tt`'s stdout into the
target file.

## How it is consumed

The generated header is included **inside a function body**, after the caller
has defined the macros that give it meaning. This is `src/version/version.c`
in full:

```c
void render_version_page(Serve_Context *sc) {
    String_Builder *sb = &sc->body;
    render_page_header(sb, "Version", "/version");
#define OUT(buf, size) sb_append_buf(sb, buf, size);
#define STR(x) sb_append_cstr(sb, (x) ? (x) : "");
#include "build/h_to_html/version.h"
#undef STR
#undef OUT
    render_page_footer(sb);
}
```

Every `OUT(...)` in the generated file appends to this function's `sb`. That
single trick is what lets a flat file of HTML run as ordinary code with access
to local variables.

### The macro vocabulary

Defined by `src/core/webc_template.h`:

| Macro | Expands to | Use |
|---|---|---|
| `OUT(buf, size)` | append raw bytes | emitted by `tt` for HTML |
| `STR(x)` | append, `NULL` → `""` | any string |
| `INT(x)` | `sb_appendf(sb, "%d", (x))` | numbers |
| `CLS(cond, t, f)` | append `t` or `f` | class toggles |
| `ESCAPED(x)` | `sb_append_html_escaped(sb, ...)` | **user data into HTML** |
| `NAV_ACTIVE(prefix)` | `strncmp(current_path, prefix, ...) == 0` | sidebar highlight |
| `PAGE_TITLE` / `CURRENT_PATH` | `page_title` / `current_path` | template locals |
| `PAGE_BEGIN(sc, title, route)` | declares `sb`, `page_title`, `current_path`; calls `render_page_header()` | page open |
| `PAGE_END(sc)` | `render_page_footer(&(sc)->body)` | page close |

`src/layout/header.c` and `footer.c` redefine `OUT`/`STR`/`CLS`/`NAV_ACTIVE`
locally before including their own generated shell templates, then `#undef`
them — same mechanism, different binding.

> **`ESCAPED` is the one that matters for safety.** `STR` appends bytes as
> they are. Anything derived from the database or the request that lands in
> HTML goes through `ESCAPED`. `serve.c` also provides `sb_append_json_escaped`
> for the JSON API.

## Worked example

`display/version.h.tt`:

```html
<div>
    <h2 class="text-2xl font-bold ...">
        Version
    </h2>
    ...
        <p class="mt-2 text-sm ...">
            Build time: ({ STR(WEBC_BUILD_TIME); })
        </p>
    ...
</div>
```

`build/h_to_html/version.h` after `tt`:

```c
OUT("\x3c\x64\x69\x76\x3e\x0a\x20\x20\x20\x20\x3c\x68\x32...0a", 417);   // ← HTML
STR(WEBC_BUILD_TIME);                                                    // ← the island
OUT("\x3c\x2f\x70\x3e\x0a...", 3);                                       // ← HTML
```

which, once `render_version_page` compiles, is just three calls in order.

A richer island — a loop, from `display/user.h.tt`:

```
({ for (size_t i = 0; i < users.count; ++i) { })
<tr class="hover:bg-gray-50 ...">
    <td class="px-6 py-4 ...">
        ({ STR(users.items[i].id) })
    </td>
</tr>
({ } })
```

`users` is a local in the enclosing function. The generated header can see it
because it is textually inside the function — there is no context object, no
symbol table and no lookup at runtime.

## Fragment mode

`SERVE_READ` supports `?fragment=all`: the handler skips `PAGE_BEGIN`/
`PAGE_END` and emits only the body region. Templates are written with
`({ if (!fragment) { })` guards around shell markup so the same file serves
both the full page and the bare pagination store that
`js/PaginationSwitcher.js` fetches to flip pages client-side.

## Rules

1. **Never edit `build/h_to_html/`.** It is a build output; change
   `display/*.tt` instead. It is regenerated on every build.
2. **Every island must parse as C in the including function.** An island that
   references a variable the function does not have is a compile error, which
   is the point — but it means you cannot read a template without knowing its
   host function.
3. **Use `ESCAPED` for anything untrusted**, `STR` for data you own.
4. **Do not `#include` a `.c` file.** The build globs every `.c` under `src/`,
   so `#include "src/layout/header.c"` compiles that file twice and fails at
   link time with *multiple definition of `render_page_header`*. Include the
   `.h`. (This exact regression is in [decisions.md](decisions.md) D6.)

## Known wart

`compile_byte_array` prints `s.data[i]` — a **signed** `char` — with `%02x`.
Any byte ≥ `0x80` therefore prints as eight hex digits (`\xffffffe2` instead
of `\xe2`), which is out of range for a `char` escape.

- The resulting bytes are **correct**: GCC truncates, and the em dash in
  `version.h` compiles to `e2 80 94` as verified with `od`.
- The real build emits **zero warnings** (verified on a full rebuild).
- An isolated translation unit that includes only a generated header *does*
  produce `hex escape sequence out of range`, because the project's normal
  include context differs.
- 9 such escapes exist today, across `version.h`, `layout/footer.h` and
  `component/master_child.h`.

The fix would be one character — `printf("\\x%02x", (unsigned char)s.data[i])`
— and it is logged in [dev-log.md](dev-log.md) rather than silently changed,
because it is cosmetic today and a silent change to template output deserves
its own test run.
