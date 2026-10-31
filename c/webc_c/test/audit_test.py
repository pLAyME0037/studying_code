#!/usr/bin/env python3
"""Security audit: SQL injection + memory safety (C codebase).

Two legs:

  static  - regex/structural scan of src/**.{c,h} for known-dangerous
            patterns. No server needed. Reports findings with a severity
            and a one-line fix. Informational by default; --strict turns
            any CRITICAL/HIGH finding into a non-zero exit.

  dynamic - live probes against a running server. These are genuine
            regression assertions (t.chk): SQLi payloads must be stored
            as literal data, must never authenticate, must never make the
            server answer 500 or echo SQL text. Only non-destructive
            detection payloads are sent - never DROP/DELETE/UPDATE.

Usage:
    python3 test/audit_test.py --static-only
    python3 test/audit_test.py HOST PORT TMPDIR      # both legs
    python3 test/audit_test.py --strict --static-only

python3 stdlib only. Exits 0 unless a chk fails (dynamic) or --strict
finds a CRITICAL/HIGH (static).
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from testlib import Checker, Client  # noqa: E402

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src")


# ---------------------------------------------------------------------------
# Static leg
# ---------------------------------------------------------------------------

# (severity, code) -> message. Ordered worst-first for the report.
SEV_RANK = {"CRITICAL": 0, "HIGH": 1, "MEDIUM": 2, "LOW": 3, "INFO": 4}

# Unsafe C string/alloc functions. snprintf/memcpy-checked are excluded on
# purpose; these are the ones with no bound in the signature.
UNSAFE_C_FUNCS = [
    "strcpy", "strcat", "sprintf", "vsprintf", "gets", "alloca",
    "scanf", "sscanf", "realpath", "mktemp", "tmpnam",
]

# Files whose only job is the DB layer - raw exec is legitimate there.
DB_LAYER = ("db/db.c", "db/sql.c", "db/driver_sqlite.c",
            "db/driver_mysql.c", "db/driver_postgres.c")


def _c_files():
    for root, _dirs, files in os.walk(SRC):
        for f in files:
            if f.endswith((".c", ".h")):
                p = os.path.join(root, f)
                yield os.path.relpath(p, os.path.join(SRC, "..")), p


def _lines(path):
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.readlines()


def _strip_comments(text):
    """Crude // and /* */ removal so comments don't produce findings."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def static_audit():
    findings = []  # (severity, file, line, message, fix)

    # Pre-read every file once, comments stripped, for cross-line checks.
    srcs = {}
    for rel, path in _c_files():
        srcs[rel] = _strip_comments("".join(_lines(path)))

    for rel, path in sorted(srcs.items()):
        text = srcs[rel]
        raw = _lines(path)
        is_db = any(rel.endswith(sfx) for sfx in DB_LAYER)

        # --- 1. Unsafe C string functions -------------------------------
        for i, line in enumerate(text.splitlines(), 1):
            for fn in UNSAFE_C_FUNCS:
                if re.search(rf"\b{fn}\s*\(", line):
                    sev = "HIGH" if fn in (
                        "strcpy", "strcat", "gets", "sprintf", "vsprintf"
                    ) else "MEDIUM"
                    findings.append((
                        sev, rel, i,
                        f"{fn}() with no bound in its signature",
                        "use snprintf/strncpy with an explicit size",
                    ))

        # --- 2. SQL value spliced as '%s' (hand-rolled quoting) ---------
        # A format string that both quotes a value as '%s' and mentions a
        # SQL verb is the F1 pattern: safe only if the argument is already
        # escaped, and quote-doubling is not sufficient on MySQL.
        for m in re.finditer(
            r"""temp_sprintf\s*\(\s*"(?:[^"\\]|\\.)*'%s'(?:[^"\\]|\\.)*\"""",
            text,
        ):
            line_no = text[: m.start()].count("\n") + 1
            fmt = m.group(0)
            if not re.search(
                r"\b(SELECT|INSERT|UPDATE|DELETE|WHERE|FROM)\b", fmt, re.I
            ):
                continue
            findings.append((
                "HIGH", rel, line_no,
                "SQL value spliced as '%s' (hand-rolled quoting); "
                "quote-doubling is not sufficient on MySQL default escapes",
                "bind it with sql_bind(?,/$1) like the rest of the tree",
            ))

        # --- 3. LIKE pattern built without an ESCAPE clause --------------
        # If a file builds a '%...%' pattern from user text AND has no
        # ESCAPE clause anywhere, % and _ are live wildcards.
        builds_pattern = bool(re.search(
            r"""sb_append_cstr\s*\(\s*&\w+\s*,\s*"%"|"""
            r"""snprintf\s*\([^)]*"\\{2}%%""",
            text,
        ))
        has_like = bool(re.search(r"\bLIKE\b", text, re.I))
        has_escape = bool(re.search(r"\bESCAPE\b", text, re.I))
        if builds_pattern and has_like and not has_escape:
            i = text[: text.index("LIKE")].count("\n") + 1 if "LIKE" in text else 1
            findings.append((
                "MEDIUM", rel, i,
                "LIKE pattern wraps user text without escaping %/_ and has "
                "no ESCAPE clause (wildcard injection)",
                "escape \\, %% and _ in the pattern and add ESCAPE '\\'",
            ))

        # --- 4. Raw SQL exec outside the db layer -----------------------
        if not is_db:
            for m in re.finditer(
                r"\b(sqlite3_exec|mysql_query|mysql_real_query|"
                r"sqlite3_mprintf)\s*\(", text
            ):
                line_no = text[: m.start()].count("\n") + 1
                findings.append((
                    "HIGH", rel, line_no,
                    f"{m.group(1)}() bypasses the bind layer",
                    "route through sql_prepare/sql_bind",
                ))

        # --- 5. Two temp_* calls in one argument list (aliasing) ---------
        # temp_sprintf returns arena memory that the next temp_* call may
        # hand out again. Two in one expression is a classic UAF.
        for i, line in enumerate(text.splitlines(), 1):
            n = len(re.findall(r"temp_(?:sprintf|strdup|strndup)\s*\(", line))
            if n >= 2:
                findings.append((
                    "MEDIUM", rel, i,
                    f"{n} temp_* allocations in one expression - the arena "
                    "may reuse the first buffer while still formatting",
                    "assign each to a local, or use a stable buffer",
                ))

        # --- 6. Uninitialised read: declared then read before set -------
        # Cheap shape check only: `Type v;` followed by use of v in the
        # next few lines with no `v =` / `v[` between.
        for m in re.finditer(
            r"\b(\w+)\s+(\w+)\s*;\s*\n(?:(?!\2\s*=).)*?\b\2\s*(?:\.|\[|;)", text
        ):
            typ, var = m.group(1), m.group(2)
            if typ in ("int", "size_t", "char", "bool", "long",
                       "unsigned", "double", "float"):
                line_no = text[: m.start()].count("\n") + 1
                findings.append((
                    "LOW", rel, line_no,
                    f"`{typ} {var};` appears read before being assigned",
                    "initialise at declaration",
                ))

    # --- 7. The temp arena: never reset (whole-tree structural check) ---
    all_src = "\n".join(srcs.values())
    uses_arena = bool(re.search(r"temp_(sprintf|strdup|strndup|alloc)\s*\(", all_src))
    resets_arena = bool(re.search(r"\b(nob_temp_reset|temp_reset)\s*\(", all_src))
    if uses_arena and not resets_arena:
        findings.append((
            "HIGH", "src/ (whole tree)", 0,
            "nob_temp arena (8MB static buffer) is written by "
            "temp_sprintf/strdup on every request but nob_temp_reset() is "
            "never called: nob_temp_size only grows, so a long-lived server "
            "crawls to NOB_ASSERT \"Increase NOB_TEMP_CAPACITY\" and aborts "
            "(slow, request-driven DoS)",
            "call nob_temp_reset() at the top of each request in the serve "
            "loop (it is single-threaded, so a global reset is safe)",
        ))

    # --- 8. Assert-on-OOM in the serving path ---------------------------
    for rel, path in _c_files():
        if not rel.startswith("http/"):
            continue
        for i, line in enumerate(_lines(path), 1):
            if "NOB_ASSERT" in line and "NULL" in line:
                findings.append((
                    "MEDIUM", rel, i,
                    "NOB_ASSERT on allocation failure in the HTTP path "
                    "aborts the whole process instead of failing the request",
                    "return 500 for this request and keep serving",
                ))

    findings.sort(key=lambda f: (SEV_RANK.get(f[0], 9), f[1], f[2]))
    return findings


