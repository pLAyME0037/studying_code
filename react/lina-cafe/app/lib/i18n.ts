// i18n: port of webc_pos src/i18n/i18n.c — the tr() chain is
// active-language rows -> default-language rows -> C literal fallback,
// resolved once per request (React cache() = webc_pos's per-request state).
import { cache } from "react";
import { cookies } from "next/headers";
import { queryAll } from "./db";

export const LANG_COOKIE = "webc_lang";
export const LANG_COOKIE_MAX_AGE = 365 * 24 * 60 * 60; // one year, Path=/

export type Lang = { id: string; code: string; name: string };

export type I18n = {
  active: string; // chosen language code ("" = no seeded languages)
  def: string; // default language (fallback level)
  langs: Lang[]; // active languages, default first
  htmlLang: string; // <html lang> value (always "km" when unresolved)
  tr: (key: string, fallback: string) => string;
};

// "km"/"en"-shaped cookie value: lowercase letters, digits, dashes only.
export function langCodeOk(c: string): boolean {
  if (c.length < 2 || c.length > 15) return false;
  for (const ch of c) {
    const ok =
      (ch >= "a" && ch <= "z") || (ch >= "0" && ch <= "9") || ch === "-";
    if (!ok) return false;
  }
  return true;
}

// One language's rows. Empty values are skipped (webc_pos: an empty value
// falls through to the default language, then to the literal).
function loadRows(langId: string): Map<string, string> {
  const rows = queryAll<{ trans_key: string; trans_value: string | null }>(
    "SELECT trans_key, trans_value FROM translations " +
      "WHERE language_id = ? AND deleted_at IS NULL " +
      "ORDER BY trans_key LIMIT 512;",
    langId,
  );
  const map = new Map<string, string>();
  for (const r of rows) {
    if (r.trans_key && r.trans_value) map.set(r.trans_key, r.trans_value);
  }
  return map;
}

// One-shot resolve per request: cookie + language list + both row sets.
// A failed resolve leaves everything empty so tr() returns its literal.
export const getI18n = cache(async (): Promise<I18n> => {
  const jar = await cookies();
  const cand = jar.get(LANG_COOKIE)?.value ?? "";

  const langs = queryAll<Lang>(
    "SELECT id, code, name FROM languages " +
      "WHERE is_active = 1 AND deleted_at IS NULL " +
      "ORDER BY is_default DESC, code LIMIT 50;",
  );
  const def = langs[0]?.code ?? "";

  // Unknown/absent cookie falls back to the default language.
  let active = def;
  if (langCodeOk(cand) && langs.some((l) => l.code === cand)) active = cand;

  let rows = new Map<string, string>();
  let drows = new Map<string, string>();
  if (langs.length > 0) {
    const activeLang = langs.find((l) => l.code === active);
    if (activeLang) rows = loadRows(activeLang.id);
    if (active !== def) drows = loadRows(langs[0].id);
  }

  const tr = (key: string, fallback: string): string => {
    if (!key) return fallback;
    return rows.get(key) ?? drows.get(key) ?? fallback;
  };

  return { active, def, langs, htmlLang: active || "km", tr };
});

// Same-site return-path guard as webc_pos serve_lang_set(): only a single
// leading slash counts ("/x" ok, "//x", "\\x", absolute URLs rejected).
export function safeBack(back: string | null | undefined, fallback: string): string {
  if (back && back.length > 1 && back.startsWith("/") && !back.startsWith("//")) {
    return back;
  }
  return fallback;
}
