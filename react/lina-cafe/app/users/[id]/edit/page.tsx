import { notFound } from "next/navigation";
import { AdminChrome } from "@/app/ui/admin-chrome";
import { queryOne } from "@/app/lib/db";
import { idValid } from "@/app/lib/crud";

// GET /users/<id>/edit — webc_pos SERVE_EDIT(users) + display/user_edit.h.tt.
type Params = { params: Promise<{ id: string }> };

export async function generateMetadata() {
  return { title: "Edit User" };
}

export default async function UserEditPage({ params }: Params) {
  const { id } = await params;
  if (!idValid(id)) notFound();

  const user = queryOne<{
    id: string;
    first_name: string | null;
    last_name: string | null;
    username: string | null;
    email: string | null;
    phone: string | null;
    profile_pic: string | null;
  }>(
    "SELECT id, first_name, last_name, username, email, phone, profile_pic " +
      "FROM Users WHERE id = ? LIMIT 1;",
    id,
  );
  if (!user) notFound();

  const hasPic = user.profile_pic !== null && user.profile_pic.length > 0;

  return (
    <AdminChrome title="Edit User">
      <div>
        <h2 className="text-2xl font-bold text-gray-800 dark:text-white mb-4">
          Edit User ID #{user.id}
        </h2>

        <form
          method="POST"
          action={`/users/${user.id}/update`}
          encType="multipart/form-data"
          className="mb-4 flex gap-2 items-end bg-white dark:bg-gray-800 p-4 rounded-lg border border-gray-200 dark:border-gray-700"
        >
          <div>
            <label className="block text-sm text-gray-600 dark:text-gray-300 mb-1">First Name</label>
            <input
              name="first_name"
              type="text"
              required
              value={user.first_name ?? ""}
              className="px-3 py-2 rounded border border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-800 text-gray-800 dark:text-white"
            />
          </div>
          <div>
            <label className="block text-sm text-gray-600 dark:text-gray-300 mb-1">Last Name</label>
            <input
              name="last_name"
              type="text"
              required
              value={user.last_name ?? ""}
              className="px-3 py-2 rounded border border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-800 text-gray-800 dark:text-white"
            />
          </div>
          <div>
            <label className="block text-sm text-gray-600 dark:text-gray-300 mb-1">Username</label>
            <input
              name="username"
              type="text"
              value={user.username ?? ""}
              className="px-3 py-2 rounded border border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-800 text-gray-800 dark:text-white"
            />
          </div>
          <div>
            <label className="block text-sm text-gray-600 dark:text-gray-300 mb-1">Email</label>
            <input
              name="email"
              type="text"
              value={user.email ?? ""}
              className="px-3 py-2 rounded border border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-800 text-gray-800 dark:text-white"
            />
          </div>
          <div>
            <label className="block text-sm text-gray-600 dark:text-gray-300 mb-1">Phone</label>
            <input
              name="phone"
              type="text"
              required
              value={user.phone ?? ""}
              className="px-3 py-2 rounded border border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-800 text-gray-800 dark:text-white"
            />
          </div>
          <div>
            <label className="block text-sm text-gray-600 dark:text-gray-300 mb-1">
              User Profile Picture
            </label>
            {hasPic && (
              <div className="flex items-center gap-2 mb-1">
                <img
                  src={user.profile_pic!}
                  alt="current"
                  className="w-8 h-8 rounded-full object-cover ring-2 ring-gray-200 dark:ring-gray-600"
                />
                <span className="text-xs text-gray-500 dark:text-gray-400 truncate max-w-[12rem]">
                  {user.profile_pic}
                </span>
              </div>
            )}
            {/* data-img-upload = js/ImageUpload.js: icon button + preview +
                crop + compression. */}
            <div className="flex items-center gap-3" data-img-upload="">
              <div className="relative w-14 h-14 rounded-full overflow-hidden bg-gray-100 dark:bg-gray-700 border border-gray-200 dark:border-gray-600 flex items-center justify-center shrink-0">
                <svg
                  data-img-placeholder=""
                  className="w-6 h-6 text-gray-400"
                  fill="none"
                  stroke="currentColor"
                  viewBox="0 0 24 24"
                >
                  <path
                    strokeLinecap="round"
                    strokeLinejoin="round"
                    strokeWidth="2"
                    d="M4 16l4.586-4.586a2 2 0 012.828 0L16 16m-2-2l1.586-1.586a2 2 0 012.828 0L20 14m-6-6h.01M6 20h12a2 2 0 002-2V6a2 2 0 00-2-2H6a2 2 0 00-2 2v12a2 2 0 002 2z"
                  />
                </svg>
                <img
                  data-img-preview=""
                  alt="Selected photo"
                  className="hidden absolute inset-0 w-full h-full object-cover"
                />
              </div>
              <div className="flex flex-col gap-1 items-start">
                <label className="inline-flex items-center gap-2 px-3 py-1.5 rounded bg-green-600 hover:bg-green-700 text-white text-sm font-medium cursor-pointer">
                  <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path
                      strokeLinecap="round"
                      strokeLinejoin="round"
                      strokeWidth="2"
                      d="M4 16l4.586-4.586a2 2 0 012.828 0L16 16m-2-2l1.586-1.586a2 2 0 012.828 0L20 14m-6-6h.01M6 20h12a2 2 0 002-2V6a2 2 0 00-2-2H6a2 2 0 00-2 2v12a2 2 0 002 2z"
                    />
                  </svg>
                  Choose image
                  <input
                    name="profile_pic"
                    type="file"
                    accept="image/png,image/jpeg,image/gif,image/webp"
                    className="sr-only"
                    data-img-input=""
                  />
                </label>
                <div className="flex items-center gap-2 text-xs">
                  <button
                    type="button"
                    data-img-crop=""
                    className="hidden px-2 py-1 rounded border border-gray-300 dark:border-gray-600 text-gray-600 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700 cursor-pointer"
                  >
                    Crop
                  </button>
                  <span data-img-name="" className="text-gray-500 dark:text-gray-400">
                    no file selected
                  </span>
                </div>
              </div>
            </div>
            <p className="text-xs text-gray-500 dark:text-gray-400 mt-1">
              Leave empty to keep the current picture.
            </p>
          </div>
          <button
            type="submit"
            className="px-4 py-2 rounded bg-green-600 hover:bg-green-700 text-white font-medium"
          >
            Save
          </button>
          <a
            href="/users"
            className="px-4 py-2 rounded bg-gray-500 hover:bg-gray-600 text-white font-medium"
          >
            Cancel
          </a>
        </form>
      </div>
    </AdminChrome>
  );
}