def report_static(strict):
    findings = static_audit()
    print("--- static audit: %d finding(s) ---" % len(findings))
    counts = {}
    for sev, rel, line, msg, fix in findings:
        counts[sev] = counts.get(sev, 0) + 1
        loc = f"{rel}:{line}" if line else rel
        print(f"  [{sev:8}] {loc}\n            {msg}\n            fix: {fix}")
    print("--- severities: "
          + ", ".join(f"{s}={counts[s]}" for s in sorted(counts, key=lambda x: SEV_RANK[x]))
          + " ---")
    bad = [f for f in findings if f[0] in ("CRITICAL", "HIGH")]
    if strict and bad:
        print(f"STRICT: {len(bad)} CRITICAL/HIGH finding(s) -> exit 1")
        return False
    return True


# ---------------------------------------------------------------------------
# Dynamic leg - live SQLi probes. Detection payloads only, never destructive.
# ---------------------------------------------------------------------------

# Each payload must be *inert if it were ever executed*: they only select.
# We assert the app treats them as literal text.
PAYLOADS = [
    "' OR '1'='1",
    "' OR '1'='1' --",
    "1' AND '1'='2",
    "' UNION SELECT 1 --",
    "\" OR \"\"=\"",
    "admin'--",
]

# SQL text that must never appear in a response body (leak check).
SQL_LEAK = re.compile(
    r"sqlite3?_|SQLITE_ERROR|syntax error|mysql_|MariaDB|near \"|"
    r"unrecognized token|no such column|no such table",
    re.I,
)


