`serve.c:L134-148`: 🔴 bug: `sc.method` and `sc.uri` never assigned to `sc`.`route_request` receives empty views, all routes 404. Add `sc.method = method; sc.uri = uri;` before routing.

`serve.c:L268-278`: 🔴 bug: `sb_append_html_escaped` appends raw characters instead of entities. Replace literals with `"&amp;"`, `"&lt;"`, `"&gt;"`, `"&quot;"`, `"&#39;"`.

`route.c:L89-103`: 🔴 bug: `type_id` uninitialized if file extension unknown. `switch (type_id)` reads garbage memory. Initialize `int type_id = 0;`.

`route.c:L117-124`: 🔴 bug: `ROUTE_EXACT` ignores `suffix` in `route_request`. `/notes/create` and `/users/create` match `/notes` and `/users` instead. Change prefix to `"/notes/create"` or update matcher to check suffix.

`route.c:L32-44`: 🔴 bug: `parse_uri_id` checks `uri.count <= prefix.count` and `uri.count <= suffix_sv.count` separately, not together. Short URI causes `id_sv.count` underflow and out-of-bounds read. Add `if (uri.count < prefix.count + suffix_sv.count) return false;`.

`serve.c:L152-160`: 🔴 bug: `sc_reset` only zeroes `.count`. `sc.request.items`, `sc.response.items`, and `sc.body.items` heap allocations leaked on every connection. Free `.items` in `sc_reset` or `cleanup`.

`route.c:L59-67,L78-105`: 🔴 bug: `String_Builder path` allocated on heap via NOB append but never freed. Leaks memory on static file hits. Call `da_free(&path)` before returns.

`serve.c:L305-307`: 🟡 risk: `json_find_string` double-increments `end` on `\\` without checking `end + 1 < body.count`. Trailing backslash causes out-of-bounds read. Add boundary check before skipping escaped character.

`serve.c:L55-75`: 🟡 risk: `read_until_double_crlf` and `read_body` have no buffer limit. Unbounded client stream exhausts server memory (DoS). Add `MAX_REQUEST_SIZE` cap.

`route.c:L110`: 🔵 nit: Typo in function name `route_inittialize`. Rename to `route_initialize`.
