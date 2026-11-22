// Stateless cookie cart: "id:qty,id:qty,..." — HttpOnly, no server state.
// Port of webc_pos src/shop/cart.c: every load re-validates against the DB
// (unknown/deleted products become invalid lines and are dropped on the
// next save), duplicates merge instead of trusting the sender, qty caps 999.
import { cookies } from "next/headers";
import { queryAll } from "./db";

export const CART_COOKIE = "webc_cart";
export const CART_MAX = 40;
export const CART_COOKIE_MAX_AGE = 30 * 24 * 60 * 60; // Max-Age=2592000
export const QTY_MAX = 999;

export type CartPair = { id: string; qty: number };

export type CartLine = {
  id: string;
  qty: number;
  name: string;
  sku: string;
  price: number;
  cost: number;
  stock: number;
  valid: boolean;
};

export type LoadedCart = { items: CartLine[]; subtotal: number; live: number };

export function idValid(s: string): boolean {
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

export function qtyParse(s: string, fallback: number): number {
  if (s.length === 0 || s.length > 6) return fallback;
  let v = 0;
  for (const c of s) {
    if (c < "0" || c > "9") return fallback;
    v = v * 10 + (c.charCodeAt(0) - 48);
    if (v > 999999) v = 999999;
  }
  return v;
}

// Raw cookie -> validated pairs: invalid ids dropped, qty<1 dropped,
// duplicates merged (cap 999), first CART_MAX pairs kept.
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
    const qty = qtyParse(pair.slice(colon + 1), 0);
    if (qty < 1) continue;
    const existing = out.find((o) => o.id === id);
    if (existing) {
      existing.qty =
        existing.qty <= QTY_MAX - qty ? existing.qty + qty : QTY_MAX;
      continue;
    }
    out.push({ id, qty: qty > QTY_MAX ? QTY_MAX : qty });
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

export function serializeCart(items: { id: string; qty: number }[]): string {
  return items
    .filter((it) => it.qty > 0 && it.qty <= QTY_MAX)
    .map((it) => `${it.id}:${it.qty}`)
    .join(",");
}

// Cookie pairs -> enriched lines (one IN query instead of webc_pos's N
// per-id lookups; same result, fewer round trips). Invalid lines stay in
// the list (excluded from display/save/subtotal), exactly like the C code.
type ProductRow = {
  id: string;
  name: string;
  sku: string | null;
  base_price: unknown;
  cost_price: unknown;
  stock: unknown;
};

export async function loadCart(): Promise<LoadedCart> {
  const jar = await cookies();
  const pairs = parseCart(jar.get(CART_COOKIE)?.value);

  const byId = new Map<string, ProductRow>();
  if (pairs.length > 0) {
    const ph = pairs.map(() => "?").join(",");
    const rows = queryAll<ProductRow>(
      "SELECT p.id, p.name, p.sku, p.base_price, p.cost_price, " +
        "COALESCE((SELECT SUM(s.quantity) FROM inventory_stocks s " +
        "          WHERE s.product_id = p.id AND s.deleted_at IS NULL), 0) AS stock " +
        "FROM products p " +
        `WHERE p.deleted_at IS NULL AND p.id IN (${ph});`,
      ...pairs.map((p) => p.id),
    );
    for (const r of rows) byId.set(String(r.id), r);
  }

  const items: CartLine[] = [];
  let subtotal = 0;
  let live = 0;
  for (const pair of pairs) {
    const r = byId.get(pair.id);
    const line: CartLine = {
      id: pair.id,
      qty: pair.qty,
      name: "",
      sku: "",
      price: 0,
      cost: 0,
      stock: 0,
      valid: false,
    };
    if (r) {
      line.name = r.name ? String(r.name) : "";
      line.sku = r.sku ? String(r.sku) : "";
      line.price = Number(r.base_price ?? 0);
      line.cost = Number(r.cost_price ?? 0);
      line.stock = Number(r.stock ?? 0);
      line.valid = true;
      subtotal += line.price * line.qty;
      live += 1;
    }
    items.push(line);
  }
  return { items, subtotal, live };
}

// Write the cart cookie (server-action context only). Only valid lines are
// persisted; an empty result clears the cookie (webc_pos shop_cart_save).
export async function saveCart(items: CartLine[]): Promise<void> {
  const raw = serializeCart(items.filter((it) => it.valid));
  const jar = await cookies();
  if (!raw) {
    jar.set(CART_COOKIE, "", { path: "/", httpOnly: true, maxAge: 0 });
    return;
  }
  jar.set(CART_COOKIE, raw, {
    path: "/",
    httpOnly: true,
    sameSite: "lax",
    maxAge: CART_COOKIE_MAX_AGE,
  });
}
