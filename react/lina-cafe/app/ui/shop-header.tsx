import Link from "next/link";
import { cookies } from "next/headers";
import { getSessionUser } from "@/app/lib/auth";
import { cartEntryCount, CART_COOKIE } from "@/app/lib/cart";
import { getI18n } from "@/app/lib/i18n";
import { shopName } from "@/app/lib/shop";
import { currentPath } from "@/app/lib/request";
import { LangForm } from "./lang-form";

// display/shop/chrome_start.h.tt — storefront header. Session, cart count,
// translations and the language form are runtime-dependent, so this async
// component streams behind the layout's <Suspense> (static shell = the
// header frame in (shop)/layout.tsx).
export async function ShopHeader() {
  const user = await getSessionUser();
  const { tr } = await getI18n();
  const jar = await cookies();
  const count = cartEntryCount(jar.get(CART_COOKIE)?.value);
  const name = shopName();
  const back = await currentPath();

  // Phase 16 signup: anonymous = admin entry + sign in + sign up;
  // signed-in = identity + logout, and only ADMIN keeps the staff
  // dashboard link (CUSTOMER sessions belong on the storefront).
  const isAdmin = user !== null && user.user_type === "ADMIN";

  return (
    <header className="bg-mantle border-b border-surface0">
      <div className="max-w-7xl mx-auto px-3 py-2 flex items-center gap-2 flex-wrap">
        <Link href="/" className="font-bold whitespace-nowrap">
          {name}
        </Link>
        <form method="GET" action="/" className="flex items-center gap-1 flex-1">
          <input
            name="q"
            placeholder={tr("shop.search.placeholder", "ស្វែងរកផលិតផល...")}
            className="border border-surface0 bg-mantle px-2.5 py-2 text-base flex-1 min-w-0"
          />
          <button className="bg-blue hover:brightness-90 px-2.5 py-2 text-base whitespace-nowrap">
            {tr("shop.search.button", "ស្វែងរក")}
          </button>
        </form>
        <Link
          href="/cart"
          className="text-base border border-surface0 px-2.5 py-2 whitespace-nowrap"
        >
          {tr("shop.cart_link", "រទេះ")} ({count})
        </Link>
        {(user === null || isAdmin) && (
          <Link href="/dashboard" className="text-base whitespace-nowrap">
            {tr("shop.admin_link", "ផ្ទៃគ្រប់គ្រង")}
          </Link>
        )}
        {user !== null && (
          <>
            <span className="text-base px-1 whitespace-nowrap">
              {user.name || user.id}
            </span>
            <Link href="/accounts" className="text-blue px-1 whitespace-nowrap">
              {tr("shop.accounts_link", "គណនី")}
            </Link>
            <Link
              href="/logout"
              className="text-base border border-surface0 px-2.5 py-2 whitespace-nowrap"
            >
              {tr("shop.logout_link", "ចេញ")}
            </Link>
          </>
        )}
        {user === null && (
          <>
            <Link
              href="/login"
              className="text-base border border-surface0 px-2.5 py-2 whitespace-nowrap"
            >
              {tr("shop.signin_link", "ចូល")}
            </Link>
            <Link
              href="/signup"
              className="text-base bg-blue px-2.5 py-2 whitespace-nowrap"
            >
              {tr("shop.signup_link", "ចុះឈ្មោះ")}
            </Link>
          </>
        )}
        <div className="flex items-center gap-0.5 text-sm" data-theme-switch>
          <button
            type="button"
            data-set-theme="light"
            title={tr("shop.theme_light", "ស្រាល")}
            className="px-1.5 py-1 border border-surface0 text-subtext0 hover:bg-surface0"
          >
            &#9788;
          </button>
          <button
            type="button"
            data-set-theme="dark"
            title={tr("shop.theme_dark", "ងងឹត")}
            className="px-1.5 py-1 border border-surface0 text-subtext0 hover:bg-surface0"
          >
            &#9790;
          </button>
          <button
            type="button"
            data-set-theme="device"
            title={tr("shop.theme_auto", "ស្វ័យប្រវត្តិ")}
            className="px-1.5 py-1 border border-surface0 text-subtext0 hover:bg-surface0"
          >
            &#9680;
          </button>
        </div>
        <LangForm back={back} />
      </div>
    </header>
  );
}
