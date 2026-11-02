#!/usr/bin/env python3
"""Phase 13 CRUD contract: every /pos master page's own create and edit
forms must be accepted by its own handlers.

Engine contract: MD_MasterConfig.columns drive BOTH the display and the
forms, while each page's handler declares its own required-field list.
When a required key never renders as an input - or a rendered field the
handler cannot accept - the UI can only ever produce 400/500: the form
posts, the server rejects, and nobody can create the row from the page.
This test scrapes each sidebar page's data-md-op forms and submits them
for real; 302 means the round trip works end to end.

Per page:
  GET page -> scrape the create form -> fill inputs browser-style
  (required selects take the first non-empty option no live row already
  holds, optional selects keep the "None" default, dates stay empty so
  DB defaults win, text/numbers get a page-unique marker or a typed
  fake) -> POST expect 302.
  Then walk the pager until the created row's update form turns up,
  submit it exactly as rendered (empty optional inputs must keep stored
  data) -> expect 302, then the row's delete form -> expect 302, and
  assert no live row with the marker remains.

Runs LAST in run.sh: the rows it leaves soft-deleted must not disturb
earlier suites' counts.
"""
import os
import re
import sqlite3
import sys
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from testlib import Client, Checker  # noqa: E402

HOST = sys.argv[1]
PORT = int(sys.argv[2])
TMP = sys.argv[3]

DB = os.path.join(TMP, "nested", "deep", "db")

c = Client(HOST, PORT)
t = Checker()

FORM_RE = re.compile(r"<form\b([^>]*)>(.*?)</form>", re.S)
ATTR_RE = re.compile(r'([a-zA-Z-]+)="([^"]*)"')
INPUT_RE = re.compile(r"<input\b([^>]*)/?>", re.S)
SELECT_RE = re.compile(r"<select\b([^>]*)>(.*?)</select>", re.S)
OPTION_RE = re.compile(r"<option\b([^>]*)>(.*?)</option>", re.S)
TEXTAREA_RE = re.compile(r"<textarea\b([^>]*)>(.*?)</textarea>", re.S)
VALUE_RE = re.compile(r'value="([^"]*)"')
NAME_RE = re.compile(r'name="([^"]*)"')
TYPE_RE = re.compile(r'type="([^"]*)"')

MONEY_HINTS = ("price", "cost", "amount", "cash", "fee", "rate", "tax",
               "balance", "subtotal", "discount", "total", "points")
QTY_HINTS = ("quantity", "threshold")
# CHECK'd enums rendered as free text must not get fake words: submit the
# empty string and let the handler's COALESCE/default (or a 500 that
# exposes a real contract bug) answer.
ENUM_NAMES = ("status", "entry_type", "delivery_status", "payment_status",
              "action")


def attrs_of(s):
    return dict(ATTR_RE.findall(s))


def has_attr(raw, name):
    """Is a bare attribute present in a raw attribute string?

    ATTR_RE only matches key="value" pairs, so valueless HTML attributes
    (required, selected, checked) are invisible to attrs_of(). Scan the
    raw string for the word itself - word-bounded so `selected_extra`
    or a data-selected="..." value never match.
    """
    return bool(re.search(r"(?<![-\w])%s(?![\w-])" % re.escape(name), raw))


def fake_text(name, seq):
    n = name.lower()
    if "email" in n:
        return f"ct13{seq}@contract.test"
    if "phone" in n:
        return f"0990100{seq:04d}"
    if n == "username":
        return f"ct13u{seq}"
    if n == "sku" or n == "barcode" or n.endswith("_code") or n == "code":
        return f"CT13-{seq}"
    if "trans_key" in n:
        return f"ct13.key.{seq}"
    if n.endswith("_at") or n.endswith("_date") or "date" in n:
        return ""  # never required (engine) -> DB default wins
    if n in ENUM_NAMES:
        return ""
    if any(k in n for k in MONEY_HINTS):
        return "1.5"
    if any(k in n for k in QTY_HINTS):
        return "1"
    return f"ct13_{n}_{seq}"


