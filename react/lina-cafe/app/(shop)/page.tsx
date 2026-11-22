import Link from "next/link";
import { getI18n } from "@/app/lib/i18n";
import { queryAll, queryOne } from "@/app/lib/db";
import { money, firstGlyph, listQuery } from "@/app/lib/shop";
import { cartAdd } from "@/app/lib/actions";

// GET / - catalog grid with category chips, search and plain ?page= links
// (webc_pos serve_shop_index + display/shop/index.h.tt).
const SHOP_PER_PAGE = 12;

type SearchParams = Promise<Record<string, string | string[] | undefined>>;

function first(v: string | string[] | undefined): string {
  if (Array.isArray(v)) return v[0] ?? "";
  return v ?? "";
}

export async function generateMetadata() {
  const { tr } = await getI18n();
  return { title: tr("shop.title_products", "ផលិតផល") };
}

type GridRow = {
  id: string;
  name: string;
  sku: string | null;
  price: unknown;
  cat: string | null;
  stock: unknown;
};

export default async function ShopIndex({
  searchParams,
}: {
  searchParams: SearchParams;
}) {
  const sp = await searchParams;
  const { tr } = await getI18n();

  let q = first(sp.q);
  if (q.length > 100) q = q.slice(0, 100);
  const cat = first(sp.cat);
  const pageRaw = parseInt(first(sp.page), 10);
  let page = Number.isNaN(pageRaw) || pageRaw < 1 ? 1 : pageRaw;
  let offset = (page - 1) * SHOP_PER_PAGE;

  const pattern = q.length > 0 ? `%${q}%` : "%";

  // Category chips (active run highlighted).
  const chips = queryAll<{ id: string; name: string }>(
    "SELECT id, name FROM categories " +
      "WHERE deleted_at IS NULL ORDER BY name LIMIT 50;",
  ).map((c) => ({ id: String(c.id), name: String(c.name), on: c.id === cat }));

  const total = Number(
    queryOne<{ c: number | string }>(
      "SELECT COUNT(*) AS c FROM products p " +
        "JOIN categories c ON c.id = p.category_id " +
        "WHERE p.deleted_at IS NULL AND c.deleted_at IS NULL " +
        "AND (? = '' OR c.id = ?) " +
        "AND (? = '' OR p.name LIKE ? OR p.sku LIKE ?);",
      cat,
      cat,
      pattern,
      pattern,
      pattern,
    )?.c ?? 0,
  );
  const pages = Math.max(1, Math.ceil(total / SHOP_PER_PAGE));
  if (page > pages) {
    page = pages;
    offset = (page - 1) * SHOP_PER_PAGE;
  }

  const rows = queryAll<GridRow>(
    "SELECT p.id, p.name, p.sku, p.base_price, c.name AS cat, " +
      "COALESCE((SELECT SUM(s.quantity) FROM inventory_stocks s " +
      "          WHERE s.product_id = p.id AND s.deleted_at IS NULL), 0) AS stock " +
      "FROM products p " +
      "JOIN categories c ON c.id = p.category_id " +
      "WHERE p.deleted_at IS NULL AND c.deleted_at IS NULL " +
      "AND (? = '' OR c.id = ?) " +
      "AND (? = '' OR p.name LIKE ? OR p.sku LIKE ?) " +
      "ORDER BY p.created_at DESC, p.id DESC " +
      "LIMIT ? OFFSET ?;",
    cat,
    cat,
    pattern,
    pattern,
    pattern,
    SHOP_PER_PAGE,
    offset,
  );

  const allLabel = tr("shop.all", "ទាំងអស់");
  const addLabel = tr("shop.add_to_cart", "ដាក់ក្នុងរទេះ");
  const outLabel = tr("shop.out_of_stock", "អស់ពីរាក់");
  const inStockLabel = tr("shop.in_stock", "នៅសល់: ");
  const pager = tr("shop.pager", "ទំព័រ %d/%d")
    .replace("%d", String(page))
    .replace("%d", String(pages));

  return (
    <div className="flex flex-col gap-3">
      <div className="flex items-center gap-1 flex-wrap">
        <Link
          href={listQuery("", q, 1)}
          className={
            cat.length === 0
              ? "px-2.5 py-2 text-sm border border-text bg-text text-onbase"
              : "px-2.5 py-2 text-sm border border-surface0 bg-mantle text-subtext0 hover:border-surface1"
          }
        >
          {allLabel}
        </Link>
        {chips.map((chip) => (
          <Link
            key={chip.id}
            href={listQuery(chip.id, q, 1)}
            className={
              chip.on
                ? "px-2.5 py-2 text-sm border border-blue bg-blue text-onbase"
                : "px-2.5 py-2 text-sm border border-surface0 bg-mantle text-subtext0 hover:border-surface1"
            }
          >
            {chip.name}
          </Link>
        ))}
        {q.length > 0 && (
          <span className="text-sm text-overlay0 px-1">
            {tr("shop.results_for", "លទ្ធផលសម្រាប់")} &quot;{q}&quot;
          </span>
        )}
      </div>

      {rows.length === 0 ? (
        <div className="bg-mantle border border-surface0 text-center px-3 py-8">
          <div className="text-overlay0 text-base">
            {tr("shop.no_products", "មិនមានផលិតផលទេ")}
          </div>
          <Link href="/" className="text-sm text-blue hover:underline">
            {tr("shop.clear_filters", "លុបតម្រង")}
          </Link>
        </div>
      ) : (
        <div className="grid grid-cols-2 md:grid-cols-3 gap-3">
          {rows.map((r) => {
            const id = String(r.id);
            const name = r.name ? String(r.name) : "";
            const price = Number(r.price ?? 0);
            const stock = Number(r.stock ?? 0);
            const out = stock <= 0;
            const stockText = out
              ? outLabel
              : inStockLabel + String(Math.trunc(stock)); // "%.0f"
            const stockClass = out ? "text-sm text-red" : "text-sm text-green";
            return (
              <div
                key={id}
                className="bg-mantle border border-surface0 flex flex-col"
              >
                <Link
                  href={`/product/${id}`}
                  className="bg-blue/10 h-40 flex items-center justify-center text-blue font-bold text-4xl"
                >
                  {firstGlyph(name)}
                </Link>
                <div className="px-2 py-2 flex flex-col gap-1 flex-1">
                  <Link
                    href={`/product/${id}`}
                    className="text-base font-medium text-text hover:text-blue leading-snug"
                  >
                    {name}
                  </Link>
                  <div className="text-sm text-overlay0 truncate">
                    {r.sku} · {r.cat}
                  </div>
                  <div className="text-lg font-semibold text-blue">
                    {money(price)}
                  </div>
                  <div className={stockClass}>{stockText}</div>
                </div>
                <form method="POST" action={cartAdd} className="border-t border-surface0 p-1">
                  <input type="hidden" name="product_id" value={id} />
                  <input type="hidden" name="redirect" value="/" />
                  <button
                    className="w-full bg-blue text-onbase hover:brightness-90 px-2.5 py-2 text-base disabled:bg-surface0 disabled:text-overlay0"
                    disabled={out}
                  >
                    {addLabel}
                  </button>
                </form>
              </div>
            );
          })}
        </div>
      )}

      {pages > 1 && (
        <div className="flex items-center justify-center gap-2 text-base">
          {page > 1 && (
            <Link
              href={listQuery(cat, q, page - 1)}
              className="px-2.5 py-2 border border-surface0 bg-mantle text-base"
            >
              «
            </Link>
          )}
          <span className="text-overlay0">{pager}</span>
          {page < pages && (
            <Link
              href={listQuery(cat, q, page + 1)}
              className="px-2.5 py-2 border border-surface0 bg-mantle text-base"
            >
              »
            </Link>
          )}
        </div>
      )}
    </div>
  );
}
