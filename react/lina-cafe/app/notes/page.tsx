import { cookies, headers } from "next/headers";
import { AdminChrome } from "@/app/ui/admin-chrome";
import { Pagination } from "@/app/ui/pagination";
import { getI18n } from "@/app/lib/i18n";
import { queryAll, queryOne } from "@/app/lib/db";
import {
  PER_COOKIE,
  pageFragmentRequested,
  pageInfoFinish,
  pageInfoParseSession,
} from "@/app/lib/paging";

// GET /notes — webc_pos serve_notes_read (SERVE_READ(notes)) +
// display/notes.h.tt. ?fragment=1 returns only the table+nav swap markup
// (PaginationSwitcher.js); otherwise the page wears the admin chrome with
// title "Note".
type SearchParams = Promise<Record<string, string | string[] | undefined>>;

type NoteRow = {
  id: string;
  title: string | null;
  created_at: string | null;
  body: string | null;
};

export async function generateMetadata() {
  return { title: "Note" };
}

export default async function NotesPage({
  searchParams,
}: {
  searchParams: SearchParams;
}) {
  const sp = await searchParams;
  const jar = await cookies();
  const fragment = pageFragmentRequested(sp);

  const info = pageInfoParseSession(sp, jar.get(PER_COOKIE)?.value, "page");
  const total = Number(
    queryOne<{ c: number }>("SELECT count(*) AS c FROM Notes;")?.c ?? 0,
  );
  pageInfoFinish(info, total);
  const notes = queryAll<NoteRow>(
    "SELECT id, user_id, title, datetime(created_at, 'localtime') AS created_at, body " +
      "FROM Notes ORDER BY created_at DESC LIMIT ? OFFSET ?;",
    info.perPage,
    info.offset,
  );

  const { tr } = await getI18n();
  const h = await headers();
  const rawSearch = h.get("x-search") ?? "";

  const rows =
    notes.length === 0 ? (
      <tr>
        <td colSpan={5} className="px-4 py-4 text-sm text-gray-500 dark:text-gray-400">
          No notes in the database yet.
        </td>
      </tr>
    ) : (
      notes.map((n) => (
        <tr key={n.id}>
          <td className="px-4 py-2 text-gray-500 dark:text-gray-400">{n.id}</td>
          <td className="px-4 py-2 font-medium text-gray-800 dark:text-white">
            {n.title}
          </td>
          <td className="px-4 py-2 text-gray-600 dark:text-gray-300">
            {n.created_at}
          </td>
          <td className="px-4 py-2 text-gray-600 dark:text-gray-300">
            {n.body}
          </td>
          <td className="px-4 py-2 whitespace-nowrap">
            <a
              href={`/notes/${n.id}/edit`}
              className="mr-2 px-2 py-1 rounded text-xs bg-amber-500 hover:bg-amber-600 text-white"
            >
              Edit
            </a>
            <form method="POST" action={`/notes/${n.id}/delete`} className="inline">
              <button
                type="submit"
                className="px-2 py-1 rounded text-xs bg-red-500 hover:bg-red-600 text-white"
              >
                Delete
              </button>
            </form>
          </td>
        </tr>
      ))
    );

  if (fragment) {
    return (
      <div id="pg-frag">
        <table data-pg-rows="notes-tbody">
          <tbody id="notes-tbody">{rows}</tbody>
        </table>
        <Pagination
          info={info}
          base="/notes"
          container="notes-tbody"
          rawSearch={rawSearch}
        />
      </div>
    );
  }

  return (
    <AdminChrome title="Note">
      <div>
        <h2 className="text-2xl font-bold text-gray-800 dark:text-white mb-4">
          SQLite Notes
        </h2>

        <form
          method="POST"
          action="/notes/create"
          className="mb-4 flex gap-2 items-end bg-white dark:bg-gray-800 p-4 rounded-lg border border-gray-200 dark:border-gray-700"
        >
          <div>
            <label className="block text-sm text-gray-600 dark:text-gray-300 mb-1">
              Title
            </label>
            <input
              name="title"
              type="text"
              required
              className="px-3 py-2 rounded border border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-800 text-gray-800 dark:text-white"
            />
          </div>
          <div className="flex-1">
            <label className="block text-sm text-gray-600 dark:text-gray-300 mb-1">
              Body
            </label>
            <input
              name="body"
              type="text"
              className="w-full px-3 py-2 rounded border border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-800 text-gray-800 dark:text-white"
            />
          </div>
          <button
            type="submit"
            className="px-4 py-2 rounded bg-indigo-600 hover:bg-indigo-700 text-white font-medium"
          >
            Add note
          </button>
        </form>

        <table className="w-full text-left border-collapse overflow-hidden rounded-lg border border-gray-200 dark:border-gray-700 bg-white dark:bg-gray-800">
          <thead className="bg-indigo-600 text-white">
            <tr>
              <th className="px-4 py-3 font-semibold">#</th>
              <th className="px-4 py-3 font-semibold">{tr("Title", "Title")}</th>
              <th className="px-4 py-3 font-semibold">
                {tr("Created At", "Created At")}
              </th>
              <th className="px-4 py-3 font-semibold">{tr("Body", "Body")}</th>
              <th className="px-4 py-3 font-semibold">
                {tr("Actions", "Actions")}
              </th>
            </tr>
          </thead>
          <tbody
            id="notes-tbody"
            className="divide-y divide-gray-200 dark:divide-gray-700"
          >
            {rows}
          </tbody>
        </table>
        <Pagination
          info={info}
          base="/notes"
          container="notes-tbody"
          rawSearch={rawSearch}
        />
      </div>
    </AdminChrome>
  );
}
