import Link from "next/link";
import { Fragment } from "react";
import { getSessionUser, hasPerm, type AuthUser } from "@/app/lib/auth";
import { getI18n } from "@/app/lib/i18n";
import { currentPath } from "@/app/lib/request";

// display/component/sidebar.h.tt — grouped POS nav: one row per entry; a
// non-null group renders the color-step group header above the row when
// any item of that group run is visible. Single-path SVG icons only.
type NavItem = {
  group: string | null;
  path: string;
  label: string;
  icon: string;
};

const NAV_ITEMS: NavItem[] = [
  {
    group: "Dashboard",
    path: "/dashboard",
    label: "Overview",
    icon: "M3 12l2-2m0 0l7-7 7 7M5 10v10a1 1 0 001 1h3m10-11l2 2m-2-2v10a1 1 0 01-1 1h-3m-6 0a1 1 0 001-1v-4a1 1 0 011-1h2a1 1 0 011 1v4a1 1 0 001 1m-6 0h6",
  },
  {
    group: "Catalog",
    path: "/pos/products",
    label: "Products",
    icon: "M20 7l-8-4-8 4m16 0l-8 4m8-4v10l-8 4m0-10L4 7m8 4v10M4 7v10l8 4",
  },
  {
    group: null,
    path: "/pos/categories",
    label: "Categories",
    icon: "M12 2L2 7l10 5 10-5-10-5zM2 17l10 5 10-5M2 12l10 5 10-5",
  },
  {
    group: "Sales",
    path: "/pos/sale",
    label: "New Sale",
    icon: "M3 3h2l.4 2M7 13h10l4-8H5.4M7 13L5.4 5M7 13l-2.293 2.293c-.63.63-.184 1.707.707 1.707H17m0 0a2 2 0 100 4 2 2 0 000-4zm-8 2a2 2 0 100 4 2 2 0 000-4z",
  },
  {
    group: null,
    path: "/pos/orders",
    label: "Orders",
    icon: "M3 3h2l.4 2M7 13h10l4-8H5.4M7 13L5.4 5M7 13l-2.293 2.293c-.63.63-.184 1.707.707 1.707H17m0 0a2 2 0 100 4 2 2 0 000-4zm-8 2a2 2 0 100 4 2 2 0 000-4z",
  },
  {
    group: null,
    path: "/pos/shifts",
    label: "Shifts",
    icon: "M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z",
  },
  {
    group: null,
    path: "/pos/finance",
    label: "Finance",
    icon: "M13 7h8m0 0v8m0-8l-8 8-4-4-6 6",
  },
  {
    group: "Party",
    path: "/pos/customers",
    label: "Customers",
    icon: "M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0M15 7a3 3 0 11-6 0 3 3 0 016 0zm6 3a2 2 0 11-4 0 2 2 0 014 0zM7 10a2 2 0 11-4 0 2 2 0 014 0z",
  },
  {
    group: null,
    path: "/pos/users",
    label: "Users",
    icon: "M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0M15 7a3 3 0 11-6 0 3 3 0 016 0zm6 3a2 2 0 11-4 0 2 2 0 014 0zM7 10a2 2 0 11-4 0 2 2 0 014 0z",
  },
  {
    group: null,
    path: "/pos/staff",
    label: "Staff",
    icon: "M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0M15 7a3 3 0 11-6 0 3 3 0 016 0zm6 3a2 2 0 11-4 0 2 2 0 014 0zM7 10a2 2 0 11-4 0 2 2 0 014 0z",
  },
  {
    group: null,
    path: "/pos/org",
    label: "Org Units",
    icon: "M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0M15 7a3 3 0 11-6 0 3 3 0 016 0zm6 3a2 2 0 11-4 0 2 2 0 014 0zM7 10a2 2 0 11-4 0 2 2 0 014 0z",
  },
  {
    group: "Inventory",
    path: "/pos/stocks",
    label: "Stocks",
    icon: "M5 8h14M5 8a2 2 0 110-4h14a2 2 0 110 4M5 8v10a2 2 0 002 2h10a2 2 0 002-2V8m-9 4h4",
  },
  {
    group: "Access",
    path: "/pos/roles",
    label: "Roles",
    icon: "M21 2l-2 2m-7.61 7.61a5.5 5.5 0 11-7.778 7.778 5.5 5.5 0 017.777-7.777zm0 0L15.5 7.5m0 0l3 3L22 7l-3-3m-3.5 3.5L19 4",
  },
  {
    group: null,
    path: "/pos/permissions",
    label: "Permissions",
    icon: "M21 2l-2 2m-7.61 7.61a5.5 5.5 0 11-7.778 7.778 5.5 5.5 0 017.777-7.777zm0 0L15.5 7.5m0 0l3 3L22 7l-3-3m-3.5 3.5L19 4",
  },
  {
    group: "Config",
    path: "/pos/provinces",
    label: "Provinces",
    icon: "M14.7 6.3a1 1 0 000 1.4l1.6 1.6a1 1 0 001.4 0l3.77-3.77a6 6 0 01-7.94 7.94l-6.91 6.91a2.12 2.12 0 01-3-3l6.91-6.91a6 6 0 017.94-7.94l-3.76 3.76z",
  },
  {
    group: null,
    path: "/pos/districts",
    label: "Districts",
    icon: "M14.7 6.3a1 1 0 000 1.4l1.6 1.6a1 1 0 001.4 0l3.77-3.77a6 6 0 01-7.94 7.94l-6.91 6.91a2.12 2.12 0 01-3-3l6.91-6.91a6 6 0 017.94-7.94l-3.76 3.76z",
  },
  {
    group: null,
    path: "/pos/communes",
    label: "Communes",
    icon: "M14.7 6.3a1 1 0 000 1.4l1.6 1.6a1 1 0 001.4 0l3.77-3.77a6 6 0 01-7.94 7.94l-6.91 6.91a2.12 2.12 0 01-3-3l6.91-6.91a6 6 0 017.94-7.94l-3.76 3.76z",
  },
  {
    group: null,
    path: "/pos/villages",
    label: "Villages",
    icon: "M14.7 6.3a1 1 0 000 1.4l1.6 1.6a1 1 0 001.4 0l3.77-3.77a6 6 0 01-7.94 7.94l-6.91 6.91a2.12 2.12 0 01-3-3l6.91-6.91a6 6 0 017.94-7.94l-3.76 3.76z",
  },
  {
    group: null,
    path: "/pos/geo",
    label: "CAM-Geo",
    icon: "M14.7 6.3a1 1 0 000 1.4l1.6 1.6a1 1 0 001.4 0l3.77-3.77a6 6 0 01-7.94 7.94l-6.91 6.91a2.12 2.12 0 01-3-3l6.91-6.91a6 6 0 017.94-7.94l-3.76 3.76z",
  },
  {
    group: null,
    path: "/pos/dictionaries",
    label: "Dictionaries",
    icon: "M14.7 6.3a1 1 0 000 1.4l1.6 1.6a1 1 0 001.4 0l3.77-3.77a6 6 0 01-7.94 7.94l-6.91 6.91a2.12 2.12 0 01-3-3l6.91-6.91a6 6 0 017.94-7.94l-3.76 3.76z",
  },
  {
    group: null,
    path: "/pos/i18n",
    label: "Languages",
    icon: "M14.7 6.3a1 1 0 000 1.4l1.6 1.6a1 1 0 001.4 0l3.77-3.77a6 6 0 01-7.94 7.94l-6.91 6.91a2.12 2.12 0 01-3-3l6.91-6.91a6 6 0 017.94-7.94l-3.76 3.76z",
  },
  {
    group: null,
    path: "/pos/config",
    label: "Configs",
    icon: "M14.7 6.3a1 1 0 000 1.4l1.6 1.6a1 1 0 001.4 0l3.77-3.77a6 6 0 01-7.94 7.94l-6.91 6.91a2.12 2.12 0 01-3-3l6.91-6.91a6 6 0 017.94-7.94l-3.76 3.76z",
  },
  {
    group: "Monitoring",
    path: "/pos/alerts",
    label: "Alerts",
    icon: "M15 17h5l-1.405-1.405A2.032 2.032 0 0118 14.158V11a6.002 6.002 0 00-4-5.659V5a2 2 0 10-4 0v.341C7.67 6.165 6 8.388 6 11v3.159c0 .538-.214 1.055-.595 1.436L4 17h5m6 0v1a3 3 0 11-6 0v-1m6 0H9",
  },
  {
    group: null,
    path: "/pos/audit",
    label: "Audit",
    icon: "M15 17h5l-1.405-1.405A2.032 2.032 0 0118 14.158V11a6.002 6.002 0 00-4-5.659V5a2 2 0 10-4 0v.341C7.67 6.165 6 8.388 6 11v3.159c0 .538-.214 1.055-.595 1.436L4 17h5m6 0v1a3 3 0 11-6 0v-1m6 0H9",
  },
  {
    group: "Reports",
    path: "/reports",
    label: "Reports",
    icon: "M9 12h6m-6 4h6m2 5H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z",
  },
  {
    group: "Demo",
    path: "/users",
    label: "Demo Users",
    icon: "M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0M15 7a3 3 0 11-6 0 3 3 0 016 0zm6 3a2 2 0 11-4 0 2 2 0 014 0zM7 10a2 2 0 11-4 0 2 2 0 014 0z",
  },
  {
    group: null,
    path: "/notes",
    label: "Notes",
    icon: "M9 11l3 3L22 4M21 12v7a2 2 0 01-2 2H5a2 2 0 01-2-2V5a2 2 0 012-2h11",
  },
  {
    group: null,
    path: "/people",
    label: "People",
    icon: "M16 21v-2a4 4 0 00-4-4H6a4 4 0 00-4 4v2M12 11a4 4 0 100-8 4 4 0 000 8zm8 10v-2a4 4 0 00-3-3.87M4 21v-2a4 4 0 013-3.87",
  },
  {
    group: null,
    path: "/version",
    label: "Version",
    icon: "M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z",
  },
  {
    group: null,
    path: "/api/notes",
    label: "API",
    icon: "M10 20l4-16m4 4l4 4-4 4M6 16l-4-4 4-4",
  },
];