def db_live_count(table, col, marker):
    conn = sqlite3.connect(DB, timeout=10)
    try:
        conn.execute("PRAGMA busy_timeout=5000")
        row = conn.execute(
            f"SELECT count(*) FROM {table} "
            f"WHERE {col} = ? AND deleted_at IS NULL",
            (marker,),
        ).fetchone()
        return row[0]
    finally:
        conn.close()


def db_value_taken(table, col, value):
    """Does a live row already hold this FK value?

    Required FK selects are UNIQUE in several tables (staff.user_id,
    deliveries.order_id, ...), so scraping the first option can collide
    with a row an earlier suite left behind - the engine answers 409,
    which is the documented duplicate-UNIQUE contract, not a form bug.
    Prefer an option no live row holds yet. Unknown table/column (or a
    table without deleted_at, e.g. notes) answers "free" so the caller
    keeps the plain first-option behaviour.
    """
    if not table or not col or value == "":
        return False
    conn = sqlite3.connect(DB, timeout=10)
    try:
        conn.execute("PRAGMA busy_timeout=5000")
        for sql in (f"SELECT count(*) FROM {table} WHERE {col} = ? "
                    "AND deleted_at IS NULL",
                    f"SELECT count(*) FROM {table} WHERE {col} = ?"):
            try:
                return conn.execute(sql, (value,)).fetchone()[0] > 0
            except sqlite3.Error:
                continue
        return False
    finally:
        conn.close()


def db_fk_target(table, col):
    """The table a FK column points at, parsed from the live DDL.

    A cascade leaf (the select carrying data-geo) ships no options at all:
    its three ancestor selects are UI-only navigation and the village list
    is fetched per commune by GeoCascade.js, which never runs here. To
    source a value the way that browser would, we need the parent table,
    and only the schema knows it.
    """
    if not table or not col:
        return None
    conn = sqlite3.connect(DB, timeout=10)
    try:
        row = conn.execute(
            "SELECT sql FROM sqlite_master WHERE type='table' AND name=?",
            (table,)).fetchone()
    finally:
        conn.close()
    if not row or not row[0]:
        return None
    m = re.search(r"\b%s\b[^,]*?\bREFERENCES\s+\"?([A-Za-z_]\w*)\"?"
                  % re.escape(col), row[0], re.I)
    return m.group(1) if m else None


def db_cascade_value(table, col):
    """A real, untaken value for a cascade leaf.

    This is what GeoCascade.js would have put into the select once the
    user walked province -> district -> commune -> village; the scraper
    cannot run that script, so it takes the value straight from the
    parent table. Reached only when the scraped form offers no non-empty
    option whatsoever - the cascade case - so ordinary selects keep their
    first-option behaviour untouched.
    """
    tgt = db_fk_target(table, col)
    if not tgt:
        return ""
    rows = None
    conn = sqlite3.connect(DB, timeout=10)
    try:
        conn.execute("PRAGMA busy_timeout=5000")
        for sql in (f"SELECT id FROM {tgt} WHERE deleted_at IS NULL ORDER BY id",
                    f"SELECT id FROM {tgt} ORDER BY id"):
            try:
                rows = conn.execute(sql).fetchall()
                break
            except sqlite3.Error:
                continue
    finally:
        conn.close()
    if not rows:
        return ""
    for (v,) in rows:
        if v and not db_value_taken(table, col, v):
            return v
    return rows[0][0]


def forms_on(html):
    """[(attrs_dict, inner_html), ...] for every form on the page."""
    out = []
    for m in FORM_RE.finditer(html):
        out.append((attrs_of(m.group(1)), m.group(2)))
    return out


def find_form(html, op, master_id=None):
    for a, inner in forms_on(html):
        if a.get("data-md-op") != op:
            continue
        if master_id is not None and a.get("data-md-master") != master_id:
            continue
        return a, inner
    return None, None


