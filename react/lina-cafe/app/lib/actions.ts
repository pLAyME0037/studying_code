"use server";

// Storefront writes: port of webc_pos POST handlers (src/shop/cart.c,
// checkout.c, shop.c). Cart mutations re-write the cookie via cookies();
// checkout runs one synchronous transaction (no awaits inside — the shared
// connection must never see an interleaved request mid-txn); login-gated
// review writes redirect to /login?next=/product/<id>.
import { redirect, notFound } from "next/navigation";
import { cookies } from "next/headers";
import { randomUUID } from "node:crypto";
import { db, queryOne } from "./db";
import {
  SID_COOKIE,
  SID_COOKIE_OPTIONS,
  activeSession,
  browserGroup,
  getSessionUser,
  hashPassword,
  mintSession,
  verifyPassword,
} from "./auth";
import { safeBack } from "./i18n";
import {
  CART_MAX,
  QTY_MAX,
  idValid,
  loadCart,
  qtyParse,
  saveCart,
  type CartLine,
} from "./cart";

function fd(formData: FormData, key: string): string {
  const v = formData.get(key);
  return typeof v === "string" ? v : "";
}

// A ?redirect= carried in the POST body (hidden input), same open-redirect
// guard as webc_pos body_redirect: a single leading slash only.
function bodyRedirect(formData: FormData, fallback: string): string {
  const t = fd(formData, "redirect");
  if (t.length > 1 && t.startsWith("/") && !t.startsWith("//") && !t.startsWith("\\")) {
    return t;
  }
  return fallback;
}

// ---------------------------------------------------------------------------
// POST /cart/add, /cart/buynow, /cart/update, /cart/remove
// ---------------------------------------------------------------------------

// Shared add path (webc cart_add_common): validate id, load the posted
// product (404 when malformed/gone), clamp qty 1..999, merge or append.
async function cartAddCommon(
  formData: FormData,
  replaceWholeCart: boolean,
  fallback: string,
): Promise<void> {
  const pid = fd(formData, "product_id");
  if (!idValid(pid)) notFound();

  const row = queryOne<Record<string, unknown>>(
    "SELECT p.name, p.sku, p.base_price, p.cost_price, " +
      "COALESCE((SELECT SUM(s.quantity) FROM inventory_stocks s " +
      "          WHERE s.product_id = p.id AND s.deleted_at IS NULL), 0) AS stock " +
      "FROM products p WHERE p.id = ? AND p.deleted_at IS NULL;",
    pid,
  );
  if (!row) notFound();

  let qty = qtyParse(fd(formData, "qty"), 1);
  if (qty < 1) qty = 1;
  if (qty > QTY_MAX) qty = QTY_MAX;

  const cart = replaceWholeCart
    ? { items: [] as CartLine[] }
    : await loadCart();

  const slot = cart.items.find((it) => it.id === pid);
  if (slot) {
    slot.qty = slot.qty <= QTY_MAX - qty ? slot.qty + qty : QTY_MAX;
    slot.valid = true;
  } else if (cart.items.length < CART_MAX) {
    cart.items.push({
      id: pid,
      qty,
      name: "",
      sku: "",
      price: 0,
      cost: 0,
      stock: 0,
      valid: true,
    });
  } else {
    notFound(); // cart cookie is full (webc_pos answers 400)
  }

  await saveCart(cart.items);
  redirect(bodyRedirect(formData, fallback));
}

export async function cartAdd(formData: FormData): Promise<void> {
  await cartAddCommon(formData, false, "/cart");
}

// Buy now = the cart becomes exactly this one line, straight to checkout.
export async function cartBuyNow(formData: FormData): Promise<void> {
  await cartAddCommon(formData, true, "/checkout");
}

export async function cartUpdate(formData: FormData): Promise<void> {
  const pid = fd(formData, "product_id");
  if (!idValid(pid)) notFound();
  const qty = qtyParse(fd(formData, "qty"), 1);

  const cart = await loadCart();
  const idx = cart.items.findIndex((it) => it.id === pid);
  if (idx >= 0) {
    if (qty < 1) {
      // qty 0 removes the line
      cart.items.splice(idx, 1);
    } else {
      cart.items[idx].qty = qty > QTY_MAX ? QTY_MAX : qty;
    }
  }
  await saveCart(cart.items);
  redirect(bodyRedirect(formData, "/cart"));
}

