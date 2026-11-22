// Session auth: port of webc_pos src/auth/auth.c — cookie webc_sid ->
// user_sessions row -> users profile + role permissions (",CODE," joined
// for needle matching). Passwords are "<salt>$<hex>", hex = sha256(salt||pw).
import { cache } from "react";
import { cookies } from "next/headers";
import { createHash, randomUUID } from "node:crypto";
import { db, queryAll, queryOne } from "./db";

export const SID_COOKIE = "webc_sid";
export const SESSION_DAYS = 7;
export const SESSION_MAX_AGE = SESSION_DAYS * 24 * 60 * 60;
const HASH_SALT_MAX = 64;

export type AuthUser = {
  id: string;
  name: string;
  email: string;
  profile_pic: string;
  user_type: string; // ADMIN / CUSTOMER / ...
  status: string; // ACTIVE / INACTIVE / SUSPENDED
  role: string; // role display name
  role_code: string;
  staff_id: string;
  org_unit_id: string;
  perms: string; // ",CODE,CODE," joined for needle matching
};

// One join loads the profile + first live role + linked staff row (webc_pos
// auth_resolve). LEFT JOIN keeps role-less users visible; a soft-deleted
// account resolves to null (sidebar shows the generic workspace identity).
// webc reads this row positionally; node:sqlite labels unaliased expressions
// with the whole expression text, so every computed column needs an alias.
const USER_SQL =
  "SELECT u.id, u.name, u.email, u.profile_pic, " +
  "u.user_type_dict_id, " +
  "CASE u.status WHEN 1 THEN 'ACTIVE' WHEN 0 THEN 'INACTIVE' ELSE 'SUSPENDED' END AS status, " +
  "COALESCE(r.role_name, '') AS role_name, COALESCE(r.role_code, '') AS role_code, " +
  "COALESCE(ur.role_id, '') AS role_id, COALESCE(s.id, '') AS staff_id, " +
  "COALESCE(s.org_unit_id, u.org_unit_id, '') AS org_unit_id " +
  "FROM users u " +
  "LEFT JOIN user_roles ur ON ur.user_id = u.id AND ur.deleted_at IS NULL " +
  "LEFT JOIN roles r ON r.id = ur.role_id AND r.deleted_at IS NULL " +
  "LEFT JOIN staff s ON s.user_id = u.id AND s.deleted_at IS NULL " +
  "WHERE u.id = ? AND u.deleted_at IS NULL LIMIT 1;";

const PERMS_SQL =
  "SELECT p.perm_code FROM role_permissions rp " +
  "JOIN permissions p ON p.id = rp.permission_id " +
  "WHERE rp.role_id = ? AND rp.deleted_at IS NULL AND p.deleted_at IS NULL;";

// Signed-in user for this request, or null. React cache() dedupes across
// the layout + page tree within one render pass (webc_pos's g_auth_* state).
export const getSessionUser = cache(async (): Promise<AuthUser | null> => {
  const jar = await cookies();
  const sid = jar.get(SID_COOKIE)?.value;
  if (!sid || sid.length > 64) return null;

  const session = queryOne<{ user_id: string }>(
    "SELECT user_id FROM user_sessions " +
      "WHERE id = ? " +
      "AND expires_at > strftime('%Y-%m-%dT%H:%M:%fZ', 'now') " +
      "LIMIT 1;",
    sid,
  );
  if (!session?.user_id) return null;

  const u = queryOne<Record<string, string | null>>(
    USER_SQL,
    session.user_id,
  );
  if (!u) return null;

  // Phase 14: the role's permission codes joined ",CODE," (auth_has_perm
  // needle-matches ",CODE," without splitting).
  let perms = ",";
  const roleId = u.role_id ? String(u.role_id) : "";
  if (roleId) {
    const codes = queryAll<{ perm_code: string }>(PERMS_SQL, roleId);
    perms = "," + codes.map((r) => r.perm_code).filter(Boolean).join(",") + ",";
    if (perms === ",,") perms = ",";
  }

  return {
    id: String(u.id ?? ""),
    name: u.name ? String(u.name) : "",
    email: u.email ? String(u.email) : "",
    profile_pic: u.profile_pic ? String(u.profile_pic) : "",
    user_type: u.user_type_dict_id ? String(u.user_type_dict_id) : "",
    status: u.status ? String(u.status) : "",
    role: u.role_name ? String(u.role_name) : "",
    role_code: u.role_code ? String(u.role_code) : "",
    staff_id: u.staff_id ? String(u.staff_id) : "",
    org_unit_id: u.org_unit_id ? String(u.org_unit_id) : "",
    perms,
  };
});

