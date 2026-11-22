import Link from "next/link";
import { getI18n } from "@/app/lib/i18n";
import { signupAction } from "@/app/lib/actions";

// GET /signup — public account form (webc_pos render_signup +
// display/auth/signup.h.tt; the Signup_Field table in src/auth/auth.c drives
// the inputs). Errors ride ?e=<key> (server re-render, no client component).
type SearchParams = Promise<Record<string, string | string[] | undefined>>;

function first(v: string | string[] | undefined): string {
  if (Array.isArray(v)) return v[0] ?? "";
  return v ?? "";
}

const ERR_FALLBACKS: Record<string, string> = {
  su_err_fill: "សូមបំពេញព័ត៌មានទាំងអស់",
  su_err_short: "ពាក្យសម្ងាត់ត្រូវមានយ៉ាងតិច៦អក្ខរា",
  su_err_phone: "លេខទូរស័ព្ទនេះបានចុះឈ្មោះរួមហើយ",
  su_err_taken: "ឈ្មោះ អ៊ីមែល ឬលេខទូរស័ព្ទនេះបានប្រើរួមហើយ",
  su_err_sys: "មិនអាចបង្កើតគណនីបានទេ",
};

// Signup_Field table ported from src/auth/auth.c (input, tr key, Khmer
// fallback, type, autocomplete token, minlength).
const FIELDS = [
  { name: "first_name", trk: "auth.su_name", def: "ឈ្មោះ", type: "text", ac: "given-name", min: 0 },
  { name: "last_name", trk: "auth.su_last_name", def: "ត្រកូល", type: "text", ac: "family-name", min: 0 },
  { name: "username", trk: "auth.label_username", def: "ឈ្មោះអ្នកប្រើ", type: "text", ac: "username", min: 0 },
  { name: "phone", trk: "auth.su_phone", def: "លេខទូរស័ព្ទ", type: "tel", ac: "tel", min: 0 },
  { name: "email", trk: "auth.su_email", def: "អ៊ីមែល", type: "email", ac: "email", min: 0 },
  { name: "password", trk: "auth.label_password", def: "ពាក្យសម្ងាត់", type: "password", ac: "new-password", min: 6 },
] as const;

export async function generateMetadata() {
  const { tr } = await getI18n();
  return { title: tr("auth.signup_title", "បង្កើតគណនីថ្មី") };
}

export default async function SignupPage({ searchParams }: { searchParams: SearchParams }) {
  const sp = await searchParams;
  const { tr } = await getI18n();

  const errKey = first(sp.e);
  const err = errKey && ERR_FALLBACKS[errKey] ? tr(`auth.${errKey}`, ERR_FALLBACKS[errKey]) : null;

  return (
    <div className="min-h-screen flex items-center justify-center bg-base px-4 sm:px-6 lg:px-8 py-6">
      <form method="POST" action={signupAction} data-signup-form className="w-3/5 bg-mantle border border-surface0">
        <div className="bg-text text-onbase px-3 py-2">
          <div className="text-base font-semibold">{tr("auth.signup_title", "បង្កើតគណនីថ្មី")}</div>
          <div className="text-base text-onbase/70">POS Admin</div>
        </div>
        <div className="px-3 py-3 flex flex-col gap-3">
          {err && (
            <div data-signup-error className="bg-red/10 border-l-2 border-red text-red px-2 py-2 text-base">
              {err}
            </div>
          )}
          {FIELDS.map((f) => (
            <div key={f.name} className="contents">
              <label className="text-base text-subtext0">{tr(f.trk, f.def)}</label>
              <input
                name={f.name}
                type={f.type}
                autoComplete={f.ac}
                required
                minLength={f.min > 0 ? f.min : undefined}
                className="border border-surface0 bg-base text-text px-2 py-2 text-base w-full"
              />
            </div>
          ))}
          <button type="submit" className="bg-blue text-onbase hover:brightness-90 px-2 py-2 text-base w-full">
            {tr("auth.su_submit", "បង្កើតគណនី")}
          </button>
          <Link href="/login" className="text-base text-blue hover:underline text-center">
            {tr("auth.su_have", "មានគណនីហើយ? ចូល")}
          </Link>
          <Link href="/" className="text-base text-blue hover:underline text-center">
            {tr("auth.back", "ត្រឡប់ទៅហាង")}
          </Link>
        </div>
      </form>
    </div>
  );
}
