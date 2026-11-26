// Paging: port of webc_pos src/layout/paging.c — page window math, the
// webc_per_page session size, the fragment flag and page_href (the link
// builder PaginationSwitcher.js consumes).

export const PAGE_DEFAULT_PER_PAGE = 20;
export const PAGE_MAX_PER_PAGE = 500;
export const PER_COOKIE = "webc_per_page";
export const PER_COOKIE_MAX_AGE = 365 * 24 * 60 * 60;

export type PageInfo = {
  pageKey: string; // query key for page number
  perKey: string; // query key for page size
  page: number; // 1-based, clamped to [1, totalPages] by finish()
  perPage: number; // clamped to [1, PAGE_MAX_PER_PAGE]
  offset: number; // 0-based SQL OFFSET
  rowStart: number; // 1-based first display row (0 when total = 0)
  rowEnd: number; // 1-based last display row (0 when total = 0)
  total: number; // row count, set by pageInfoFinish()
  totalPages: number; // ceil(total/perPage), always >= 1
};

export type Query = Record<string, string | string[] | undefined>;

// First value of a query key (arrays keep the first entry, like a browser
// rebuilding the URL).
export function firstOf(q: Query, key: string): string | null {
  const v = q[key];
  if (Array.isArray(v)) return v.length > 0 ? v[0] : null;
  return v === undefined ? null : v;
}

// webc_pos page_size_from_sv: digits only; empty, zero or non-numeric ->
// null (caller keeps its default); huge/overflowing -> PAGE_MAX_PER_PAGE
// (webc's SIZE_MAX, clamped on assignment).
export function sizeFromValue(v: string | null): number | null {
  if (v === null || v.length === 0) return null;
  for (const ch of v) {
    if (ch < "0" || ch > "9") return null;
  }
  if (v.length >= 21) return PAGE_MAX_PER_PAGE;
  const n = Number(v);
  if (!Number.isFinite(n) || n < 1) return null;
  return n;
}

// webc_pos page_info_parse(): ?<pageKey> and ?<perKey> out of the query,
// invalid -> defaults (page 1, 20 rows).
export function pageInfoParse(
  q: Query,
  pageKey = "page",
  perKey = "per_page",
): PageInfo {
  const info: PageInfo = {
    pageKey: pageKey.length > 0 ? pageKey : "page",
    perKey: perKey.length > 0 ? perKey : "per_key",
    page: 1,
    perPage: PAGE_DEFAULT_PER_PAGE,
    offset: 0,
    rowStart: 0,
    rowEnd: 0,
    total: 0,
    totalPages: 1,
  };
  const pv = sizeFromValue(firstOf(q, info.pageKey));
  if (pv !== null) info.page = pv;
  const qv = sizeFromValue(firstOf(q, info.perKey));
  if (qv !== null) info.perPage = Math.min(qv, PAGE_MAX_PER_PAGE);
  return info;
}

// webc_pos page_info_parse_session(): the query's ?per_page= wins (the
// proxy has already stored it as the session value); absent or malformed,
// the saved webc_per_page cookie (re-clamped — a hand-edited cookie must
// not break the window), else the default.
export function pageInfoParseSession(
  q: Query,
  cookie: string | undefined,
  pageKey = "page",
): PageInfo {
  const info = pageInfoParse(q, pageKey, "per_page");
  if (sizeFromValue(firstOf(q, "per_page")) !== null) return info;
  const cv = sizeFromValue(cookie ?? null);
  if (cv !== null) info.perPage = Math.min(cv, PAGE_MAX_PER_PAGE);
  return info;
}

// webc_pos page_info_finish(): clamp the window to the real row count.
export function pageInfoFinish(info: PageInfo, total: number): void {
  if (info.perPage < 1) info.perPage = PAGE_DEFAULT_PER_PAGE;
  info.total = total;
  info.totalPages = total === 0 ? 1 : Math.floor((total - 1) / info.perPage) + 1;
  if (info.page < 1) info.page = 1;
  if (info.page > info.totalPages) info.page = info.totalPages;
  info.offset = (info.page - 1) * info.perPage;
  if (total === 0) {
    info.rowStart = 0;
    info.rowEnd = 0;
  } else {
    info.rowStart = info.offset + 1;
    info.rowEnd =
      info.perPage < total - info.offset ? info.offset + info.perPage : total;
  }
}

// webc_pos page_fragment_requested(): ?fragment=1 asks for the bare
// table+nav swap markup (PaginationSwitcher.js fetches these).
export function pageFragmentRequested(q: Query): boolean {
  return firstOf(q, "fragment") === "1";
}

// webc_pos sb_append_url_attr_safe(): percent-encode the attribute-unsafe
// bytes only (`" ' < > \`, space, 0x7f) — the raw query is already
// percent-encoded as served.
function attrSafe(src: string): string {
  let out = "";
  for (const ch of src) {
    const c = ch.charCodeAt(0);
    if (
      c === 0x22 ||
      c === 0x27 ||
      c === 0x3c ||
      c === 0x3e ||
      c === 0x5c ||
      c === 0x20 ||
      c === 0x7f
    ) {
      out += "%" + c.toString(16).toUpperCase().padStart(2, "0");
    } else {
      out += ch;
    }
  }
  return out;
}

// webc_pos page_href(): base + every current query pair except the page
// key, the per key and `fragment`, then the new window. `rawSearch` is the
// query exactly as served (x-search, leading '?' optional).
export function pageHref(
  base: string,
  rawSearch: string,
  info: PageInfo,
  page: number,
  perPage: number,
): string {
  let out = attrSafe(base);
  let sep = "?";
  const query = rawSearch.startsWith("?") ? rawSearch.slice(1) : rawSearch;
  for (const pair of query.split("&")) {
    if (pair.length === 0) continue;
    const eq = pair.indexOf("=");
    const name = eq >= 0 ? pair.slice(0, eq) : pair;
    if (name === info.pageKey || name === info.perKey || name === "fragment") {
      continue;
    }
    out += sep + attrSafe(pair);
    sep = "&";
  }
  out += sep + `${info.pageKey}=${page}&${info.perKey}=${perPage}`;
  return out;
}