def parse_fields(inner, create=True, table=None):
    """Browser-style (fields, kinds) for a form body.

    kinds maps field name -> "text"|"number"|"date"|"hidden"|"select"|
    "textarea" so the marker pick can stay off FKs and numerics.

    create=True: fill text/number with fakes, required selects take the
    first non-empty option that no live row already holds (see
    db_value_taken - a scraped first option can collide with a UNIQUE
    FK and earn a 409), optional selects submit the browser default
    (first option, possibly the empty None). create=False: submit every
    value exactly as rendered (prefilled inputs, selected options).
    """
    fields = {}
    kinds = {}
    seq = len(fields)
    for m in INPUT_RE.finditer(inner):
        a = attrs_of(m.group(1))
        name = a.get("name")
        if not name:
            continue
        typ = a.get("type", "text").lower()
        if typ == "file" or typ == "submit" or typ == "button" \
                or typ == "reset":
            continue
        if typ == "checkbox" and "checked" not in m.group(1):
            continue
        kinds[name] = typ if typ in ("number", "date", "hidden") else "text"
        if not create:
            fields[name] = a.get("value", "")
            continue
        if typ == "hidden":
            fields[name] = a.get("value", "")
        elif typ == "number":
            fields[name] = fake_text(name, seq) or "1"
            if not re.fullmatch(r"-?\d+(\.\d+)?", fields[name]):
                fields[name] = "1"
        elif typ == "date":
            fields[name] = ""  # engine: dates never required
        else:
            fields[name] = fake_text(name, seq)
        seq += 1
    for m in SELECT_RE.finditer(inner):
        a = attrs_of(m.group(1))
        name = a.get("name")
        if not name:
            continue
        kinds[name] = "select"
        # Keep the RAW option attrs too: `selected` is a bare attribute and
        # attrs_of() cannot see it (see has_attr).
        opts = [(attrs_of(oa), oh.strip(), oa)
                for oa, oh in OPTION_RE.findall(m.group(2))]
        if not create:
            if not opts:
                continue
            picked = ""
            for oa, _oh, oraw in opts:
                if has_attr(oraw, "selected"):
                    picked = oa.get("value", "")
                    break
            if not picked:
                # Nothing marked selected (or the stored value is no longer
                # offerable): browser falls back to the first option.
                picked = opts[0][0].get("value", "")
            fields[name] = picked
            continue
        # `required` renders as a bare attribute (no ="") - ATTR_RE cannot
        # see it, so scan the raw attr string for the word itself.
        required = has_attr(m.group(1), "required")
        if required:
            picked = ""
            for oa, _oh, _oraw in opts:
                v = oa.get("value", "")
                if not v:
                    continue
                # Skip options a live row already holds: UNIQUE FK columns
                # (staff.user_id, deliveries.order_id, ...) answer 409 and
                # the create leg would never get its row.
                if db_value_taken(table, name, v):
                    continue
                picked = v
                break
            if not picked:
                # Everything is taken - fall back to the plain first option
                # so the caller still gets a 409 it can report.
                for oa, _oh, _oraw in opts:
                    if oa.get("value", ""):
                        picked = oa.get("value", "")
                        break
            if not picked and a.get("data-geo"):
                # A cascade leaf renders only `None` server-side - the
                # ancestors are UI-only and the leaf is filled per commune
                # by GeoCascade.js, which this scraper never runs. Take the
                # value that script would have chosen, or the NOT NULL
                # column would be posted empty and answer a 500.
                picked = db_cascade_value(table, name)
            fields[name] = picked
        else:
            # browser default: first option as rendered (None -> "")
            fields[name] = opts[0][0].get("value", "") if opts else ""
        seq += 1
    for m in TEXTAREA_RE.finditer(inner):
        a = attrs_of(m.group(1))
        name = a.get("name")
        if not name:
            continue
        kinds[name] = "textarea"
        if create:
            fields[name] = fake_text(name, seq)
            seq += 1
        else:
            fields[name] = m.group(2)
    return fields, kinds


def pick_marker_field(fields, kinds):
    """Field that receives the page-unique marker.

    Only plain text/textarea inputs qualify: a marker stuffed into an FK
    select is a constraint violation, into a number/date it is a type
    error, and into an enum name (status/...) a CHECK failure.
    """
    usable = [k for k in fields
              if kinds.get(k) in ("text", "textarea")
              and k.lower() not in ENUM_NAMES
              and not k.lower().endswith("status")]
    for pref in ("name", "title", "description"):
        if pref in usable:
            return pref
    for k in usable:
        n = k.lower()
        if "email" in n or "phone" in n or n.endswith("_at") or "date" in n:
            continue
        if any(h in n for h in MONEY_HINTS + QTY_HINTS):
            continue
        return k
    return None


