import { pageHref, type PageInfo } from "@/app/lib/paging";

// display/component/pagination.h.tt — the global pager markup included
// from every paged list (notes, users). Contract with
// public/js/PaginationSwitcher.js: data-pg-* on <nav>, data-pg-n on page
// links (fetched as ?fragment=1), plain <a> for per-page presets (full
// SSR reload, no JS needed).
export function Pagination({
  info,
  base,
  container,
  rawSearch,
}: {
  info: PageInfo;
  base: string;
  container: string;
  rawSearch: string;
}) {
  const pgBtn =
    "px-2 py-1 border border-gray-300 dark:border-gray-600 text-gray-600 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700";
  const pgCur = "px-2 py-1 bg-indigo-600 text-white";

  // 5-page window (pagination.h.tt): current +- 2, pinned to the tail.
  let pgFirst = 1;
  if (info.page > 3) pgFirst = info.page - 2;
  if (pgFirst + 4 > info.totalPages) {
    pgFirst = info.totalPages > 4 ? info.totalPages - 4 : 1;
  }
  let pgLast = pgFirst + 4;
  if (pgLast > info.totalPages) pgLast = info.totalPages;

  const href = (p: number, per: number): string =>
    pageHref(base, rawSearch, info, p, per);

  const window: number[] = [];
  for (let pgn = pgFirst; pgn <= pgLast; ++pgn) window.push(pgn);

  const presets = [10, 20, 50];
  const ppIsPreset = presets.includes(info.perPage);

  return (
    <nav
      className="mt-3 flex flex-wrap items-center justify-between gap-2 text-sm"
      data-pg-container={container}
      data-pg-key={info.pageKey}
      data-pg-per-key={info.perKey}
      data-pg-base={base}
      data-pg-per={info.perPage}
    >
      <span className="text-gray-500 dark:text-gray-400">
        Showing {info.rowStart}&ndash;{info.rowEnd} of {info.total} rows
      </span>
      <div className="flex items-center gap-1">
        {info.page > 1 ? (
          <a className={pgBtn} data-pg-n={info.page - 1} href={href(info.page - 1, info.perPage)}>
            Prev
          </a>
        ) : (
          <span className={`${pgBtn} opacity-40`}>Prev</span>
        )}

        {pgFirst > 1 && (
          <>
            <a className={pgBtn} data-pg-n={1} href={href(1, info.perPage)}>
              1
            </a>
            <span className="text-gray-400 px-1">&hellip;</span>
          </>
        )}

        {window.map((pgn) =>
          pgn === info.page ? (
            <span key={pgn} className={pgCur}>
              {pgn}
            </span>
          ) : (
            <a key={pgn} className={pgBtn} data-pg-n={pgn} href={href(pgn, info.perPage)}>
              {pgn}
            </a>
          ),
        )}

        {pgLast < info.totalPages && (
          <>
            <span className="text-gray-400 px-1">&hellip;</span>
            <a
              className={pgBtn}
              data-pg-n={info.totalPages}
              href={href(info.totalPages, info.perPage)}
            >
              {info.totalPages}
            </a>
          </>
        )}

        {info.page < info.totalPages ? (
          <a
            className={pgBtn}
            data-pg-n={info.page + 1}
            href={href(info.page + 1, info.perPage)}
          >
            Next
          </a>
        ) : (
          <span className={`${pgBtn} opacity-40`}>Next</span>
        )}

        <span className="text-gray-400 px-2">|</span>
        <span className="text-gray-500 dark:text-gray-400">Per page:</span>
        {presets.map((ppv) =>
          ppv === info.perPage ? (
            <span key={ppv} className={pgCur}>
              {ppv}
            </span>
          ) : (
            <a key={ppv} className={pgBtn} href={href(1, ppv)}>
              {ppv}
            </a>
          ),
        )}
        {!ppIsPreset && <span className={pgCur}>{info.perPage}</span>}
      </div>
    </nav>
  );
}