export async function cartRemove(formData: FormData): Promise<void> {
  const pid = fd(formData, "product_id");
  if (!idValid(pid)) notFound();

  const cart = await loadCart();
  const idx = cart.items.findIndex((it) => it.id === pid);
  if (idx >= 0) cart.items.splice(idx, 1);
  await saveCart(cart.items);
  redirect(bodyRedirect(formData, "/cart"));
}

// ---------------------------------------------------------------------------
// POST /checkout - create the order (guest checkout, Phase 11 rules)
//   * every price/total comes from the DB cart, posted amounts are ignored;
//   * stock is decremented + written to stock_ledger inside one transaction
//     with order/items/delivery, all-or-nothing;
//   * order.org/staff are the deterministic first rows (schema: NOT NULL),
//     status starts PENDING, no payment row - staff complete it in /pos.
// ---------------------------------------------------------------------------

function nonEmpty(s: string): boolean {
  for (const c of s) {
    if (c !== " " && c !== "\t" && c !== "\r" && c !== "\n") return true;
  }
  return false;
}

const CHECKOUT_MAX_STOCK_ROWS = 16;

class CheckoutFail extends Error {
  key: string;
  constructor(key: string) {
    super(key);
    this.key = key;
  }
}

// One synchronous transaction: on failure everything rolls back and the
// failing translation key rides back to the checkout form.
function runCheckoutTxn(args: {
  name: string;
  phone: string;
  address: string;
  cart: { items: CartLine[]; subtotal: number };
}): { orderId: string; failKey: string | null } {
  const { name, phone, address, cart } = args;
  const orderId = randomUUID();

  db.exec("BEGIN");
  try {
    // Deterministic org/staff (orders.* are NOT NULL; seeds guarantee both).
    const org = queryOne<{ id: string }>(
      "SELECT id FROM org_units WHERE deleted_at IS NULL ORDER BY id LIMIT 1;",
    );
    const staff = queryOne<{ id: string }>(
      "SELECT id FROM staff WHERE deleted_at IS NULL ORDER BY id LIMIT 1;",
    );
    if (!org || !staff || !org.id || !staff.id) {
      throw new CheckoutFail("err_org");
    }

    // Customer: reuse by phone, else create customers (+ users) rows.
    let customerId = "";
    const found = queryOne<{ id: string; cid: string; utype: string }>(
      "SELECT id, COALESCE(customer_id, '') AS cid, user_type_dict_id AS utype " +
        "FROM users WHERE phone = ? AND deleted_at IS NULL LIMIT 1;",
      phone,
    );
    const foundUid = found && found.id ? String(found.id) : "";
    const foundCid = found && found.cid ? String(found.cid) : "";
    const foundType = found && found.utype ? String(found.utype) : "";

    if (foundCid) {
      customerId = foundCid;
    } else {
      // New customers row (defaults: TEIR_1, 0 points).
      const cid = randomUUID();
      try {
        db.prepare("INSERT INTO customers (id) VALUES (?);").run(cid);
      } catch {
        throw new CheckoutFail("err_customer");
      }
      customerId = cid;
      if (foundUid && foundType === "CUSTOMER") {
        // Existing customer-type user without a link: attach it so reports
        // resolve the name through users.customer_id.
        try {
          db.prepare("UPDATE users SET customer_id = ? WHERE id = ?;").run(
            customerId,
            foundUid,
          );
        } catch {
          throw new CheckoutFail("err_link");
        }
      } else if (!foundUid) {
        // Fresh guest: users row so /pos/users shows the buyer. username
        // derives from the unique phone (email stays NULL - 0007); the empty
        // hash keeps the account out of any login (login also requires
        // ADMIN). One "name" box -> first_name/last_name: split at the first
        // space, rest stays in last_name.
        let first = name;
        let last = "";
        const sp = name.indexOf(" ");
        if (sp >= 0) {
          first = name.slice(0, sp);
          last = name.slice(sp + 1);
        }
        const uid = randomUUID();
        try {
          db.prepare(
            "INSERT INTO users " +
              "(id, first_name, last_name, username, email, phone, " +
              " password_hash, user_type_dict_id, customer_id) " +
              "VALUES (?, ?, ?, ?, ?, ?, '', 'CUSTOMER', ?);",
          ).run(uid, first, last, phone, null, phone, customerId);
        } catch {
          throw new CheckoutFail("err_customer");
        }
      }
      // else: phone belongs to a non-CUSTOMER user (staff) - keep the order
      // customer separate, contact travels in the delivery row.
    }

    // Order row. WEB-YYMMDD is the shop's local day + first 8 hex of a
    // dashless uuid (19 chars total).
    const now = new Date();
    const yy = String(now.getFullYear() % 100).padStart(2, "0");
    const mm = String(now.getMonth() + 1).padStart(2, "0");
    const dd = String(now.getDate()).padStart(2, "0");
    const tail = randomUUID().replace(/-/g, "").slice(0, 8);
    const orderNumber = `WEB-${yy}${mm}${dd}-${tail}`;
    const sub = cart.subtotal.toFixed(2);
    try {
      db.prepare(
        "INSERT INTO orders " +
          "(id, org_unit_id, staff_id, customer_id, order_number, " +
          " order_status_dict_id, subtotal, discount_amount, " +
          " tax_amount, delivery_fee, total_amount) " +
          "VALUES (?, ?, ?, ?, ?, 'PENDING', ?, 0, 0, 0, ?);",
      ).run(orderId, org.id, staff.id, customerId, orderNumber, sub, sub);
    } catch {
      throw new CheckoutFail("err_save_order");
    }

    // Lines + stock movement.
    const stockRowsSql =
      "SELECT id, quantity FROM inventory_stocks " +
      "WHERE product_id = ? AND deleted_at IS NULL " +
      "ORDER BY CASE WHEN org_unit_id = ? THEN 0 ELSE 1 END, id;";
    for (const it of cart.items) {
      if (!it.valid) continue;

      try {
        db.prepare(
          "INSERT INTO order_items " +
            "(order_id, product_id, variant_id, unit_price, unit_cost, " +
            " quantity, discount_amount, tax_amount, total_line) " +
            "VALUES (?, ?, NULL, ?, ?, ?, 0, 0, ?);",
        ).run(
          orderId,
          it.id,
          it.price.toFixed(2),
          it.cost.toFixed(2),
          it.qty.toFixed(2),
          (it.price * it.qty).toFixed(2),
        );
      } catch {
        throw new CheckoutFail("err_save_line");
      }

      // Stock: prefer this org's rows, walk them greedily (bounded 16).
      let rows: { id: string; qty: number }[];
      try {
        rows = queryAllStock(stockRowsSql, it.id, org.id).slice(
          0,
          CHECKOUT_MAX_STOCK_ROWS,
        );
      } catch {
        throw new CheckoutFail("err_read_stock");
      }
      let available = 0;
      for (const r of rows) available += r.qty;
      if (available + 1e-9 < it.qty) {
        throw new CheckoutFail("err_low_stock");
      }

      let need = it.qty;
      for (const r of rows) {
        if (need <= 1e-9) break;
        const take = Math.min(r.qty, need);
        if (take <= 0) continue;
        const balance = r.qty - take;
        try {
          db.prepare(
            "UPDATE inventory_stocks SET quantity = quantity - ? WHERE id = ?;",
          ).run(take.toFixed(4), r.id);
        } catch {
          throw new CheckoutFail("err_dec_stock");
        }
        try {
          db.prepare(
            "INSERT INTO stock_ledger " +
              "(stock_id, reference_type, reference_id, quantity_change, " +
              " balance_after, note) VALUES (?, 'ORDER', ?, ?, ?, ?);",
          ).run(
            r.id,
            orderId,
            `-${take.toFixed(4)}`,
            balance.toFixed(4),
            `កម្មង់ ${orderNumber}`,
          );
        } catch {
          throw new CheckoutFail("err_ledger");
        }
        need -= take;
      }
    }

    // Delivery row carries the guest contact for staff fulfilment.
    try {
      db.prepare(
        "INSERT INTO deliveries " +
          "(order_id, recipient_name, recipient_phone, delivery_address) " +
          "VALUES (?, ?, ?, ?);",
      ).run(orderId, name, phone, address);
    } catch {
      throw new CheckoutFail("err_delivery");
    }
    // `note` travels in the form only — webc_pos stores no column for it.

    db.exec("COMMIT");
    return { orderId, failKey: null };
  } catch (e) {
    db.exec("ROLLBACK");
    if (e instanceof CheckoutFail) return { orderId: "", failKey: e.key };
    return { orderId: "", failKey: "err_save_order" };
  }
}