// Needle-match ",CODE," in the joined perms string (webc_pos auth_has_perm).
export function hasPerm(user: AuthUser | null, code: string): boolean {
  if (!code || !user || !user.perms || user.perms === ",") return false;
  return user.perms.includes(`,${code},`);
}

// ---------------------------------------------------------------------------
// Passwords: "<salt>$<hex>", hex = sha256(salt || password)
// ---------------------------------------------------------------------------

export function hashHex(pw: string, salt: string): string {
  return createHash("sha256").update(salt).update(pw).digest("hex");
}

export function hashPassword(pw: string, salt: string): string {
  return `${salt}$${hashHex(pw, salt)}`;
}

export function verifyPassword(pw: string, stored: string): boolean {
  const sep = stored.indexOf("$");
  if (sep <= 0) return false;
  const salt = stored.slice(0, sep);
  if (salt.length >= HASH_SALT_MAX) return false;
  return hashHex(pw, salt) === stored.slice(sep + 1);
}

// ---------------------------------------------------------------------------
// Session mint / browser group (login + signup flows)
// ---------------------------------------------------------------------------

// Housekeeping first (drop expired rows, best effort), then insert the new
// session; the caller sets the webc_sid cookie with the returned id.
export function mintSession(userId: string, group: string | null): string {
  db.prepare(
    "DELETE FROM user_sessions " +
      "WHERE expires_at <= strftime('%Y-%m-%dT%H:%M:%fZ', 'now');",
  ).run();
  const sid = randomUUID();
  // Join the caller's browser group when given; otherwise mint a fresh one
  // (a first login on a new browser starts a group by itself).
  const grp = group && group.length ? group : randomUUID();
  db.prepare(
    "INSERT INTO user_sessions (id, user_id, switch_group, expires_at) " +
      "VALUES (?, ?, ?, strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '+7 days'));",
  ).run(sid, userId, grp);
  return sid;
}

// The active session row on this browser (id + account group), or null when
// the cookie is absent/expired. webc_pos active_session(); powers logout,
// /accounts and /switch.
export async function activeSession(): Promise<{
  sid: string;
  group: string;
} | null> {
  const jar = await cookies();
  const sid = jar.get(SID_COOKIE)?.value;
  if (!sid || sid.length > 64) return null;
  const row = queryOne<{ id: string; switch_group: string | null }>(
    "SELECT id, COALESCE(switch_group, '') AS switch_group " +
      "FROM user_sessions WHERE id = ? " +
      "AND expires_at > strftime('%Y-%m-%dT%H:%M:%fZ', 'now') LIMIT 1;",
    sid,
  );
  if (!row?.id) return null;
  return {
    sid: String(row.id),
    group: row.switch_group ? String(row.switch_group) : "",
  };
}

// The browser's account group: reuse the switch_group of whatever session is
// already active on this browser (second login joins it); a session minted
// before 0010 has none and is stamped here; a fresh browser starts a group.
export async function browserGroup(): Promise<string> {
  const active = await activeSession();
  if (!active) return randomUUID();
  if (active.group) return active.group;
  // Active session predates the group column: stamp it now so this login
  // links to it instead of orphaning it.
  const grp = randomUUID();
  db.prepare("UPDATE user_sessions SET switch_group = ? WHERE id = ?;").run(
    grp,
    active.sid,
  );
  return grp;
}

// Cookie options matching webc_pos http_set_cookie for webc_sid:
// Path=/, HttpOnly, SameSite=Lax, Max-Age=7 days.
export const SID_COOKIE_OPTIONS = {
  httpOnly: true,
  sameSite: "lax" as const,
  path: "/",
  maxAge: SESSION_MAX_AGE,
};