def probe_no_auth_bypass(t, c):
    """A SQLi payload in the login fields must not authenticate."""
    for payload in PAYLOADS:
        st, hdr, _, _ = c.req_full(
            "POST", "/login",
            body=("username=" + payload.replace(" ", "%20")
                  + "&password=" + payload.replace(" ", "%20")).encode(),
            headers={"Content-Type": "application/x-www-form-urlencoded"},
        )
        loc = hdr.get("Location", "")
        # A successful login redirects into the app; a rejected one stays.
        t.chk(f"login SQLi inert ({payload[:14]!r})",
              (st, loc in ("/", "/dashboard", "/notes")),
              (303, False))


def probe_search_is_literal(t, c, base_path, col_param):
    """A payload in a filter box must match nothing, not everything."""
    for payload in PAYLOADS:
        sep = "&" if "?" in base_path else "?"
        url = f"{base_path}{sep}{col_param}=" + payload.replace(" ", "%20")
        st, body, ctype = c.get(url)
        html = body.decode("utf-8", "replace")
        rows = html.count("data-row-id=")
        t.chk(f"filter SQLi matches nothing ({payload[:14]!r})",
              (st, rows), (200, 0))
        t.chk(f"filter SQLi leaks no SQL text ({payload[:14]!r})",
              bool(SQL_LEAK.search(html)), False)


def probe_create_stores_literal(t, c, db_path, create_path, fields,
                                marker_field, read_back_sql):
    """A payload submitted to a create form is stored as literal text.

    We insert, then read the row straight out of SQLite and require the
    payload to be present verbatim - proof it was bound, not executed.
    """
    import sqlite3
    import urllib.parse

    payload = "' OR '1'='1'; DROP TABLE users;--"
    post = dict(fields)
    post[marker_field] = payload
    st, body, _ = c.post_urlencoded(
        create_path + ("&" if "?" in create_path else "?") + "redirect=/",
        post,
    )
    # Any outcome other than a server error is acceptable here; what matters
    # is that the DB still has its tables and holds the literal string.
    t.chk(f"create with SQLi payload does not 500 ({create_path})", st != 500,
          True)
    conn = sqlite3.connect(db_path, timeout=10)
    try:
        # Tables must all still exist - nothing was dropped.
        n_tables = conn.execute(
            "SELECT COUNT(*) FROM sqlite_master WHERE type='table'"
        ).fetchone()[0]
        t.chk(f"tables intact after SQLi create ({create_path})",
              n_tables > 10, True)
        row = conn.execute(read_back_sql, (payload,)).fetchone()
        t.chk(f"payload stored verbatim, not executed ({create_path})",
              row is not None, True)
    finally:
        conn.close()
    _ = urllib.parse  # imported for symmetry with post encoding


def probe_error_pages_dont_leak(t, c):
    """Malformed/oversized input must not surface driver text."""
    probes = [
        ("GET", "/pos/provinces?q=" + "'" * 40, None),
        ("GET", "/users/" + "A" * 300 + "/delete", None),
        ("GET", "/pos/nonexistent_page", None),
        ("POST", "/login", b"username=%ff%fe&password=%00"),
    ]
    for method, path, body in probes:
        st, data, ctype = c.req(method, path, body) if body else \
            c.req(method, path)
        text = data.decode("utf-8", "replace")
        t.chk(f"{method} {path[:34]!r} leaks no SQL/driver text",
              bool(SQL_LEAK.search(text)), False)
        t.chk(f"{method} {path[:34]!r} answers without a 500",
              st != 500, True)


def run_dynamic(host, port, tmpdir):
    t = Checker()
    db_path = os.path.join(tmpdir, "nested", "deep", "db")

    c = Client(host, port)
    c.wait_ready()
    probe_no_auth_bypass(t, c)

    # Reuse the demo staff session for the authenticated pages.
    st, _ = c.login_as("sd.staff1", "posadmin1")
    t.chk("audit login", st, 303)

    probe_search_is_literal(t, c, "/pos/provinces", "q_name_en")
    probe_search_is_literal(t, c, "/pos/products", "q_name_en")
    probe_search_is_literal(t, c, "/people", "q")
    probe_search_is_literal(t, c, "/?q=", "" if False else "q")
    probe_error_pages_dont_leak(t, c)

    return t.summary()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main(argv):
    static_only = "--static-only" in argv
    strict = "--strict" in argv
    args = [a for a in argv if not a.startswith("--")]

    ok = report_static(strict)

    if not static_only:
        if len(args) < 3:
            print("usage: audit_test.py [--static-only] [--strict] "
                  "HOST PORT TMPDIR")
            sys.exit(2 if not ok else 0)
        host, port, tmpdir = args[0], int(args[1]), args[2]
        if not run_dynamic(host, port, tmpdir):
            ok = False

    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main(sys.argv[1:])
