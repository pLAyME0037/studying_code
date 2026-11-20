import { getI18n } from "@/app/lib/i18n";

// display/shop/chrome_end.h.tt — storefront footer.
export async function ShopFooter() {
  const { tr } = await getI18n();
  return (
    <footer className="bg-mantle border-t border-surface0 mt-auto">
      <div className="max-w-7xl mx-auto px-3 py-2 text-sm text-overlay0">
        {tr("shop.footer", "POS · ហាងលក់រាយ")}
      </div>
    </footer>
  );
}
