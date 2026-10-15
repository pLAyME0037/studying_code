#!/usr/bin/env python3
"""wiki2impress.py — convert the webc_sqlite_ui wiki (Markdown) into an
Impress presentation (.odp), optionally also .pptx.

Stdlib only. Reads every ``wiki/*.md`` page (README first, then alphabetical),
turns each ``#`` heading into a title slide and each ``##`` heading into a
content slide (paginating long sections), and writes a valid ODP package:

    mimetype (stored first, uncompressed)
    META-INF/manifest.xml
    content.xml  styles.xml  meta.xml

Each slide carries a ``presentation:notes`` frame with the raw markdown
source of its section (disable with --no-notes).

Usage:
    python3 wiki2impress.py [WIKI_DIR] [-o OUT.odp] [--title TITLE]
                            [--max-lines N] [--no-notes] [--pptx]
"""
from __future__ import annotations

import argparse
import datetime
import html
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from xml.etree import ElementTree as ET

MIMETYPE = "application/vnd.oasis.opendocument.presentation"
SLIDE_W, SLIDE_H = "13.333in", "7.5in"   # 16:9
DEFAULT_TITLE = "webc_sqlite_ui Wiki"

# ---------------------------------------------------------------- markdown --

HEADING_RE = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
BULLET_RE = re.compile(r"^(\s*)([-*+]|\d+[.)])\s+(.*)$")
TABLE_SEP_RE = re.compile(r"^\|?[\s:|-]+\|?$")
FENCE_RE = re.compile(r"^```")


def md_inline(s: str) -> str:
    """Escape XML, then apply the small inline-markdown subset we use."""
    s = html.escape(s, quote=False)
    s = s.replace("\\|", "|")                                 # escaped table pipes
    s = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", s)          # [label](url) -> label
    s = re.sub(r"`([^`]+)`",
               r'<text:span text:style-name="spanMono">\1</text:span>', s)
    s = re.sub(r"\*\*([^*]+)\*\*",
               r'<text:span text:style-name="spanBold">\1</text:span>', s)
    return s


# backtick code spans and **bold** spans are wrapped atomically so a line
# break can never land inside them (which would split the XML span)
ATOM_RE = re.compile(r"`[^`]*`|\*\*[^*]+\*\*|\S+")


def wrap_raw(style: str, text: str, width: int = 104) -> list[str]:
    """Word-wrap a raw markdown line, keeping `code`/`**bold**` spans whole."""
    if len(text) <= width:
        return [text]
    tokens: list[tuple[bool, str]] = []   # (had_space_before, token)
    last = 0
    for m in ATOM_RE.finditer(text):
        tokens.append((m.start() > last, m.group()))
        last = m.end()
    lines: list[str] = []
    cur = ""
    for gap, tok in tokens:
        cand = cur + (" " if cur and gap else "") + tok
        if cur and len(cand) > width:
            lines.append(cur)
            cur = tok
        else:
            cur = cand
    if cur:
        lines.append(cur)
    return lines or [text]