function queryAllStock(
  sql: string,
  productId: string,
  orgId: string,
): { id: string; qty: number }[] {
  const rows = db.prepare(sql).all(productId, orgId);
  return rows.map((r) => ({
    id: String(r.id),
    qty: Number(r.quantity ?? 0),
  }));
}

export async function checkout(formData: FormData): Promise<void> {
  const name = fd(formData, "name");
  const phone = fd(formData, "phone");
  const address = fd(formData, "address");
  const note = fd(formData, "note");

  const cart = await loadCart();
  if (cart.live === 0) redirect("/cart");

  // Validation order matches webc_pos: name, phone (>= 6 chars), address.
  // Failures come back as ?e=<key> with the fields preserved (server
  // re-render without a client component).
  const fail = (key: string): never => {
    const p = new URLSearchParams({ e: key, name, phone, address, note });
    redirect(`/checkout?${p.toString()}`);
  };
  if (!nonEmpty(name)) fail("err_name");
  if (!nonEmpty(phone) || phone.length < 6) fail("err_phone");
  if (!nonEmpty(address)) fail("err_address");

  const { orderId, failKey } = runCheckoutTxn({
    name,
    phone,
    address,
    cart,
  });
  if (failKey) fail(failKey);

  // Empty the cart, then celebrate.
  await saveCart([]);
  redirect(`/order/${orderId}`);
}