// One required permission per nav row (NULL = visible to all), mirroring
// NAV_ITEMS 1:1 (webc_pos nav_perms + _Static_assert).
const NAV_PERMS: (string | null)[] = [
  null, // Dashboard / Overview
  "SD.PRODUCT.MGMT", // Products
  "SD.PRODUCT.MGMT", // Categories
  "SD.SELL", // New Sale
  "SD.SELL", // Orders
  "SD.SELL", // Shifts
  "SD.FINANCE", // Finance
  "SD.SELL", // Customers
  "SD.USER.MGMT", // Users
  "SD.USER.MGMT", // Staff
  "SD.USER.MGMT", // Org Units
  "SD.STOCK.ADJUST", // Stocks
  "SD.USER.MGMT", // Roles
  "SD.USER.MGMT", // Permissions
  "SD.SETTINGS", // Provinces
  "SD.SETTINGS", // Districts
  "SD.SETTINGS", // Communes
  "SD.SETTINGS", // Villages
  "SD.SETTINGS", // CAM-Geo
  "SD.SETTINGS", // Dictionaries
  "SD.SETTINGS", // Languages
  "SD.SETTINGS", // Configs
  "SD.AUDIT", // Alerts
  "SD.AUDIT", // Audit
  "SD.REPORT.VIEW", // Reports
  null,
  null,
  null,
  null,
  null, // Demo Users / Notes / People / Version / API
];

