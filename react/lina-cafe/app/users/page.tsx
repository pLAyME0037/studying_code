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

// GET /users — webc_pos serve_users_read (SERVE_READ(users)) +
// display/user.h.tt. ?fragment=1 returns only the table+nav swap markup
// (PaginationSwitcher.js); otherwise the create form + table wear the admin
// chrome with title "Users".
type SearchParams = Promise<Record<string, string | string[] | undefined>>;

type UserRow = {
  id: string;
  name: string | null;
  first_name: string | null;
  last_name: string | null;
  username: string | null;
  email: string | null;
  phone: string | null;
  profile_pic: string | null;
};

export async function generateMetadata() {
  return { title: "Users" };
}

// user.h.tt's inline avatar fallback (verbatim data URI).
const PLACEHOLDER_PIC =
  "data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 40 40'%3E%3Ccircle cx='20' cy='15' r='7' fill='%237F9CF5'/%3E%3Cpath d='M6 36c0-8 6-13 14-13s14 5 14 13' fill='%237F9CF5'/%3E%3C/svg%3E";

const TH_CLASS =
  "px-6 py-3 text-xs font-medium text-gray-500 dark:text-gray-300 uppercase tracking-wider w-auto text-left";

export default async function UsersPage({ searchParams }: { searchParams: SearchParams }) {
  const sp = await searchParams;
  const jar = await cookies();
  const fragment = pageFragmentRequested(sp);

  const info = pageInfoParseSession(sp, jar.get(PER_COOKIE)?.value, "page");
  const total = Number(
    queryOne<{ c: number }>("SELECT count(*) AS c FROM Users;")?.c ?? 0,
  );
  pageInfoFinish(info, total);
  const users = queryAll<UserRow>(
    "SELECT id, name, first_name, last_name, username, email, phone, profile_pic " +
      "FROM Users ORDER BY id ASC LIMIT ? OFFSET ?;",
    info.perPage,
    info.offset,
  );

  const { tr } = await getI18n();
  const h = await headers();
  const rawSearch = h.get("x-search") ?? "";

  const rows =
    users.length === 0 ? (
      <tr>
        {/* webc renders colspan="" — the HTML parser reads it back as the
            default 1; React's types only accept numbers, so the raw value
            rides a cast. */}
        <td
          colSpan={"" as unknown as number}
          className="px-6 py-12 text-center text-gray-500 dark:text-gray-400"
        >
          <div className="flex flex-col items-center justify-center">
            <svg
              className="w-12 h-12 text-gray-300 mb-3"
              fill="none"
              stroke="currentColor"
              viewBox="0 0 24 24"
            >
              <path
                strokeLinecap="round"
                strokeLinejoin="round"
                strokeWidth="2"
                d="M20 13V6a2 2 0 00-2-2H6a2 2 0 00-2 2v7m16 0v5a2 2 0 01-2 2H6a2 2 0 01-2-2v-5m16 0h-2.586a1 1 0 00-.707.293l-2.414 2.414a1 1 0 01-.707.293h-3.172a1 1 0 01-.707-.293l-2.414-2.414A1 1 0 006.586 13H4"
              />
            </svg>
            <span className="text-lg font-medium">No records found</span>
          </div>
        </td>
      </tr>
    ) : (
      users.map((u) => {
        const hasPic = u.profile_pic !== null && u.profile_pic.length > 0;
        return (
          <tr
            key={u.id}
            className="hover:bg-gray-50 dark:hover:bg-gray-700 transition-colors"
          >
            <div className="flex flex-col gap-1 items-start">
              <td className="px-6 py-4 whitespace-nowrap dark:text-white text-left">
                {u.id}
              </td>
            </div>
            <div className="flex flex-col gap-1 items-center">
              <td className="px-6 py-4 whitespace-nowrap text-left">
                <div className="w-10 h-10 relative rounded-full ring-4 ring-white dark:ring-gray-800 shadow-sm bg-gray-100 dark:bg-gray-700 group">
                  <img
                    src={hasPic ? u.profile_pic! : PLACEHOLDER_PIC}
                    alt={u.profile_pic ?? ""}
                    className="w-full h-full rounded-full object-cover transition-opacity duration-300 opacity-50"
                  />
                </div>
              </td>
            </div>
            <div className="flex flex-col gap-1 items-center">
              <td className="px-6 py-4 whitespace-nowrap dark:text-white text-left">
                {u.name}
              </td>
            </div>
            <div className="flex flex-col gap-1 items-center">
              <td className="px-6 py-4 whitespace-nowrap dark:text-white text-left">
                {u.username}
              </td>
            </div>
            <div className="flex flex-col gap-1 items-end">
              <td className="px-6 py-4 whitespace-nowrap dark:text-white text-left">
                {u.email}
              </td>
            </div>
            <div className="flex flex-col gap-1 items-end">
              <td className="px-6 py-4 whitespace-nowrap dark:text-white text-left">
                {u.phone}
              </td>
            </div>
            <div className="flex flex-col gap-1 items-end">
              <td className="px-6 py-4 whitespace-nowrap text-left">
                <a
                  href={`/users/${u.id}/edit`}
                  className="mr-2 px-2 py-1 rounded text-xs bg-amber-500 hover:bg-amber-600 text-white"
                >
                  Edit
                </a>
                <form
                  method="POST"
                  action={`/users/${u.id}/delete`}
                  className="inline"
                >
                  <button
                    type="submit"
                    className="px-2 py-1 rounded text-xs bg-red-500 hover:bg-red-600 text-white"
                  >
                    Delete
                  </button>
                </form>
              </td>
            </div>
          </tr>
        );
      })
    );

  if (fragment) {
    return (
      <div id="pg-frag">
        <table data-pg-rows="users-tbody">
          <tbody id="users-tbody">{rows}</tbody>
        </table>
        <Pagination
          info={info}
          base="/users"
          container="users-tbody"
          rawSearch={rawSearch}
        />
      </div>
    );
  }

  return (
    <AdminChrome title="Users">
      <form
        method="POST"
        action="/users/create"
        encType="multipart/form-data"
        className="mb-4 flex-col gap-2 items-end bg-white dark:bg-gray-800 p-4 rounded-lg border border-gray-200 dark:border-gray-700"
      >
        <div>
          <label className="block text-sm text-gray-600 dark:text-gray-300 mb-1">
            First Name
          </label>
          <input
            name="first_name"
            type="text"
            required
            className="border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-800 text-gray-800 dark:text-white p-1.5 rounded-xs border"
          />
        </div>
        <div>
          <label className="block text-sm text-gray-600 dark:text-gray-300 mb-1">
            Last Name
          </label>
          <input
            name="last_name"
            type="text"
            required
            className="border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-800 text-gray-800 dark:text-white p-1.5 rounded-xs border"
          />
        </div>
        <div>
          <label className="block text-sm text-gray-600 dark:text-gray-300 mb-1">
            Username
          </label>
          <input
            name="username"
            type="text"
            required
            className="border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-800 text-gray-800 dark:text-white p-1.5 rounded-xs border"
          />
        </div>
        <div>
          <label className="block text-sm text-gray-600 dark:text-gray-300 mb-1">
            Email
          </label>
          <input
            name="email"
            type="text"
            required
            className="border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-800 text-gray-800 dark:text-white p-1.5 rounded-xs border"
          />
        </div>
        <div>
          <label className="block text-sm text-gray-600 dark:text-gray-300 mb-1">
            Phone
          </label>
          <input
            name="phone"
            type="text"
            required
            className="border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-800 text-gray-800 dark:text-white p-1.5 rounded-xs border"
          />
        </div>
        <div>
          <label className="block text-sm text-gray-600 dark:text-gray-300 mb-1">
            Profile Pic
          </label>
          {/* data-img-upload = js/ImageUpload.js picks this wrapper up:
              icon button + live preview + crop modal + compression. */}
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
              <label className="inline-flex items-center gap-2 px-3 py-1.5 rounded bg-indigo-600 hover:bg-indigo-700 text-white text-sm font-medium cursor-pointer">
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
                  className="hidden"
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
        </div>
        <button
          type="submit"
          className="px-4 py-2 rounded bg-indigo-600 hover:bg-indigo-700 text-white font-medium"
        >
          Add user
        </button>
      </form>

      <div className="overflow-x-auto">
        <table className="min-w-full divide-y divide-gray-200 dark:divide-gray-700">
          <thead className="bg-gray-50 dark:bg-gray-700">
            <tr>
              <th className={TH_CLASS}>#</th>
              <th className={TH_CLASS}>{tr("Profile", "Profile")}</th>
              <th className={TH_CLASS}>{tr("Name", "Name")}</th>
              <th className={TH_CLASS}>{tr("Username", "Username")}</th>
              <th className={TH_CLASS}>{tr("Email", "Email")}</th>
              <th className={TH_CLASS}>{tr("Phone", "Phone")}</th>
              <th className={TH_CLASS}>{tr("Action", "Action")}</th>
            </tr>
          </thead>
          <tbody
            id="users-tbody"
            className="divide-y divide-gray-200 dark:divide-gray-700 bg-white dark:bg-gray-800"
          >
            {rows}
          </tbody>
        </table>
      </div>
      <Pagination
        info={info}
        base="/users"
        container="users-tbody"
        rawSearch={rawSearch}
      />
    </AdminChrome>
  );
}
