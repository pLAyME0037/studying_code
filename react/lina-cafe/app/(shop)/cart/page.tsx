import Link from "next/link";
import { getI18n } from "@/app/lib/i18n";
import { loadCart } from "@/app/lib/cart";
import { money } from "@/app/lib/shop";
import { cartRemove, cartUpdate } from "@/app/lib/actions";

// GET /cart — webc_pos serve_shop_cart + display/shop/cart.h.tt. Lines come
// from the cookie cart (invalid product ids render nothing, subtotal counts
// valid lines only).
export async function generateMetadata() {
  const { tr } = await getI18n();
  return { title: tr("cart.title", "រទេះទំនិញ") };
}

export default async function CartPage() {
  const { tr } = await getI18n();
  const cart = await loadCart();
  const lines = cart.items.filter((it) => it.valid);

  const tContinue = tr("cart.continue", "បន្តទិញ");

  return (
    <div className="bg-mantle border border-surface0">
      <div className="bg-text text-onbase px-3 py-2 flex items-center justify-between">
        <span className="text-base font-semibold">
          {tr("cart.title", "រទេះទំនិញ")}
        </span>
        <Link href="/" className="text-sm text-onbase/70 hover:text-onbase">
          {tContinue}
        </Link>
      </div>

      {lines.length === 0 ? (
        <div className="text-center px-3 py-8">
          <div className="text-base">
            {tr("cart.empty", "រទេះទំនិញទំនេរ")}
          </div>
          <Link href="/" className="text-sm text-blue hover:underline">
            {tr("cart.view_products", "មើលផលិតផល")}
          </Link>
        </div>
      ) : (
        <>
          {lines.map((it) => {
            const low = it.qty > it.stock;
            return (
              <div
                key={it.id}
                data-cart-row
                className="px-3 py-2 border-b border-surface0 flex items-center gap-3"
              >
                <div className="flex-1 min-w-0">
                  <div className="text-base font-medium truncate">{it.name}</div>
                  <div className="text-sm text-overlay0 truncate">
                    {it.sku} · {money(it.price)}
                    {low && (
                      <>
                        {" "}
                        ·{" "}
                        <span className="text-red">
                          {tr("cart.low_stock", "ស្តុកមិនគ្រប់គ្រាន់")}
                        </span>
                      </>
                    )}
                  </div>
                </div>
                <form method="POST" action={cartUpdate} className="flex items-center gap-1">
                  <input type="hidden" name="product_id" value={it.id} />
                  <input type="hidden" name="redirect" value="/cart" />
                  <input
                    type="number"
                    name="qty"
                    defaultValue={it.qty}
                    min={0}
                    className="border border-surface0 bg-mantle px-1 py-2 text-base w-16"
                  />
                  <button className="border border-surface0 px-2 py-2 text-sm bg-mantle text-subtext0 hover:bg-surface0">
                    {tr("cart.edit", "កែ")}
                  </button>
                </form>
                <div className="text-base w-24 text-right font-medium">
                  {money(it.price * it.qty)}
                </div>
                <form method="POST" action={cartRemove}>
                  <input type="hidden" name="product_id" value={it.id} />
                  <button className="text-red text-sm hover:underline">
                    {tr("cart.remove", "លុប")}
                  </button>
                </form>
              </div>
            );
          })}

          <div className="px-3 py-2 flex items-center justify-between bg-surface0/50 border-t border-surface0">
            <span className="text-base font-semibold">
              {tr("cart.subtotal", "សរុប")}
            </span>
            <span className="text-base font-semibold">
              {money(cart.subtotal)}
            </span>
          </div>
          <div className="px-3 py-2 flex items-center justify-end gap-2 border-t border-surface0">
            <Link
              href="/"
              className="border border-surface0 px-2 py-2 text-base bg-mantle hover:bg-surface0"
            >
              {tContinue}
            </Link>
            <Link
              href="/checkout"
              className="bg-blue hover:brightness-90 px-2 py-2 text-base"
            >
              {tr("cart.checkout", "បង់ប្រាក់")}
            </Link>
          </div>
        </>
      )}
    </div>
  );
}