// Sidebar identity: picture ringed by account status or an initials
// circle; nobody signed in -> generic workspace avatar (header.c
// sidebar_avatar_html).
function Avatar({ user }: { user: AuthUser | null }) {
  let ring = "";
  if (user !== null) {
    if (user.status === "ACTIVE") ring = " ring-2 ring-blue";
    else if (user.status === "INACTIVE") ring = " ring-2 ring-peach";
    else if (user.status === "SUSPENDED") ring = " ring-2 ring-red";
  }
  if (user !== null && user.profile_pic) {
    return (
      // eslint-disable-next-line @next/next/no-img-element
      <img
        src={user.profile_pic}
        alt=""
        className={`h-10 w-10 rounded-full object-cover shrink-0${ring}`}
      />
    );
  }
  const name = user !== null ? user.name : "";
  let initial = "?";
  if (name) {
    // First Unicode scalar of the name (webc_pos reads the first UTF-8
    // sequence, 1-4 bytes).
    const cp = name.codePointAt(0);
    if (cp !== undefined) initial = String.fromCodePoint(cp);
  }
  return (
    <span
      className={`h-10 w-10 rounded-full shrink-0 inline-flex items-center justify-center text-sm font-semibold bg-surface1 text-subtext0${ring}`}
    >
      {initial}
    </span>
  );
}

