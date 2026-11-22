import { notFound } from "next/navigation";
import { AdminChrome } from "@/app/ui/admin-chrome";
import { queryOne } from "@/app/lib/db";
import { idValid } from "@/app/lib/crud";

// GET /notes/<id>/edit — webc_pos SERVE_EDIT(notes) + display/notes_edit.h.tt.
type Params = { params: Promise<{ id: string }> };

export async function generateMetadata() {
  return { title: "Edit Note" };
}

export default async function NoteEditPage({ params }: Params) {
  const { id } = await params;
  if (!idValid(id)) notFound();

  const note = queryOne<{
    id: string;
    title: string | null;
    body: string | null;
  }>(
    "SELECT id, title, body FROM Notes WHERE id = ? LIMIT 1;",
    id,
  );
  if (!note) notFound();

  return (
    <AdminChrome title="Edit Note">
      <div>
        <h2 className="text-2xl font-bold text-gray-800 dark:text-white mb-4">
          {"Edit Note #{ "}
          {note.id}
          {" }"}
        </h2>

        <form
          method="POST"
          action={`/notes/${note.id}/update`}
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
              value={note.title ?? ""}
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
              value={note.body ?? ""}
              className="w-full px-3 py-2 rounded border border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-800 text-gray-800 dark:text-white"
            />
          </div>
          <button
            type="submit"
            className="px-4 py-2 rounded bg-green-600 hover:bg-green-700 text-white font-medium"
          >
            Save
          </button>
          <a
            href="/notes"
            className="px-4 py-2 rounded bg-gray-500 hover:bg-gray-600 text-white font-medium"
          >
            Cancel
          </a>
        </form>
      </div>
    </AdminChrome>
  );
}