def parse_page(text: str) -> list[dict]:
    """One markdown page -> sections: {kind, title, items[(style, raw)], notes}.

    Soft-wrapped continuation lines are joined back into the open item, so
    inline markup (``**bold**`` across a source line break) stays whole and
    wrapping happens once, against the complete line.
    """
    sections: list[dict] = []
    cur: dict | None = None
    pending: list[str] | None = None      # [style, raw] of the open item
    in_code = False
    prev_table = False

    def flush() -> None:
        nonlocal pending
        if pending and cur is not None:
            style, raw = pending
            for piece in wrap_raw(style, raw):
                cur["items"].append((style, piece))
        pending = None

    def open_item(style: str, raw: str) -> None:
        nonlocal pending
        flush()
        pending = [style, raw]

    def append_item(s: str) -> None:
        nonlocal pending
        if pending is None:
            open_item("pBody", s)
        else:
            pending[1] += " " + s

    def new_section(kind: str, title: str, note: str) -> None:
        nonlocal cur, prev_table
        flush()
        cur = {"kind": kind, "title": title, "items": [], "notes": [note]}
        sections.append(cur)
        prev_table = False

    def emit(style: str, raw: str) -> None:
        if cur is None:
            new_section("title", "wiki", "")
        for piece in wrap_raw(style, raw):
            cur["items"].append((style, piece))

    for raw in text.splitlines():
        line = raw.rstrip()
        if FENCE_RE.match(line.strip()):
            flush()
            in_code = not in_code
            prev_table = False
            continue
        if in_code:
            emit("pCode", raw if raw.strip() else " ")
            continue
        if not line.strip():
            flush()
            prev_table = False
            continue
        m = HEADING_RE.match(line)
        if m:
            level, title = len(m.group(1)), m.group(2)
            if level == 1:
                new_section("title", title, line)
            elif level == 2:
                new_section("content", title, line)
            else:
                flush()
                emit("pH3", "▸ " + title)
            prev_table = False
            continue
        stripped = line.strip()
        if stripped.startswith("|"):
            if TABLE_SEP_RE.match(stripped):
                continue                      # separator row keeps header state
            flush()
            emit("pCodeHead" if not prev_table else "pCode", stripped)
            prev_table = True
            continue
        prev_table = False
        if line.startswith(("---", "***")) and set(line) <= {"-", "*"}:
            flush()
            continue
        if line.startswith(">"):
            open_item("pQuote", line.lstrip("> ").strip())
            continue
        m = BULLET_RE.match(line)
        if m:
            indent, marker, body = m.groups()
            level = min(len(indent.replace("\t", "  ")) // 2, 2)
            if marker[0].isdigit():
                open_item(f"pBul{level}", f"{marker} {body}")
            else:
                prefix = ("• ", "– ", "· ")[level]
                open_item(f"pBul{level}", prefix + body)
            continue
        append_item(stripped)
    flush()

    return [s for s in sections if s["items"] or s["kind"] == "title"]


def paginate(sections: list[dict], max_lines: int) -> list[dict]:
    """Split over-long sections into '(cont.)' slides."""
    out: list[dict] = []
    for sec in sections:
        chunk: list[tuple[str, str]] = []
        first = True
        for item in sec["items"]:
            if chunk and len(chunk) >= max_lines:
                out.append(_slice(sec, chunk, first))
                chunk, first = [], False
            chunk.append(item)
        out.append(_slice(sec, chunk, first))
    return out


def _slice(sec: dict, items: list, first: bool) -> dict:
    title = sec["title"] if first else sec["title"] + " (cont.)"
    return {
        "kind": sec["kind"] if first else "content",
        "title": title,
        "items": items,
        "notes": sec["notes"] if first else [],
    }


# ------------------------------------------------------------------ ODP XML --

FONT_FACES = (
    '<office:font-face-decls>'
    "<style:font-face style:name=\"Sans\" "
    "svg:font-family=\"'Liberation Sans', Arial, sans-serif\"/>"
    "<style:font-face style:name=\"Mono\" "
    "svg:font-family=\"'Liberation Mono', Consolas, monospace\"/>"
    "</office:font-face-decls>"
)

CONTENT_STYLES = f"""
{FONT_FACES}
<office:automatic-styles>
 <style:style style:name="dp1" style:family="drawing-page">
  <style:drawing-page-properties draw:fill="solid" draw:fill-color="#ffffff"
   presentation:background-visible="true" presentation:background-objects-visible="true"/>
 </style:style>
 <style:style style:name="grFrame" style:family="graphic">
  <style:graphic-properties draw:stroke="none" draw:fill="none" fo:wrap-option="wrap"/>
 </style:style>
 <style:style style:name="pTitle" style:family="paragraph">
  <style:paragraph-properties fo:text-align="center" fo:line-height="120%"/>
  <style:text-properties style:font-name="Sans" fo:font-size="32pt"
   fo:font-weight="bold" fo:color="#16213e"/>
 </style:style>
 <style:style style:name="pH2" style:family="paragraph">
  <style:paragraph-properties fo:line-height="115%" fo:margin-bottom="0.04in"/>
  <style:text-properties style:font-name="Sans" fo:font-size="23pt"
   fo:font-weight="bold" fo:color="#0f3460"/>
 </style:style>
 <style:style style:name="pH3" style:family="paragraph">
  <style:paragraph-properties fo:margin-top="0.10in" fo:margin-bottom="0.03in"/>
  <style:text-properties style:font-name="Sans" fo:font-size="15.5pt"
   fo:font-weight="bold" fo:color="#3d3d3d"/>
 </style:style>
 <style:style style:name="pBody" style:family="paragraph">
  <style:paragraph-properties fo:line-height="130%" fo:margin-bottom="0.05in"/>
  <style:text-properties style:font-name="Sans" fo:font-size="15pt" fo:color="#222222"/>
 </style:style>
 <style:style style:name="pBul0" style:family="paragraph">
  <style:paragraph-properties fo:line-height="125%" fo:margin-left="0.20in"
   fo:margin-bottom="0.03in"/>
  <style:text-properties style:font-name="Sans" fo:font-size="15pt" fo:color="#222222"/>
 </style:style>
 <style:style style:name="pBul1" style:family="paragraph">
  <style:paragraph-properties fo:line-height="125%" fo:margin-left="0.62in"
   fo:margin-bottom="0.02in"/>
  <style:text-properties style:font-name="Sans" fo:font-size="14.5pt" fo:color="#333333"/>
 </style:style>
 <style:style style:name="pBul2" style:family="paragraph">
  <style:paragraph-properties fo:line-height="125%" fo:margin-left="1.04in"
   fo:margin-bottom="0.02in"/>
  <style:text-properties style:font-name="Sans" fo:font-size="14pt" fo:color="#444444"/>
 </style:style>
 <style:style style:name="pCode" style:family="paragraph">
  <style:paragraph-properties fo:line-height="115%" fo:margin-bottom="0.02in"
   fo:background-color="#f6f8fa"/>
  <style:text-properties style:font-name="Mono" fo:font-size="12.5pt" fo:color="#1d3557"/>
 </style:style>
 <style:style style:name="pCodeHead" style:family="paragraph">
  <style:paragraph-properties fo:line-height="115%" fo:margin-bottom="0.02in"
   fo:background-color="#e8eef5"/>
  <style:text-properties style:font-name="Mono" fo:font-size="12.5pt"
   fo:font-weight="bold" fo:color="#0b3d5c"/>
 </style:style>
 <style:style style:name="pQuote" style:family="paragraph">
  <style:paragraph-properties fo:line-height="125%" fo:margin-left="0.5in"
   fo:margin-bottom="0.05in"/>
  <style:text-properties style:font-name="Sans" fo:font-size="14.5pt"
   fo:font-style="italic" fo:color="#555555"/>
 </style:style>
 <style:style style:name="spanMono" style:family="text">
  <style:text-properties style:font-name="Mono" fo:font-size="13pt" fo:color="#224466"/>
 </style:style>
 <style:style style:name="spanBold" style:family="text">
  <style:text-properties fo:font-weight="bold"/>
 </style:style>
</office:automatic-styles>"""

STYLES_XML = f"""<?xml version="1.0" encoding="UTF-8"?>
<office:document-styles
 xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
 xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
 xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
 office:version="1.3">
{FONT_FACES}
 <office:automatic-styles>
  <style:page-layout style:name="pm1">
   <style:page-layout-properties fo:page-width="{SLIDE_W}" fo:page-height="{SLIDE_H}"
    style:print-orientation="landscape" fo:margin-top="0in" fo:margin-bottom="0in"
    fo:margin-left="0in" fo:margin-right="0in"/>
  </style:page-layout>
 </office:automatic-styles>
 <office:master-styles>
  <style:master-page style:name="Default" style:page-layout-name="pm1"/>
 </office:master-styles>
</office:document-styles>"""


def frame(x: float, y: float, w: float, h: float, paras: list[str]) -> str:
    if not paras:
        return ""
    return (
        f'<draw:frame draw:style-name="grFrame" draw:layer="layout" '
        f'svg:x="{x}in" svg:y="{y}in" svg:width="{w}in" svg:height="{h}in">'
        f'<draw:text-box>{"".join(paras)}</draw:text-box></draw:frame>'
    )


def page_xml(idx: int, sec: dict, notes_on: bool) -> str:
    title = md_inline(sec["title"])
    paras = [f'<text:p text:style-name="{st}">{md_inline(tx)}</text:p>'
             for st, tx in sec["items"]]
    if sec["kind"] == "title":
        frames = frame(0.8, 2.45, 11.7, 1.5,
                       [f'<text:p text:style-name="pTitle">{title}</text:p>'])
        frames += frame(1.4, 4.2, 10.5, 2.7, paras)
    else:
        frames = frame(0.55, 0.32, 12.2, 1.0,
                       [f'<text:p text:style-name="pH2">{title}</text:p>'])
        frames += frame(0.55, 1.45, 12.2, 5.8, paras)
    notes = ""
    if notes_on and sec["notes"]:
        nparas = "".join(
            f'<text:p text:style-name="pCode">{html.escape(n, quote=False)}</text:p>'
            for n in sec["notes"])
        notes = (
            "<presentation:notes>"
            '<draw:frame draw:layer="notes" draw:style-name="grFrame"'
            ' svg:x="0.79in" svg:y="0.79in" svg:width="11.75in" svg:height="6in">'
            f"<draw:text-box>{nparas}</draw:text-box></draw:frame>"
            "</presentation:notes>"
        )
    return (
        f'<draw:page draw:name="page{idx}" draw:style-name="dp1"'
        f' draw:master-page-name="Default">{frames}{notes}</draw:page>'
    )


def content_xml(pages: list[str]) -> str:
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<office:document-content
 xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"
 xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"
 xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"
 xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
 xmlns:presentation="urn:oasis:names:tc:opendocument:xmlns:presentation:1.0"
 office:version="1.3">{CONTENT_STYLES}
 <office:body><office:presentation>
{"".join(pages)}
 </office:presentation></office:body>
</office:document-content>"""


def meta_xml(title: str) -> str:
    now = datetime.datetime.now().replace(microsecond=0).isoformat()
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<office:document-meta
 xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
 xmlns:dc="http://purl.org/dc/elements/1.1/"
 xmlns:meta="urn:oasis:names:tc:opendocument:xmlns:meta:1.0"
 office:version="1.3">
 <office:meta>
  <dc:title>{html.escape(title, quote=False)}</dc:title>
  <meta:generator>wiki2impress.py</meta:generator>
  <meta:creation-date>{now}</meta:creation-date>
 </office:meta>
</office:document-meta>"""


MANIFEST_XML = f"""<?xml version="1.0" encoding="UTF-8"?>
<manifest:manifest
 xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0"
 manifest:version="1.3">
 <manifest:file-entry manifest:full-path="/"
  manifest:version="1.3" manifest:media-type="{MIMETYPE}"/>
 <manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>
 <manifest:file-entry manifest:full-path="styles.xml" manifest:media-type="text/xml"/>
 <manifest:file-entry manifest:full-path="meta.xml" manifest:media-type="text/xml"/>
</manifest:manifest>"""


# ------------------------------------------------------------------ package --

def write_odp(out: Path, title: str, pages: list[str]) -> None:
    content, styles, meta = content_xml(pages), STYLES_XML, meta_xml(title)
    for blob in (content, styles, meta, MANIFEST_XML):
        ET.fromstring(blob)                     # fail fast on malformed XML
    with zipfile.ZipFile(out, "w") as z:
        zi = zipfile.ZipInfo("mimetype")
        zi.compress_type = zipfile.ZIP_STORED   # spec: first, uncompressed
        z.writestr(zi, MIMETYPE)
        for name, data in (
            ("META-INF/manifest.xml", MANIFEST_XML),
            ("content.xml", content),
            ("styles.xml", styles),
            ("meta.xml", meta),
        ):
            z.writestr(name, data, zipfile.ZIP_DEFLATED)


def to_pptx(odp: Path) -> None:
    soffice = shutil.which("soffice")
    if not soffice:
        print("--pptx: LibreOffice (soffice) not found, skipped", file=sys.stderr)
        return
    with tempfile.TemporaryDirectory(prefix="wiki2imp-") as prof:
        proc = subprocess.run(
            [soffice, "--headless", f"-env:UserInstallation=file://{prof}",
             "--convert-to", "pptx", "--outdir", str(odp.parent), str(odp)],
            capture_output=True, text=True, timeout=180)
        out = odp.with_suffix(".pptx")
        if proc.returncode == 0 and out.exists():
            print(f"wrote {out}")
        else:
            print(f"--pptx conversion failed: {proc.stderr.strip()[:400]}",
                  file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("wiki", nargs="?", default=None,
                    help="wiki directory (default: parent of this script)")
    ap.add_argument("-o", "--out", default=None, help="output .odp path")
    ap.add_argument("--title", default=DEFAULT_TITLE)
    ap.add_argument("--max-lines", type=int, default=12,
                    help="body lines per slide before '(cont.)' pagination")
    ap.add_argument("--include-all", action="store_true",
                    help="include appendix pages (file names starting with 9)")
    ap.add_argument("--no-notes", action="store_true",
                    help="omit speaker notes frames")
    ap.add_argument("--pptx", action="store_true",
                    help="also convert to .pptx via headless LibreOffice")
    args = ap.parse_args()

    wiki = Path(args.wiki) if args.wiki else Path(__file__).resolve().parent.parent
    pages = sorted(wiki.glob("*.md"),
                   key=lambda p: (p.name != "README.md", p.name))
    if not args.include_all:
        skipped = [p for p in pages if p.name.startswith("9")]
        pages = [p for p in pages if p not in skipped]
        if skipped:
            print("appendix skipped: " + ", ".join(p.name for p in skipped))
    if not pages:
        print(f"no markdown pages in {wiki}", file=sys.stderr)
        return 1

    sections: list[dict] = []
    for p in pages:
        sections += paginate(parse_page(p.read_text(encoding="utf-8")),
                             args.max_lines)
    xml_pages = [page_xml(i + 1, s, not args.no_notes)
                 for i, s in enumerate(sections)]

    out = Path(args.out) if args.out else wiki / "webc_sqlite_wiki.odp"
    write_odp(out, args.title, xml_pages)
    print(f"wrote {out} — {len(xml_pages)} slides from {len(pages)} pages")
    if args.pptx:
        to_pptx(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