// ---------------------------------------------------------------------------
// POST /product/<id>/review, /review/<id>/like — signed-in writes
// ---------------------------------------------------------------------------

const SHOP_REVIEW_MAX = 1000;

export async function postReview(formData: FormData): Promise<void> {
  const pid = fd(formData, "product_id");
  if (!idValid(pid)) notFound();
  const product = queryOne<{ one: number }>(
    "SELECT 1 AS one FROM products WHERE id = ? AND deleted_at IS NULL;",
    pid,
  );
  if (!product) notFound();

  // Login gate: the product resolved first so ?next= is a real, safe path.
  const viewer = await getSessionUser();
  if (!viewer) redirect(`/login?next=/product/${pid}`);

  const body = fd(formData, "body");
  if (body.length === 0 || body.length > SHOP_REVIEW_MAX) notFound();
  if (!/\S/.test(body)) notFound(); // at least one non-space

  // Resolve the parent: live review of THIS product; a reply to a reply is
  // flattened to its root (two-level display).
  let parentId = "";
  const parent = fd(formData, "parent_id");
  if (parent) {
    const row = queryOne<{ parent_id: string | null }>(
      "SELECT parent_id FROM product_reviews " +
        "WHERE id = ? AND product_id = ? AND deleted_at IS NULL;",
      parent,
      pid,
    );
    if (!row) notFound();
    parentId = row.parent_id ? String(row.parent_id) : parent;
  }

  // Rating: roots only, exactly one digit 1..5 when present.
  const ratingRaw = fd(formData, "rating");
  let rating: number | null = null;
  if (ratingRaw) {
    if (parentId || ratingRaw.length !== 1 || ratingRaw < "1" || ratingRaw > "5") {
      notFound();
    }
    rating = Number(ratingRaw);
  }

  db.prepare(
    "INSERT INTO product_reviews " +
      "(product_id, user_id, parent_id, rating, body) " +
      "VALUES (?, ?, ?, ?, ?);",
  ).run(pid, viewer.id, parentId || null, rating, body);
  redirect(`/product/${pid}`);
}

