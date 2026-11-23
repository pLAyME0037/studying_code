import Link from "next/link";
import { getI18n } from "@/app/lib/i18n";
import { loginAction } from "@/app/lib/actions";

// GET /login — sign-in form (webc_pos render_login + display/auth/login.h.tt).
// The error banner rides ?e=<key>&next=<path> (server re-render, no client
// component); the POST action redirects the same way.
type SearchParams = Promise<Record<string, string | string[] | undefined>>;

function first(v: string | string[] | undefined): string {
  if (Array.isArray(v)) return v[0] ?? "";
  return v ?? "";
}

const ERR_FALLBACKS: Record<string, string> = {
  err_fill: "សូមបំពេញឈ្មោះនិងពាក្យសម្ងាត់",
  err_bad: "ឈ្មោះឬពាក្យសម្ងាត់មិនត្រឹមត្រូវ",
};

export async function generateMetadata() {
  const { tr } = await getI18n();
  return { title: tr("auth.page_title", "ចូលគណនី") };
}

export default async function LoginPage({ searchParams }: { searchParams: SearchParams }) {
  const sp = await searchParams;
  const { tr } = await getI18n();

  const next = first(sp.next) || "/dashboard";
  const errKey = first(sp.e);
  const err = errKey && ERR_FALLBACKS[errKey] ? tr(`auth.${errKey}`, ERR_FALLBACKS[errKey]) : null;

  return (
    <div className="min-h-screen flex items-center justify-center bg-base px-4 sm:px-6 lg:px-8 py-6">
      <form method="POST" action={loginAction} data-login-form className="w-3/5 bg-mantle border border-surface0">
        <div className="bg-text text-onbase px-3 py-2">
          <div className="text-base font-semibold">{tr("auth.title", "កត់ឈ្មោះចូល")}</div>
          <div className="text-base text-onbase/70">POS Admin</div>
        </div>
        <div className="px-3 py-3 flex flex-col gap-3">
          {err && (
            <div data-login-error className="bg-red/10 border-l-2 border-red text-red px-2 py-2 text-base">
              {err}
            </div>
          )}
          <label className="text-base text-subtext0">
            {tr("auth.label_username", "ឈ្មោះអ្នកប្រើ")}
          </label>
          <input name="username" autoComplete="username" required className="border border-surface0 bg-base text-text px-2 py-2 text-base w-full" />
          <label className="text-base text-subtext0">
            {tr("auth.label_password", "ពាក្យសម្ងាត់")}
          </label>
          <input name="password" type="password" autoComplete="current-password" required className="border border-surface0 bg-base text-text px-2 py-2 text-base w-full" />
          <input type="hidden" name="next" value={next} />
          <button type="submit" className="bg-blue text-onbase hover:brightness-90 px-2 py-2 text-base w-full">
            {tr("auth.submit", "ចូល")}
          </button>
          <Link href="/" className="text-base text-blue hover:underline text-center">
            {tr("auth.back", "ត្រឡប់ទៅហាង")}
          </Link>
          <a href="/signup" className="text-base text-blue hover:underline text-center">
            {tr("auth.signup_link", "បង្កើតគណនីថ្មី")}
          </a>
        </div>
      </form>
    </div>
  );
}
