// Stateless cookie cart: "id:qty,id:qty,..." — HttpOnly, no server state.
// Port of webc_pos src/shop/cart.c parse rules (every load re-validates
// against the DB; unknown/deleted products become invalid lines).
export const CART_COOKIE = "webc_cart";
export const CART_MAX = 40;
export const CART_COOKIE_MAX_AGE = 30 * 24 * 60 * 60; // Max-Age=2592000

export type CartPair = { id: string; qty: number };

function idValid(s: string): boolean {
  if (s.length === 0 || s.length > 39) return false;
  for (const c of s) {
    const ok =
      (c >= "a" && c <= "z") ||
      (c >= "A" && c <= "Z") ||
      (c >= "0" && c <= "9") ||
      c === "-" ||
      c === "_";
    if (!ok) return false;
  }
  return true;
}

function qtyParse(s: string, fallback: number): number {
  if (s.length === 0 || s.length > 6) return fallback;
  let v = 0;
  for (const c of s) {
    if (c < "0" || c > "9") return fallback;
    v = v * 10 + (c.charCodeAt(0) - 48);
    if (v > 999999) v = 999999;
  }
  return v;
}

// Parse the raw cookie into validated pairs (invalid ids/qtys dropped).
export function parseCart(raw: string | undefined | null): CartPair[] {
  const out: CartPair[] = [];
  if (!raw) return out;
  for (const pair of raw.split(",")) {
    if (out.length >= CART_MAX) break;
    if (pair.length === 0) continue;
    const colon = pair.indexOf(":");
    if (colon < 0) continue;
    const id = pair.slice(0, colon);
    if (!idValid(id)) continue;
    out.push({ id, qty: qtyParse(pair.slice(colon + 1), 0) });
  }
  return out;
}

// Header badge count: non-empty pairs up to CART_MAX (webc_pos
// cart_entry_count counts raw pairs; validation happens on load).
export function cartEntryCount(raw: string | undefined | null): number {
  if (!raw) return 0;
  let n = 0;
  for (const pair of raw.split(",")) {
    if (n >= CART_MAX) break;
    if (pair.length > 0) ++n;
  }
  return n;
}

export function serializeCart(items: CartPair[]): string {
  return items
    .filter((it) => it.qty > 0 && it.qty <= 999)
    .map((it) => `${it.id}:${it.qty}`)
    .join(",");
}
