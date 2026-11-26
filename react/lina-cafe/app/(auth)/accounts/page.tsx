import Link from "next/link";
import { redirect } from "next/navigation";
import { getI18n } from "@/app/lib/i18n";
import { queryAll } from "@/app/lib/db";
import { activeSession, getSessionUser } from "@/app/lib/auth";
import { switchAccount } from "@/app/lib/actions";

// GET /accounts — multi-account switcher (webc_pos serve_auth_accounts +
// display/auth/accounts.h.tt). Rows = live sibling sessions of this
// browser's switch_group, newest session first, deduped per user (a user
// who signed in twice is listed once), max 16.
const ACCT_MAX = 16;

export async function generateMetadata() {
  const { tr } = await getI18n();
  return { title: tr("auth.acct_title", "គណនី") };
}

type AcctRow = { sid: string; name: string; mail: string; type: string; isMe: boolean };

export default async function AccountsPage() {
  const { tr } = await getI18n();

  const me = await getSessionUser();
  if (!me) redirect("/login?next=/accounts");

  const active = await activeSession();
  const mySid = active ? active.sid : "";
  const group = active ? active.group : "";

  const accts: AcctRow[] = [];
  const seen = new Set<string>();
  if (group) {
    const rows = queryAll<{ sid: string; uid: string; name: string | null; mail: string | null; type: string | null }>(
      "SELECT s.id AS sid, u.id AS uid, u.name, COALESCE(u.email,'') AS mail, " +
        "u.user_type_dict_id AS type " +
        "FROM user_sessions s JOIN users u ON u.id = s.user_id " +
        "WHERE s.switch_group = ? " +
        "AND s.expires_at > strftime('%Y-%m-%dT%H:%M:%fZ','now') " +
        "AND u.deleted_at IS NULL " +
        "ORDER BY s.created_at DESC;",
      group,
    );
    for (const r of rows) {
      if (accts.length >= ACCT_MAX) break;
      if (!r.sid || !r.uid) continue;
      const uid = String(r.uid);
      if (seen.has(uid)) continue;
      seen.add(uid);
      accts.push({
        sid: String(r.sid),
        name: r.name ? String(r.name) : "",
        mail: r.mail ? String(r.mail) : "",
        type: r.type ? String(r.type) : "",
        isMe: String(r.sid) === mySid,
      });
    }
  }

  const tCurrent = tr("auth.acct_current", "ឥឡូវនេះ");
  const tSwitch = tr("auth.acct_switch", "ប្តូរ");
  const tRemove = tr("auth.acct_remove", "លុប");

  return (
    <div className="min-h-screen flex items-center justify-center bg-base px-4 sm:px-6 lg:px-8 py-6">
      <div className="w-3/5 bg-mantle border border-surface0">
        <div className="bg-text text-onbase px-3 py-2 flex items-center justify-between">
          <div>
            <div className="text-lg font-semibold">{tr("auth.acct_title", "គណនី")}</div>
            <div className="text-base text-onbase/70">{tr("auth.acct_sub", "ចូលហើយក្នុងកម្មវិធីនេះ")}</div>
          </div>
          <Link href="/" className="text-base text-onbase/80 hover:underline">Store</Link>
        </div>
        <ul className="divide-y divide-surface0">
          {accts.map((a) => {
            const cp = a.name.codePointAt(0);
            const glyph = cp !== undefined ? String.fromCodePoint(cp) : "?";
            return (
              <li key={a.sid} className="flex items-center gap-3 px-3 py-2.5">
                <span className="h-9 w-9 rounded-full bg-surface1 text-subtext0 inline-flex items-center justify-center text-base font-semibold shrink-0">
                  {glyph}
                </span>
                <div className="min-w-0 flex-1">
                  <div className="text-base text-text truncate">{a.name}</div>
                  <div className="text-base text-subtext0 truncate">
                    {a.mail}
                    {a.type && <> &#183; {a.type}</>}
                  </div>
                </div>
                {a.isMe ? (
                  <span className="text-base text-blue border border-blue/40 px-2 py-1 whitespace-nowrap">
                    {tCurrent}
                  </span>
                ) : (
                  <form action={switchAccount} className="flex items-center gap-1.5">
                    <input type="hidden" name="sid" value={a.sid} />
                    <input type="hidden" name="next" value="/accounts" />
                    <button name="op" value="switch" className="text-base bg-blue text-onbase px-2 py-1">
                      {tSwitch}
                    </button>
                    <button name="op" value="remove" className="text-base text-red border border-surface0 px-2 py-1">
                      {tRemove}
                    </button>
                  </form>
                )}
              </li>
            );
          })}
        </ul>
        <div className="p-3 flex items-center justify-between text-base">
          <Link href="/login?next=/accounts" className="text-blue hover:underline">
            {tr("auth.acct_add", "បន្ថែមគណនី")}
          </Link>
          <Link href="/logout" className="text-red hover:underline">
            {tr("shop.logout_link", "ចេញ")}
          </Link>
        </div>
      </div>
    </div>
  );
}
