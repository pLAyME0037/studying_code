import Link from "next/link";
import { cache } from "react";
import { notFound } from "next/navigation";
import { getI18n } from "@/app/lib/i18n";
import { queryAll, queryOne } from "@/app/lib/db";
import { money, statusLabel } from "@/app/lib/shop";

// GET /order/<id> — guest confirmation; the uuid id is the unguessable
// token (webc_pos serve_shop_order + display/shop/order.h.tt).
type PageProps = { params: Promise<{ id: string }> };

// One round trip per section; cache() shares the rows between
// generateMetadata (<title> = order number) and the page body.
const loadOrder = cache((id: string) => {
  const o = queryOne<Record<string, unknown>>(
    "SELECT o.order_number, COALESCE(o.order_status_dict_id, '') AS status, " +
      "o.total_amount, o.created_at, " +
      "COALESCE((SELECT recipient_name FROM deliveries " +
      "          WHERE order_id = o.id LIMIT 1), '') AS rname, " +
      "COALESCE((SELECT recipient_phone FROM deliveries " +
      "          WHERE order_id = o.id LIMIT 1), '') AS rphone, " +
      "COALESCE((SELECT delivery_address FROM deliveries " +
      "          WHERE order_id = o.id LIMIT 1), '') AS raddr " +
      "FROM orders o WHERE o.id = ? AND o.deleted_at IS NULL;",
    id,
  );
  if (!o) return null;

  const lines = queryAll<{
    name: unknown;
    quantity: unknown;
    total_line: unknown;
  }>(
    "SELECT p.name, oi.unit_price, oi.quantity, oi.total_line " +
      "FROM order_items oi JOIN products p ON p.id = oi.product_id " +
      "WHERE oi.order_id = ? AND oi.deleted_at IS NULL " +
      "ORDER BY oi.created_at, oi.id;",
    id,
  );
  return { o, lines };
});

export async function generateMetadata({ params }: PageProps) {
  const { id } = await params;
  const data = loadOrder(id);
  // Pre-flush: missing order -> real 404 status (see product page note).
  if (!data) notFound();
  return { title: String(data.o.order_number ?? "") };
}

export default async function OrderPage({ params }: PageProps) {
  const { id } = await params;
  const { tr } = await getI18n();

  const data = loadOrder(id);
  if (!data) notFound();
  const { o, lines } = data;

  const number = o.order_number ? String(o.order_number) : "";
  const status = o.status ? String(o.status) : "";
  const total = Number(o.total_amount ?? 0);
  const created = o.created_at ? String(o.created_at) : "";
  const rname = o.rname ? String(o.rname) : "";
  const rphone = o.rphone ? String(o.rphone) : "";
  const raddr = o.raddr ? String(o.raddr) : "";

  return (
    <div className="bg-mantle border border-surface0 max-w-lg mx-auto w-full">
      <div className="bg-green text-onbase px-3 py-3">
        <div className="font-semibold">
          {tr("order.thanks", "អរគុណសម្រាប់ការបញ្ជាទិញ!")}
        </div>
        <div className="text-sm text-onbase/80">
          {tr("order.number", "លេខបញ្ជាទិញ")}: {number} · {created}
        </div>
      </div>
      <div className="px-3 py-2 border-b border-surface0 flex items-center gap-2">
        <span className="bg-yellow text-onbase border border-yellow px-2 py-0.5 text-sm">
          {statusLabel(tr, status)}
        </span>
        <span className="text-sm text-overlay0">
          {tr("order.status_now", "ស្ថានភាពបច្ចុប្បន្ន")}
        </span>
      </div>
      {lines.map((ln, i) => (
        <div
          key={i}
          className="px-3 py-2 border-b border-surface0 flex items-center gap-2 text-base"
        >
          <div className="flex-1 min-w-0 text-text truncate">
            {ln.name ? String(ln.name) : ""}
          </div>
          <div className="text-sm text-overlay0">
            × {Number(ln.quantity ?? 0)}
          </div>
          <div className="text-text">{money(Number(ln.total_line ?? 0))}</div>
        </div>
      ))}
      <div className="px-3 py-2 flex items-center justify-between bg-surface0/50 border-y border-surface0 text-base">
        <span className="font-semibold text-text">
          {tr("checkout.total_due", "សរុបត្រូវបង់")}
        </span>
        <span className="font-semibold text-text">{money(total)}</span>
      </div>
      <div className="px-3 py-2 text-sm text-subtext0 border-b border-surface0">
        {tr("checkout.label_name", "ឈ្មោះ")}: {rname} ·{" "}
        {tr("checkout.label_phone", "លេខទូរស័ព្ទ")}: {rphone}
        <br />
        {tr("checkout.label_address", "អាសយដ្ឋាន")}: {raddr}
      </div>
      <div className="px-3 py-2 text-sm text-subtext0">
        {tr(
          "order.contact_note",
          "យើងនឹងទាក់ទងលោកអ្នកដើម្បីបញ្ជាក់ការបញ្ជាទិញ។",
        )}
      </div>
      <div className="px-3 py-2 border-t border-surface0">
        <Link href="/" className="text-sm text-blue hover:underline">
          {tr("shop.back", "« ត្រឡប់ទៅហាង")}
        </Link>
      </div>
    </div>
  );
}
