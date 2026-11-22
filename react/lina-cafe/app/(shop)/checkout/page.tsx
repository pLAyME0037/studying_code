import { redirect } from "next/navigation";
import { getI18n } from "@/app/lib/i18n";
import { loadCart } from "@/app/lib/cart";
import { money } from "@/app/lib/shop";
import { checkout } from "@/app/lib/actions";

// GET /checkout — summary + guest contact form (webc_pos serve_shop_checkout
// + display/shop/checkout.h.tt). An empty cart sends you back to /cart; a
// failed POST comes back as ?e=<key> with the fields preserved (the C code
// re-renders in place; here the state rides the query string).
type SearchParams = Promise<Record<string, string | string[] | undefined>>;

function first(v: string | string[] | undefined): string {
  if (Array.isArray(v)) return v[0] ?? "";
  return v ?? "";
}

// Failure keys the POST action can send back -> the same tr() fallbacks as
// checkout.c (webc_pos translates them at fail time).
const ERR_FALLBACKS: Record<string, string> = {
  err_name: "សូមបំពេញឈ្មោះ",
  err_phone: "សូមបញ្ចូលលេខទូរស័ព្ទត្រឹមត្រូវ",
  err_address: "សូមបំពេញអាសយដ្ឋាន",
  err_org: "មិនអាចកំណត់ហាងបានទេ",
  err_customer: "មិនអាចបង្កើតគណនីអតិថិជនបានទេ",
  err_link: "មិនអាចភ្ជាប់គណនីអតិថិជនបានទេ",
  err_save_order: "មិនអាចរក្សាទុកការបញ្ជាទិញបានទេ",
  err_save_line: "មិនអាចរក្សាទុកខ្សែទំនិញបានទេ",
  err_read_stock: "មិនអាចអានស្តុកបានទេ",
  err_low_stock: "ស្តុកមិនគ្រប់គ្រាន់សម្រាប់ការបញ្ជាទិញនេះ",
  err_dec_stock: "មិនអាចកែប្រែស្តុកបានទេ",
  err_ledger: "មិនអាចកត់ត្រាបញ្ជីស្តុកបានទេ",
  err_delivery: "មិនអាចកំណត់ការដឹកជញ្ជូនបានទេ",
};

export async function generateMetadata() {
  const { tr } = await getI18n();
  return { title: tr("checkout.page_title", "ការបង់ប្រាក់") };
}

export default async function CheckoutPage({
  searchParams,
}: {
  searchParams: SearchParams;
}) {
  const sp = await searchParams;
  const { tr } = await getI18n();
  const cart = await loadCart();
  if (cart.live === 0) redirect("/cart");

  const lines = cart.items.filter((it) => it.valid);

  const errKey = first(sp.e);
  let err: string | null = null;
  if (errKey && ERR_FALLBACKS[errKey]) {
    err = tr(`checkout.${errKey}`, ERR_FALLBACKS[errKey]);
  }

  const name = first(sp.name);
  const phone = first(sp.phone);
  const address = first(sp.address);
  const note = first(sp.note);

  const labelCls = "text-sm text-subtext0";
  const inputCls = "border border-surface0 bg-base px-2 py-2 text-base";

  return (
    <div className="grid md:grid-cols-2 gap-3">
      <div className="bg-mantle border border-surface0 self-start">
        <div className="bg-text px-3 py-2 text-base font-semibold">
          {tr("checkout.order_title", "ការបញ្ជាទិញរបស់អ្នក")}
        </div>
        {lines.map((it) => (
          <div
            key={it.id}
            className="px-3 py-2 border-b border-surface0 flex items-center gap-2 text-base"
          >
            <div className="flex-1 min-w-0">
              <div className="text-text truncate">{it.name}</div>
              <div className="text-sm text-overlay0">
                {it.sku} × {it.qty}
              </div>
            </div>
            <div className="text-text">{money(it.price * it.qty)}</div>
          </div>
        ))}
        <div className="px-3 py-2 flex items-center justify-between bg-surface0/50 border-t border-surface0 text-base">
          <span className="font-semibold text-text">
            {tr("checkout.total", "សរុប")}
          </span>
          <span className="font-semibold text-text">
            {money(cart.subtotal)}
          </span>
        </div>
      </div>

      <form
        method="POST"
        action={checkout}
        data-checkout-form
        className="bg-mantle border border-surface0 px-3 py-3 flex flex-col gap-2"
      >
        {err && (
          <div
            data-checkout-error
            className="bg-red/10 border-l-2 border-red text-red px-2 py-2 text-sm"
          >
            {err}
          </div>
        )}
        <label className={labelCls}>
          {tr("checkout.label_name", "ឈ្មោះ")}
        </label>
        <input name="name" required defaultValue={name} className={inputCls} />
        <label className={labelCls}>
          {tr("checkout.label_phone", "លេខទូរស័ព្ទ")}
        </label>
        <input
          name="phone"
          required
          defaultValue={phone}
          className={inputCls}
        />
        <label className={labelCls}>
          {tr("checkout.label_address", "អាសយដ្ឋាន")}
        </label>
        <input
          name="address"
          required
          defaultValue={address}
          className={inputCls}
        />
        <label className={labelCls}>
          {tr("checkout.label_note", "មតិ (ស្រេចចិត្ត)")}
        </label>
        <input name="note" defaultValue={note} className={inputCls} />
        <button className="bg-blue hover:brightness-90 px-2 py-2 text-base">
          {tr("checkout.submit", "បញ្ជាទិញ")}
        </button>
      </form>
    </div>
  );
}
