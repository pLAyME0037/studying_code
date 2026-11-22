// Storefront helpers + the /product/<id> data bundle.
// Port of webc_pos src/shop/shop.c: shop_name() (0011 seed + C fallback),
// shop_money(), first-glyph tile placeholder, list query builder, and
// serve_shop_product's bounded queries (scalar subqueries, LIMITs everywhere).
import { cache } from "react";
import { queryAll, queryOne } from "./db";

export function shopName(): string {
  const row = queryOne<{ config_value: string | null }>(
    "SELECT config_value FROM system_configs " +
      "WHERE config_key = 'pos.shop_name' AND deleted_at IS NULL;",
  );
  const v = row?.config_value;
  return v ? v : "ហាង POS";
}

// shop_money(v) = "%.2f $"
export function money(v: number): string {
  return `${v.toFixed(2)} $`;
}

// First UTF-8 glyph of a name, for placeholder product tiles.
export function firstGlyph(name: string): string {
  if (!name) return "";
  const cp = name.codePointAt(0);
  return cp === undefined ? "" : String.fromCodePoint(cp);
}

// "?cat=&q=&page=" preserving the other active filters (raw values are
// html-escaped by React's attribute context).
export function listQuery(cat: string, q: string, page: number): string {
  return `/?cat=${cat}&q=${q}&page=${page}`;
}

// Order status label: tr("status.<code>") with the raw dictionary id as the
// last fallback (unknown codes pass through untranslated).
export function statusLabel(
  tr: (k: string, f: string) => string,
  code: string,
): string {
  if (!code) return "";
  const t = tr(`status.${code}`, "");
  return t ? t : code;
}

// ---------------------------------------------------------------------------
// /product/<id> bundle — one round trip per section, all bounded (LIMIT /
// indexed WHERE). cache() dedupes generateMetadata + page within a request.
// ---------------------------------------------------------------------------

export type ShopPicture = { kind: string; name: string; image: string };

export type ShopReview = {
  id: string;
  parent: string;
  user: string;
  when: string; // YYYY-MM-DD slice of created_at
  body: string;
  rating: number; // 0 = no rating (always 0 on replies)
  likes: number;
  liked: boolean;
};

export type ShopCard = {
  id: string;
  name: string;
  price: number;
  image: string;
  sold: number; // recommendation cards only
};

export type ProductBundle = {
  id: string;
  name: string;
  sku: string;
  cat: string;
  catId: string;
  price: number;
  stock: number;
  avgRating: number;
  ratingCount: number;
  commentCount: number;
  sold: number;
  starCount: number[]; // index 1..5, 0 unused
  pics: ShopPicture[];
  roots: ShopReview[];
  replies: ShopReview[];
  related: ShopCard[];
  recommend: ShopCard[];
};

const PICS_MAX = 24;
const ROOTS_MAX = 6;
const REPLIES_MAX = 100;
const CARDS_MAX = 8;

type SqlNum = number | string | null | undefined;
const num = (v: SqlNum): number => (v === null || v === undefined ? 0 : Number(v));