def row_page_walk(page, rid, table, marker_field, create_only=False):
    """Find the page holding rid's row (its update form when there is
    one); return html or None."""
    for n in range(1, 41):
        path = page if n == 1 else f"{page}?page={n}"
        st, body, _ = c.get(path)
        if st != 200:
            return None
        html = body.decode()
        if f'id="md-m-{table}-{rid}"' in html:
            return html
        if create_only and f'data-row-id="{rid}"' in html:
            return html
        if f'data-md-master="{rid}"' in html and marker_field is None:
            return html
    return None


def main():
    st, _ = c.login_as("sd.staff1", "posadmin1")
    t.chk("contract login", st, 303)

    # Sidebar is the page inventory: every admin nav link is a master page.
    st, body, _ = c.get("/dashboard")
    t.chk("dashboard for page inventory", st, 200)
    html = body.decode()
    pages = []
    for href in re.findall(r'href="(/pos/[^"#?]*)"', html):
        if href not in pages:
            pages.append(href)
    t.chk("sidebar exposes master pages", len(pages) >= 13, True)

    exercised = 0
    for page in pages:
        slug = page.rsplit("/", 1)[-1]
        st, body, _ = c.get(page)
        t.chk(f"{slug}: page renders", st, 200)
        if st != 200:
            continue
        html = body.decode()
        a, inner = find_form(html, "create")
        if a is not None and a.get("data-md-kind") != "master":
            # Child add-row forms also post data-md-op="create" (kind
            # child); a master page whose own add form is gone (orders:
            # rows come from the sale flow) is exercised like a
            # read-only page.
            a, inner = None, None
        if a is None:
            print(f"SKIP: {page} (read-only master, no create form)")
            continue
        # create-only masters (create_only config) render the add form
        # but no edit button/update form; their row legs locate the row
        # by data-row-id instead of the update form's id.
        page_create_only = "master-edit-btn" not in html

        action = a.get("action", "")
        scope = a.get("data-md-scope", "")
        fields, kinds = parse_fields(inner, create=True, table=scope)
        if not fields:
            t.chk(f"{slug}: create form carries fields", len(fields) > 0, True)
            continue
        marker_field = pick_marker_field(fields, kinds)
        marker = f"CT13-{slug.upper()}"
        if marker_field:
            fields[marker_field] = marker

        st, hdr, _, _ = _post(action, fields)
        t.chk(f"{slug}: create form accepted (302)", st, 302)
        if st != 302:
            continue
        exercised += 1
        t.chk(f"{slug}: create redirects home",
              str(hdr.get("Location", "")).startswith(page), True)
        if marker_field:
            t.chk(f"{slug}: created row persisted",
                  db_live_count(scope, marker_field, marker), 1)
        elif kinds:
            # No plain text field to tag the row with: the create contract
            # is proven; the locate/update/delete legs need the marker.
            print(f"NOTE: {page} has no text field for a marker - "
                  "update/delete legs skipped")
            continue

        # ---- update: submit the row's form exactly as rendered ---------
        rid = _row_id_for(scope, marker_field, marker, page)
        t.chk(f"{slug}: created row locatable", bool(rid), True)
        if not rid:
            continue
        rhtml = row_page_walk(page, rid, scope, marker_field, page_create_only)
        t.chk(f"{slug}: row found in pager", bool(rhtml), True)
        if not rhtml:
            continue
        ua, uinner = find_form(rhtml, "update", rid)
        if ua is None and "master-edit-btn" not in rhtml:
            # create-only master (create_only config): no edit button and
            # no update route -- the create contract above is the story
            print(f"NOTE: {page} is create-only - update/delete legs skipped")
            continue
        t.chk(f"{slug}: update form rendered for the row", ua is not None, True)
        if ua:
            ufields, _ = parse_fields(uinner, create=False)
            ust, uhdr, _, _ = _post(ua.get("action", ""), ufields)
            t.chk(f"{slug}: update form accepted (302)", ust, 302)
            if ust == 302 and marker_field:
                t.chk(f"{slug}: row survived its own update",
                      db_live_count(scope, marker_field, marker), 1)

        # ---- cleanup: the row's delete form ----------------------------
        da, _dinner = find_form(rhtml, "delete", rid)
        if da:
            dst, _, _, _ = _post(da.get("action", ""), {})
            t.chk(f"{slug}: delete form accepted (302)", dst, 302)
            if dst == 302 and marker_field:
                t.chk(f"{slug}: row gone from live view",
                      db_live_count(scope, marker_field, marker), 0)

    t.chk("contract exercised the CRUD surface", exercised >= 13, True)

    # ---- geo cascade: lazy leaf + <noscript> escape hatch --------------
    # The village select ships only "None" server-side (GeoCascade.js fills
    # it per commune), so a scripting-off browser could never satisfy the
    # NOT NULL village_id on staff. The page answers a <noscript> meta
    # refresh to ?nojs=1, where the full list is rendered instead - and
    # must NOT re-offer that refresh, or it would bounce forever.
    st, body, _ = c.get("/pos/staff")
    html = body.decode()
    t.chk("staff page renders (cascade)", st, 200)
    leaf = re.search(r'<select[^>]*name="village_id"[^>]*>(.*?)</select>',
                     html, re.S)
    t.chk("cascade leaf ships no village options",
          len(re.findall(r"<option", leaf.group(1))) if leaf else -1, 1)
    refresh = re.search(r'content="0;url=([^"]*nojs=1[^"]*)"', html)
    t.chk("cascade page offers the nojs escape hatch", refresh is not None, True)
    if refresh:
        target = refresh.group(1)
        t.chk("nojs target keeps the path", target.split("?")[0], "/pos/staff")
        t.chk("nojs target carries the flag", target.endswith("nojs=1"), True)

    st, plain, _ = c.get("/pos/staff?nojs=1")
    ph = plain.decode()
    t.chk("nojs page renders (200)", st, 200)
    t.chk("nojs page does not re-offer the refresh (no loop)",
          re.search(r'content="0;url=[^"]*nojs=1', ph) is None, True)
    t.chk("nojs page drops the lazy dataset",
          re.search(r"data-geo-data>", ph) is None, True)
    leaf2 = re.search(r'<select[^>]*name="village_id"[^>]*>(.*?)</select>',
                      ph, re.S)
    n2 = len(re.findall(r"<option", leaf2.group(1))) if leaf2 else -1
    t.chk("nojs page ships the full village list", n2 > 14000, True)

    # The village column filters as free text in BOTH modes: its option
    # list is ~2 MB, and md_append_where already matches an FK column
    # against its live parent's label through an EXISTS subquery.
    t.chk("village filter is a text box (JS mode)",
          bool(re.search(r'<input[^>]*name="q_village_id"', html)), True)
    t.chk("village filter is a text box (nojs mode)",
          bool(re.search(r'<input[^>]*name="q_village_id"', ph)), True)


def _post(action, fields):
    body = urllib.parse.urlencode(fields).encode()
    return c.req_full(
        "POST", action, body,
        {"Content-Type": "application/x-www-form-urlencoded"},
    )


def _row_id_for(table, col, marker, page):
    """id of the created row: DB first, pager fallback."""
    if col:
        conn = sqlite3.connect(DB, timeout=10)
        try:
            conn.execute("PRAGMA busy_timeout=5000")
            row = conn.execute(
                f"SELECT id FROM {table} WHERE {col} = ?", (marker,)
            ).fetchone()
            if row:
                return row[0]
        finally:
            conn.close()
    # not queryable (odd column): scan pages for the marker value
    for n in range(1, 41):
        path = page if n == 1 else f"{page}?page={n}"
        st, body, _ = c.get(path)
        if st != 200:
            break
        m = re.search(
            r'<tr class="[^"]*" data-row-id="([^"]+)">(?:(?!</tr>).)*'
            + re.escape(marker),
            body.decode(), re.S,
        )
        if m:
            return m.group(1)
        if f'data-row-id=' not in body.decode():
            break
    return None


if __name__ == "__main__":
    main()
    ok = t.summary()
    sys.exit(0 if ok else 1)