// POST /review/<id>/like — toggle the viewer's like (INSERT-or-DELETE; the
// unique pair keeps double clicks idempotent).
export async function toggleReviewLike(formData: FormData): Promise<void> {
  const rid = fd(formData, "review_id");
  if (!idValid(rid)) notFound();
  const row = queryOne<{ product_id: string }>(
    "SELECT product_id FROM product_reviews " +
      "WHERE id = ? AND deleted_at IS NULL;",
    rid,
  );
  if (!row || !row.product_id) notFound();
  const pid = String(row.product_id);

  const viewer = await getSessionUser();
  if (!viewer) redirect(`/login?next=/product/${pid}`);

  const liked = queryOne<{ one: number }>(
    "SELECT 1 AS one FROM review_likes WHERE review_id = ? AND user_id = ?;",
    rid,
    viewer.id,
  );
  if (liked) {
    db.prepare(
      "DELETE FROM review_likes WHERE review_id = ? AND user_id = ?;",
    ).run(rid, viewer.id);
  } else {
    db.prepare(
      "INSERT OR IGNORE INTO review_likes (review_id, user_id) VALUES (?, ?);",
    ).run(rid, viewer.id);
  }
  redirect(`/product/${pid}`);
}

// ---------------------------------------------------------------------------
// POST /login - any ACTIVE account with a password may sign in (Phase 16;
// the landing target is decided after the verify: non-ADMIN = storefront).
// Errors come back as ?e=<key>&next=<path> (server re-render, no client
// component), matching render_login's banner + next round-trip.
// ---------------------------------------------------------------------------

export async function loginAction(formData: FormData): Promise<void> {
  const username = fd(formData, "username");
  const password = fd(formData, "password");
  const next = safeBack(fd(formData, "next"), "/dashboard");

  const err: (key: string) => never = (key) => {
    redirect(`/login?e=${key}&next=${encodeURIComponent(next)}`);
  };
  if (username.length === 0 || password.length === 0) err("err_fill");

  const u = queryOne<{
    id: string;
    password_hash: string | null;
    utype: string | null;
  }>(
    "SELECT id, password_hash, user_type_dict_id AS utype FROM users " +
      "WHERE (username = ? OR email = ?) " +
      "AND status = 1 AND deleted_at IS NULL LIMIT 1;",
    username,
    username,
  );
  const ok =
    !!u &&
    !!u.id &&
    !!u.password_hash &&
    verifyPassword(password, u.password_hash);
  if (!ok || !u) err("err_bad");

  // ADMIN follows the (validated) staff next= target; CUSTOMER sessions
  // belong on the storefront, so a crafted ?next=/dashboard bounces.
  let target = next;
  if (u.utype && u.utype !== "ADMIN") target = "/";

  const sid = mintSession(u.id, await browserGroup());
  const jar = await cookies();
  jar.set(SID_COOKIE, sid, SID_COOKIE_OPTIONS);
  redirect(target);
}

// ---------------------------------------------------------------------------
// POST /signup - public permanent accounts (Phase 16). The phone decides
// insert-vs-claim: an existing row with an empty password_hash is the guest
// checkout account (0007) and gets claimed; a hashed row means the phone is
// already registered. The minted session is the one login produces.
// ---------------------------------------------------------------------------