export const getProduct = cache(
  (id: string, viewerId: string): ProductBundle | null => {
    // Product row + rating summary + comment count (scalar subqueries keep
    // it one round trip over the product's own index).
    const p = queryOne<Record<string, SqlNum>>(
      "SELECT p.name, p.sku, p.base_price, c.name AS cat_name, " +
        "COALESCE((SELECT SUM(s.quantity) FROM inventory_stocks s " +
        "          WHERE s.product_id = p.id AND s.deleted_at IS NULL), 0) AS stock, " +
        "p.category_id, " +
        "(SELECT ROUND(AVG(r.rating), 1) FROM product_reviews r " +
        "  WHERE r.product_id = p.id AND r.deleted_at IS NULL " +
        "    AND r.rating IS NOT NULL) AS avg_rating, " +
        "(SELECT COUNT(*) FROM product_reviews r " +
        "  WHERE r.product_id = p.id AND r.deleted_at IS NULL " +
        "    AND r.rating IS NOT NULL) AS rating_count, " +
        "(SELECT COUNT(*) FROM product_reviews r " +
        "  WHERE r.product_id = p.id AND r.deleted_at IS NULL) AS comment_count, " +
        "COALESCE((SELECT SUM(oi.quantity) FROM order_items oi " +
        "  WHERE oi.product_id = p.id AND oi.deleted_at IS NULL), 0) AS sold " +
        "FROM products p JOIN categories c ON c.id = p.category_id " +
        "WHERE p.id = ? AND p.deleted_at IS NULL;",
      id,
    );
    if (!p) return null;

    // Star distribution (at most 5 rows), index 1..5.
    const starCount = [0, 0, 0, 0, 0, 0];
    const stars = queryAll<{ rating: SqlNum; c: SqlNum }>(
      "SELECT rating, COUNT(*) AS c FROM product_reviews " +
        "WHERE product_id = ? AND deleted_at IS NULL " +
        "  AND rating IS NOT NULL " +
        "GROUP BY rating;",
      id,
    );
    for (const s of stars) {
      const bucket = Math.trunc(num(s.rating));
      if (bucket >= 1 && bucket <= 5) starCount[bucket] = Math.trunc(num(s.c));
    }

    // Section 1: pictures in display order.
    const pics = queryAll<{ kind: SqlNum; name: SqlNum; image: SqlNum }>(
      "SELECT kind, name, image FROM product_images " +
        "WHERE product_id = ? AND deleted_at IS NULL " +
        "ORDER BY sort_order, created_at LIMIT 24;",
      id,
    ).map((r) => ({
      kind: r.kind ? String(r.kind) : "",
      name: r.name ? String(r.name) : "",
      image: r.image ? String(r.image) : "",
    }));

    // Section 3a: root reviews (newest first) with like counts.
    const roots = queryAll<Record<string, SqlNum>>(
      "SELECT r.id, u.name AS user_name, r.created_at, r.body, r.rating, " +
        "(SELECT COUNT(*) FROM review_likes l " +
        "  WHERE l.review_id = r.id) AS likes, " +
        "(SELECT COUNT(*) FROM review_likes l " +
        "  WHERE l.review_id = r.id AND l.user_id = ?) AS liked " +
        "FROM product_reviews r JOIN users u ON u.id = r.user_id " +
        "WHERE r.product_id = ? AND r.deleted_at IS NULL " +
        "  AND r.parent_id IS NULL " +
        "ORDER BY r.created_at DESC LIMIT 6;",
      viewerId,
      id,
    ).map((r) => reviewRow(r, ""));

    // Section 3b: replies to the loaded roots (two-level thread; a reply to
    // a reply is stored against its root).
    let replies: ShopReview[] = [];
    if (roots.length > 0) {
      const ph = roots.map(() => "?").join(",");
      replies = queryAll<Record<string, SqlNum>>(
        "SELECT r.id, r.parent_id, u.name AS user_name, r.created_at, r.body, " +
          "(SELECT COUNT(*) FROM review_likes l " +
          "  WHERE l.review_id = r.id) AS likes, " +
          "(SELECT COUNT(*) FROM review_likes l " +
          "  WHERE l.review_id = r.id AND l.user_id = ?) AS liked " +
          "FROM product_reviews r JOIN users u ON u.id = r.user_id " +
          "WHERE r.deleted_at IS NULL AND r.parent_id IN (" +
          ph +
          ") " +
          "ORDER BY r.created_at ASC LIMIT 100;",
        viewerId,
        ...roots.map((r) => r.id),
      ).map((r) => reviewRow(r, r.parent_id ? String(r.parent_id) : ""));
    }

    // Section 4a: related = other products of the same category.
    const catId = p.category_id ? String(p.category_id) : "";
    const related = queryAll<Record<string, SqlNum>>(
      "SELECT p.id, p.name, p.base_price, " +
        "(SELECT i.image FROM product_images i " +
        "  WHERE i.product_id = p.id AND i.deleted_at IS NULL " +
        "  ORDER BY i.sort_order, i.created_at LIMIT 1) AS image " +
        "FROM products p " +
        "WHERE p.category_id = ? AND p.id <> ? AND p.deleted_at IS NULL " +
        "ORDER BY p.created_at DESC LIMIT 8;",
      catId,
      id,
    ).map(cardRow);

    // Section 4b: recommendation = best sellers excluding self + related
    // (no product twice); newest-first fallback when nothing has sold.
    const excl =
      related.length > 0
        ? " AND p.id NOT IN (" + related.map(() => "?").join(",") + ")"
        : "";
    const exclIds = related.map((r) => r.id);
    let recommend = queryAll<Record<string, SqlNum>>(
      "SELECT p.id, p.name, p.base_price, " +
        "COALESCE(SUM(oi.quantity), 0) AS sold, " +
        "(SELECT i.image FROM product_images i " +
        "  WHERE i.product_id = p.id AND i.deleted_at IS NULL " +
        "  ORDER BY i.sort_order, i.created_at LIMIT 1) AS image " +
        "FROM order_items oi JOIN products p ON p.id = oi.product_id " +
        "WHERE oi.deleted_at IS NULL " +
        "  AND p.deleted_at IS NULL AND p.id <> ?" +
        excl +
        " " +
        "GROUP BY p.id, p.name, p.base_price " +
        "ORDER BY COALESCE(SUM(oi.quantity), 0) DESC, " +
        "         p.created_at DESC LIMIT 8;",
      id,
      ...exclIds,
    ).map(cardRow);
    if (recommend.length === 0) {
      recommend = queryAll<Record<string, SqlNum>>(
        "SELECT p.id, p.name, p.base_price, " +
          "(SELECT i.image FROM product_images i " +
          "  WHERE i.product_id = p.id AND i.deleted_at IS NULL " +
          "  ORDER BY i.sort_order, i.created_at LIMIT 1) AS image " +
          "FROM products p " +
          "WHERE p.deleted_at IS NULL AND p.id <> ?" +
          excl +
          " " +
          "ORDER BY p.created_at DESC LIMIT 8;",
        id,
        ...exclIds,
      ).map((r) => ({ ...cardRow(r), sold: 0 }));
    }

    return {
      id,
      name: p.name ? String(p.name) : "",
      sku: p.sku ? String(p.sku) : "",
      cat: p.cat_name ? String(p.cat_name) : "",
      catId,
      price: num(p.base_price),
      stock: num(p.stock),
      avgRating: num(p.avg_rating),
      ratingCount: Math.trunc(num(p.rating_count)),
      commentCount: Math.trunc(num(p.comment_count)),
      sold: Math.trunc(num(p.sold)),
      starCount,
      pics: pics.slice(0, PICS_MAX),
      roots: roots.slice(0, ROOTS_MAX),
      replies: replies.slice(0, REPLIES_MAX),
      related,
      recommend: recommend.slice(0, CARDS_MAX),
    };
  },
);

function reviewRow(r: Record<string, SqlNum>, parent: string): ShopReview {
  const created = r.created_at ? String(r.created_at) : "";
  return {
    id: r.id ? String(r.id) : "",
    parent,
    user: r.user_name ? String(r.user_name) : "",
    when: created.slice(0, 10),
    body: r.body ? String(r.body) : "",
    rating: num(r.rating),
    likes: Math.trunc(num(r.likes)),
    liked: num(r.liked) > 0,
  };
}

function cardRow(r: Record<string, SqlNum>): ShopCard {
  return {
    id: r.id ? String(r.id) : "",
    name: r.name ? String(r.name) : "",
    price: num(r.base_price),
    image: r.image ? String(r.image) : "",
    sold: Math.trunc(num(r.sold)),
  };
}