export async function Sidebar() {
  const user = await getSessionUser();
  const { tr } = await getI18n();
  const full = await currentPath();
  const pathname = full.split("?", 1)[0];

  const visible = NAV_ITEMS.map(
    (_, i) =>
      user === null ||
      NAV_PERMS[i] === null ||
      hasPerm(user, NAV_PERMS[i] as string),
  );
  const groupOn = NAV_ITEMS.map((item, i) => {
    if (item.group === null) return false;
    for (let j = i; j < NAV_ITEMS.length; ++j) {
      if (j > i && NAV_ITEMS[j].group !== null) break;
      if (visible[j]) return true;
    }
    return false;
  });

  // Sidebar card: name + role (fallback email, then the workspace label).
  const uName = user !== null ? user.name : "";
  let uSub = "Staff workspace";
  if (user !== null && user.role) uSub = user.role;
  else if (user !== null && user.email) uSub = user.email;

  return (
    <>
      <aside
        id="sidebar"
        className="shrink-0 flex flex-col border-r border-surface0 bg-mantle transition-all duration-300 h-full"
      >
        <div className="border-b border-surface0 bg-crust shrink-0">
          <div id="sidebar-pad" className="flex items-center p-3 user-card">
            <Link href="/accounts">
              <Avatar user={user} />
            </Link>
            <div className="ml-3 min-w-0 nav-label">
              <p className="text-sm font-semibold text-text truncate">{uName}</p>
              <p className="text-xs text-subtext0 truncate">{uSub}</p>
            </div>
          </div>
          <div className="sidebar-tooltip">{uName}</div>
        </div>

        <nav className="flex-1 py-0 px-3 space-y-1 overflow-y-auto no-scrollbar">
          {NAV_ITEMS.map((item, ni) => {
            const isActive =
              (pathname === item.path ||
                (item.path !== "/" && pathname.startsWith(item.path))) &&
              (item.path !== "/" || pathname === "/");
            return (
              <Fragment key={item.path}>
                {item.group !== null && groupOn[ni] && (
                  <div className="px-2 py-1 text-xs font-semibold uppercase tracking-wide text-overlay0 bg-surface0/60 nav-label">
                    {tr(item.group, item.group)}
                  </div>
                )}
                {visible[ni] && (
                  <div className="relative group/item mb-1">
                    <Link
                      href={item.path}
                      className={`nav-link flex items-center transition-all duration-200 ease-in-out p-2 ${
                        isActive ? "nav-active" : "text-subtext0"
                      }`}
                    >
                      <svg
                        className="w-5 h-5 shrink-0"
                        fill="none"
                        stroke="currentColor"
                        viewBox="0 0 24 24"
                      >
                        <path
                          strokeLinecap="round"
                          strokeLinejoin="round"
                          strokeWidth="2"
                          d={item.icon}
                        />
                      </svg>
                      <span className="ml-3 text-sm font-medium whitespace-nowrap flex-1 nav-label">
                        {tr(item.label, item.label)}
                      </span>
                    </Link>
                    <div className="sidebar-tooltip">
                      {tr(item.label, item.label)}
                    </div>
                  </div>
                )}
              </Fragment>
            );
          })}
        </nav>

        <div id="sidebar-pad" className="border-t border-surface0 shrink-0">
          <Link
            href="/accounts"
            className="nav-link relative flex items-center w-full p-2 text-subtext0 hover:bg-blue/10 hover:text-blue transition-colors"
          >
            <svg
              className="w-5 h-5 shrink-0"
              fill="none"
              stroke="currentColor"
              viewBox="0 0 24 24"
            >
              <path
                strokeLinecap="round"
                strokeLinejoin="round"
                strokeWidth="2"
                d="M17 19v-2a4 4 0 00-4-4H5a4 4 0 00-4 4v2M9 9a4 4 0 100-8 4 4 0 000 8zM23 19v-2a4 4 0 00-3-3.87M16 3.13a4 4 0 010 7.75"
              />
            </svg>
            <span className="ml-3 text-sm font-medium whitespace-nowrap nav-label">
              {tr("Accounts", "Accounts")}
            </span>
            <div className="sidebar-tooltip">{tr("Accounts", "Accounts")}</div>
          </Link>
          <form method="POST" action="/logout">
            <button
              type="submit"
              className="nav-link relative flex items-center w-full p-2 text-subtext0 hover:bg-red/10 hover:text-red transition-colors group"
            >
              <svg
                className="w-5 h-5 shrink-0"
                fill="none"
                stroke="currentColor"
                viewBox="0 0 24 24"
              >
                <path
                  strokeLinecap="round"
                  strokeLinejoin="round"
                  strokeWidth="2"
                  d="M17 16l4-4m0 0l-4-4m4 4H7m6 4v1a3 3 0 01-3 3H6a3 3 0 01-3-3V7a3 3 0 013-3h4a3 3 0 013 3v1"
                />
              </svg>
              <span className="ml-3 text-sm font-medium whitespace-nowrap nav-label">
                {tr("Log Out", "Log Out")}
              </span>
              <div className="sidebar-tooltip">{tr("Log Out", "Log Out")}</div>
            </button>
          </form>
        </div>
      </aside>
      <div id="sidebar-backdrop" data-sidebar-close />
    </>
  );
}