export async function signupAction(formData: FormData): Promise<void> {
  const first = fd(formData, "first_name");
  const last = fd(formData, "last_name");
  const username = fd(formData, "username");
  const phone = fd(formData, "phone");
  const email = fd(formData, "email");
  const password = fd(formData, "password");

  let err: string | null = null;
  let uid = "";
  let claim = false;

  if (
    first.length === 0 ||
    last.length === 0 ||
    username.length === 0 ||
    phone.length === 0 ||
    email.length === 0 ||
    password.length === 0
  ) {
    err = "su_err_fill";
  } else if (password.length < 6) {
    err = "su_err_short";
  }

  // The phone decides insert-vs-claim-vs-reject.
  if (!err) {
    const byPhone = queryOne<{ id: string; h: string | null }>(
      "SELECT id, COALESCE(password_hash, '') AS h FROM users " +
        "WHERE phone = ? LIMIT 1;",
      phone,
    );
    if (byPhone && byPhone.h) {
      err = "su_err_phone";
    } else if (byPhone && byPhone.id) {
      uid = String(byPhone.id);
      claim = true;
    }
  }

  // Username and email must be free (the claimed row may keep its own).
  if (!err) {
    const takenUser = queryOne<{ id: string }>(
      "SELECT id FROM users WHERE username = ? LIMIT 1;",
      username,
    );
    if (takenUser && (!claim || String(takenUser.id) !== uid)) {
      err = "su_err_taken";
    }
    const takenMail = queryOne<{ id: string }>(
      "SELECT id FROM users WHERE email = ? LIMIT 1;",
      email,
    );
    if (!err && takenMail && (!claim || String(takenMail.id) !== uid)) {
      err = "su_err_taken";
    }
  }

  // Per-account salt: "<salt>$<hex>" exactly like 0005_auth — verifyPassword
  // reads the salt back out of the stored string (16 uuid chars).
  let stored = "";
  if (!err) stored = hashPassword(password, randomUUID().slice(0, 16));

  // Insert the fresh CUSTOMER row (DDL default) or claim the guest one by
  // filling the credentials - guest rows keep their customer link.
  if (!err) {
    try {
      if (claim) {
        db.prepare(
          "UPDATE users SET first_name = ?, last_name = ?, username = ?, " +
            "email = ?, password_hash = ? WHERE id = ?;",
        ).run(first, last, username, email, stored, uid);
      } else {
        uid = randomUUID();
        db.prepare(
          "INSERT INTO users " +
            "(id, first_name, last_name, username, email, phone, " +
            "password_hash) VALUES (?, ?, ?, ?, ?, ?, ?);",
        ).run(uid, first, last, username, email, phone, stored);
      }
    } catch {
      // A race lost against the UNIQUE indexes reads as "already taken".
      err = "su_err_taken";
    }
  }

  if (err) redirect(`/signup?e=${err}`);

  const sid = mintSession(uid, await browserGroup());
  const jar = await cookies();
  jar.set(SID_COOKIE, sid, SID_COOKIE_OPTIONS);
  redirect("/");
}

// ---------------------------------------------------------------------------
// POST /switch - multi-account hop (repoint webc_sid at a sibling session of
// this browser's group) or drop one (never the active session).
// ---------------------------------------------------------------------------

export async function switchAccount(formData: FormData): Promise<void> {
  const me = await getSessionUser();
  if (!me) redirect("/login?next=/accounts");

  const op = fd(formData, "op");
  const targ = fd(formData, "sid");
  const next = safeBack(fd(formData, "next"), "/accounts");
  if (targ.length === 0 || targ.length > 64) redirect("/accounts");

  const active = await activeSession();
  if (!active || !active.group) redirect("/accounts"); // nothing to switch

  if (op === "remove") {
    // Drop a sibling session from MY group only (never the active one).
    db.prepare(
      "DELETE FROM user_sessions " +
        "WHERE id = ? AND switch_group = ? AND id <> ?;",
    ).run(targ, active.group, active.sid);
    redirect("/accounts");
  }

  // Switch: target must be a live session in MY group owned by a live user
  // - this blocks hopping to an arbitrary/stolen sid.
  const ok = queryOne<{ one: number }>(
    "SELECT 1 AS one FROM user_sessions s " +
      "JOIN users u ON u.id = s.user_id " +
      "WHERE s.id = ? AND s.switch_group = ? " +
      "AND s.expires_at > strftime('%Y-%m-%dT%H:%M:%fZ', 'now') " +
      "AND u.deleted_at IS NULL LIMIT 1;",
    targ,
    active.group,
  );
  if (!ok) redirect("/accounts");

  const jar = await cookies();
  jar.set(SID_COOKIE, targ, SID_COOKIE_OPTIONS);
  redirect(next);
}
