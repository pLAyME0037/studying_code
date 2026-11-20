import { getI18n } from "@/app/lib/i18n";

// display/i18n/lang_form.h.tt — top-bar language <select>. Server
// component: the options come from the languages table; auto-submit is
// bound globally by the root layout's bootstrap (data-lang-form).
export async function LangForm({ back }: { back: string }) {
  const { langs, active } = await getI18n();
  if (langs.length < 2) return null;
  return (
    <form
      method="GET"
      action="/lang"
      data-lang-form
      className="flex items-center gap-1"
    >
      <input type="hidden" name="back" defaultValue={back} />
      <select
        name="code"
        defaultValue={active}
        aria-label="Language"
        className="border border-surface0 bg-mantle text-text px-1 py-1.5 text-xs"
      >
        {langs.map((l) => (
          <option key={l.id} value={l.code}>
            {l.name || l.code}
          </option>
        ))}
      </select>
      <noscript>
        <button
          type="submit"
          className="border border-surface0 bg-mantle text-text px-1 py-1.5 text-xs"
        >
          &#8629;
        </button>
      </noscript>
    </form>
  );
}
