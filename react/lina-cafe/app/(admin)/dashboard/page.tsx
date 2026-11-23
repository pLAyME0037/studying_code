import Link from "next/link";
import { AdminChrome } from "@/app/ui/admin-chrome";
import { getSessionUser, type AuthUser } from "@/app/lib/auth";
import { getI18n } from "@/app/lib/i18n";
import { queryAll, queryOne } from "@/app/lib/db";

// /dashboard — port of webc_pos src/dashboard/dashboard.c (dashboard_load)
// + display/layout/dashboard.h.tt, served by src/layout/dashboard.c with
// chrome title "Dashboard". One template, three role centers: role_code
// picks the workspace (SD-MANAGER / SD-CASHIER / SD-DRIVER / none).

// dashboard.c DAY_SECS, in ms — Date.now() is milliseconds (webc's now is
// time_t seconds), so the day offsets must be ms or the window collapses
// into "today".
const DAY_MS = 86400 * 1000;
const BAR_H = 72; // dashboard.c BAR_H — bar pixel ceiling

type Kpi = {
  label: string;
  value: string;
  delta: string;
  deltaShow: boolean;
  deltaTone: number; // 1 green, 0 red, 2 neutral
};
type DayRow = { d: string; sales: number; n: number; mineSales: number; mineN: number };
type DashOrder = {
  number: string;
  time: string;
  who: string;
  total: string;
  status: string;
  toneCls: string;
};
type DashDeliv = {
  status: string;
  toneCls: string;
  recipient: string;
  phone: string;
  note: string;
  number: string;
};
type Bar = { value: string; pct: number; day: string };
type Ws = {
  kind: number;
  greeting: string;
  dateHuman: string;
  workspace: string;
  roleCls: string;
  roleLabel: string;
  kpis: Kpi[];
  attention: string;
  attentionHref: string;
  attentionCls: string;
  bars: Bar[];
  barsSum: string;
  hasShift: boolean;
  shiftSince: string;
  shiftCash: string;
  shiftOrg: string;
  recent: DashOrder[];
  delPending: number;
  delTransit: number;
  delDone: number;
  delivs: DashDeliv[];
  outOfStock: number;
};

// ---- helpers (dashboard.c: iso_day / local_hm / money / col_double) -----

// UTC day key — matches SQL substr(created_at, 1, 10) on stored UTC stamps.
function isoDay(t: number): string {
  return new Date(t).toISOString().slice(0, 10);
}

// "YYYY-MM-DDTHH:MM..." (stored UTC) -> shop-local "HH:MM". Bad/short
// input -> "-" (webc sscanf requires the literal 'T').
function localHm(iso: string | null | undefined): string {
  if (iso && iso.length >= 16) {
    const m = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})/.exec(iso);
    if (m) {
      // timegm(parsed fields) then localtime_r — Date.UTC + local getters.
      const d = new Date(Date.UTC(+m[1], +m[2] - 1, +m[3], +m[4], +m[5]));
      const h = String(d.getHours()).padStart(2, "0");
      const mi = String(d.getMinutes()).padStart(2, "0");
      return `${h}:${mi}`;
    }
  }
  return "-";
}

function money(v: number): string {
  return v.toFixed(2) + " $";
}

// webc col_double: NULL text -> 0, non-numeric -> 0 (strtod).
function colNum(v: unknown): number {
  if (v === null || v === undefined) return 0;
  const n = Number(v);
  return Number.isFinite(n) ? n : 0;
}

function deltaPct(k: Kpi, cur: number, prev: number): void {
  k.deltaShow = true;
  if (prev <= 0) {
    k.delta = "no prior-period data";
    k.deltaTone = 2;
    return;
  }
  k.delta = `${(((cur - prev) * 100) / prev >= 0 ? "+" : "")}${(
    ((cur - prev) * 100) /
    prev
  ).toFixed(1)}% vs prior 7 days`;
  k.deltaTone = cur >= prev ? 1 : 0;
}

function orderTone(status: string | null | undefined): string {
  if (!status) return "bg-surface1 text-subtext0";
  if (status === "DELIVERED") return "bg-green/15 text-green";
  if (status === "CANCELLED") return "bg-red/15 text-red";
  if (status === "PENDING") return "bg-yellow/15 text-yellow";
  if (status === "PACKED") return "bg-teal/15 text-teal";
  if (status === "PAID") return "bg-blue/15 text-blue";
  if (status === "SHIPPED") return "bg-mauve/15 text-mauve";
  return "bg-surface1 text-subtext0";
}

function delivLabel(status: string | null | undefined): string {
  if (status === "pending") return "Pending";
  if (status === "in_transit") return "In transit";
  if (status === "delivered") return "Delivered";
  return status ? status : "-";
}

function delivTone(status: string | null | undefined): string {
  if (status === "delivered") return "bg-green/15 text-green";
  if (status === "in_transit") return "bg-blue/15 text-blue";
  if (status === "pending") return "bg-yellow/15 text-yellow";
  return "bg-surface1 text-subtext0";
}

// ---- queries (dashboard.c: q_days / q_counts / q_recent / ...) ----------

// node:sqlite labels unaliased expressions with the whole expression text
// (webc reads these rows positionally), so every computed column carries an
// alias matching the loader's field names.
const Q_DAYS =
  "SELECT substr(created_at, 1, 10) AS d, " +
  "COALESCE(SUM(total_amount), 0) AS sales, COUNT(*) AS n, " +
  "COALESCE(SUM(CASE WHEN staff_id = ? THEN total_amount ELSE 0 END), 0) AS mine_sales, " +
  "SUM(CASE WHEN staff_id = ? THEN 1 ELSE 0 END) AS mine_n " +
  "FROM orders " +
  "WHERE deleted_at IS NULL AND substr(created_at, 1, 10) >= ? " +
  "GROUP BY 1 ORDER BY 1;";

const Q_COUNTS =
  "SELECT " +
  "(SELECT COUNT(*) FROM inventory_stocks " +
  " WHERE deleted_at IS NULL AND quantity <= min_threshold) AS low, " +
  "(SELECT COUNT(*) FROM inventory_stocks " +
  " WHERE deleted_at IS NULL AND quantity <= 0) AS out0, " +
  "(SELECT COUNT(*) FROM cash " +
  " WHERE deleted_at IS NULL AND closed_at IS NULL) AS open, " +
  "(SELECT COUNT(*) FROM cash " +
  " WHERE deleted_at IS NULL AND closed_at IS NULL " +
  " AND substr(opened_at, 1, 10) < ?) AS stale, " +
  "(SELECT COUNT(*) FROM orders " +
  " WHERE deleted_at IS NULL AND order_status_dict_id = 'PENDING') AS pending;";

const Q_RECENT_TAIL =
  "FROM orders o " +
  "LEFT JOIN dictionaries d ON d.id = o.order_status_dict_id " +
  "AND d.deleted_at IS NULL " +
  "LEFT JOIN staff s ON s.id = o.staff_id AND s.deleted_at IS NULL " +
  "WHERE o.deleted_at IS NULL ";

const Q_RECENT_HEAD =
  "SELECT o.order_number AS number, o.created_at AS created, " +
  "o.total_amount AS total, " +
  "o.order_status_dict_id AS sid, COALESCE(d.label, '') AS label, " +
  "COALESCE((SELECT u.name FROM users u WHERE u.id = s.user_id), '') AS who ";

const Q_RECENT = Q_RECENT_HEAD + Q_RECENT_TAIL + "ORDER BY o.created_at DESC LIMIT 8;";
const Q_RECENT_MINE =
  Q_RECENT_HEAD +
  Q_RECENT_TAIL +
  "AND o.staff_id = ? ORDER BY o.created_at DESC LIMIT 8;";

const Q_SHIFT =
  "SELECT s.opened_at AS opened, s.opening_cash AS cash, " +
  "COALESCE(o.ou_name, '') AS org " +
  "FROM cash s " +
  "LEFT JOIN org_units o ON o.id = s.org_unit_id AND o.deleted_at IS NULL " +
  "WHERE s.staff_id = ? AND s.closed_at IS NULL AND s.deleted_at IS NULL " +
  "ORDER BY s.opened_at DESC LIMIT 1;";

const Q_OPEN_SHIFTS =
  "SELECT COUNT(*) AS open FROM cash " +
  "WHERE deleted_at IS NULL AND closed_at IS NULL;";

const Q_DEL_COUNTS =
  "SELECT delivery_status AS s, COUNT(*) AS n FROM deliveries " +
  "WHERE driver_staff_id = ? AND deleted_at IS NULL " +
  "AND delivery_status <> 'cancelled' GROUP BY delivery_status;";

const Q_DEL_LIST =
  "SELECT d.delivery_status AS s, d.recipient_name AS recipient, " +
  "d.recipient_phone AS phone, " +
  "d.delivery_address AS address, COALESCE(o.order_number, '') AS number " +
  "FROM deliveries d " +
  "LEFT JOIN orders o ON o.id = d.order_id " +
  "WHERE d.driver_staff_id = ? AND d.deleted_at IS NULL " +
  "AND d.delivery_status <> 'cancelled' " +
  "ORDER BY CASE d.delivery_status " +
  "WHEN 'pending' THEN 0 WHEN 'in_transit' THEN 1 ELSE 2 END, " +
  "COALESCE(d.dispatched_at, '') DESC LIMIT 12;";

// ---- loader -------------------------------------------------------------

function dashboardLoad(au: AuthUser | null): Ws {
  const ws: Ws = {
    kind: 3,
    greeting: "",
    dateHuman: "",
    workspace: "Workspace",
    roleCls: "bg-surface1 text-subtext0 border-surface0",
    roleLabel: "No role",
    kpis: [],
    attention: "All clear - nothing needs attention",
    attentionHref: "",
    attentionCls: "border-green bg-green/10 text-green",
    bars: Array.from({ length: 7 }, () => ({ value: "", pct: 0, day: "" })),
    barsSum: "",
    hasShift: false,
    shiftSince: "",
    shiftCash: "",
    shiftOrg: "",
    recent: [],
    delPending: 0,
    delTransit: 0,
    delDone: 0,
    delivs: [],
    outOfStock: 0,
  };

  const now = Date.now();
  const today = isoDay(now); // UTC day keys - matches SQL substr(created_at)
  // Header date is what the staff reads on the wall: local day.
  ws.dateHuman = new Date(now).toLocaleDateString("en-GB", {
    weekday: "short",
    day: "2-digit",
    month: "short",
    year: "numeric",
  });
  ws.greeting = `Good day, ${au && au.name ? au.name : "staff"}`;
  if (au && au.role) ws.roleLabel = au.role;

  // Workspace dispatch: role_code decides the center. No role -> generic
  // welcome and no queries (webc kind 3).
  const rc = au && au.role_code ? au.role_code : "";
  if (rc === "SD-MANAGER") {
    ws.kind = 0;
    ws.roleCls = "bg-lavender/15 text-lavender border-lavender/40";
    ws.workspace = "Manager workspace";
  } else if (rc === "SD-CASHIER") {
    ws.kind = 1;
    ws.roleCls = "bg-blue/15 text-blue border-blue/40";
    ws.workspace = "Counter workspace";
  } else if (rc === "SD-DRIVER") {
    ws.kind = 2;
    ws.roleCls = "bg-peach/15 text-peach border-peach/40";
    ws.workspace = "Delivery workspace";
  } else {
    ws.kind = 3;
    return ws;
  }

  const staffId = au && au.staff_id ? au.staff_id : "";

  let low = 0;
  let out0 = 0;
  let stale = 0;
  let pending = 0;
  if (ws.kind !== 2) {
    const r = queryOne<{
      low: number;
      out0: number;
      open: number;
      stale: number;
      pending: number;
    }>(Q_COUNTS, today);
    if (r) {
      low = Number(r.low ?? 0);
      out0 = Number(r.out0 ?? 0);
      stale = Number(r.stale ?? 0);
      pending = Number(r.pending ?? 0);
    }
    ws.outOfStock = out0;
  }

  // ---- driver: assigned deliveries (before the KPI strip reads them) ---
  if (ws.kind === 2 && staffId) {
    for (const r of queryAll<{ s: string; n: number }>(Q_DEL_COUNTS, staffId)) {
      const n = Number(r.n ?? 0);
      if (r.s === "pending") ws.delPending = n;
      else if (r.s === "in_transit") ws.delTransit = n;
      else if (r.s === "delivered") ws.delDone = n;
    }
    for (const r of queryAll<{
      s: string | null;
      recipient: string | null;
      phone: string | null;
      address: string | null;
      number: string | null;
    }>(Q_DEL_LIST, staffId)) {
      if (ws.delivs.length >= 12) break;
      ws.delivs.push({
        status: delivLabel(r.s),
        toneCls: delivTone(r.s),
        recipient: r.recipient ? r.recipient : "-",
        phone: r.phone ? r.phone : "-",
        note: r.address ? r.address : "-",
        number: r.number ? r.number : "-",
      });
    }
  }

  // ---- 14-day rollup (manager + counter) -------------------------------
  const drows: DayRow[] = [];
  let s7 = 0;
  let p7 = 0;
  let sToday = 0;
  let mine7 = 0;
  let n7 = 0;
  let pn7 = 0;
  let nToday = 0;
  let mn7 = 0;

  if (ws.kind === 0 || ws.kind === 1) {
    const w14 = isoDay(now - 13 * DAY_MS);
    const staffVal = staffId ? staffId : null;
    for (const r of queryAll<{
      d: string;
      sales: number;
      n: number;
      mine_sales: number;
      mine_n: number;
    }>(Q_DAYS, staffVal, staffVal, w14)) {
      if (drows.length >= 16) break;
      drows.push({
        d: r.d ? String(r.d) : "",
        sales: colNum(r.sales),
        n: Number(r.n ?? 0),
        mineSales: colNum(r.mine_sales),
        mineN: Number(r.mine_n ?? 0),
      });
    }

    for (let i = 0; i < 14; ++i) {
      const d = isoDay(now - i * DAY_MS);
      const r = drows.find((row) => row.d === d);
      if (!r) continue;
      if (i <= 6) {
        s7 += r.sales;
        n7 += r.n;
        mine7 += r.mineSales;
        mn7 += r.mineN;
      } else {
        p7 += r.sales;
        pn7 += r.n;
      }
      if (i === 0) {
        sToday = r.sales;
        nToday = r.n;
      }
    }
  }

  // ---- KPI strip --------------------------------------------------------
  const addKpi = (label: string, value: string): Kpi => {
    const k: Kpi = { label, value, delta: "", deltaShow: false, deltaTone: 2 };
    ws.kpis.push(k);
    return k;
  };
  if (ws.kind === 0) {
    let k = addKpi("Net sales (7d)", money(s7));
    deltaPct(k, s7, p7);

    k = addKpi("Transactions (7d)", String(n7));
    deltaPct(k, n7, pn7);

    k = addKpi("Avg basket (7d)", money(n7 > 0 ? s7 / n7 : 0));
    deltaPct(k, n7 > 0 ? s7 / n7 : 0, pn7 > 0 ? p7 / pn7 : 0);

    k = addKpi("Sales today", money(sToday));
    k.deltaShow = true;
    k.deltaTone = 2;
    k.delta = `${nToday} orders`;

    k = addKpi("Low stock", String(low));
    k.deltaShow = true;
    k.deltaTone = 2;
    k.delta = "at or below minimum";

    k = addKpi("Open shifts", "");
    {
      const r = queryOne<{ open: number }>(Q_OPEN_SHIFTS);
      k.value = String(r ? Number(r.open ?? 0) : 0);
    }
    k.deltaShow = true;
    k.deltaTone = 2;
    k.delta = stale > 0 ? `${stale} open since an earlier day` : "all opened today";
  } else if (ws.kind === 1) {
    let k = addKpi("My sales (7d)", money(mine7));
    k.deltaShow = true;
    k.deltaTone = 2;
    k.delta = `${mn7} orders`;

    k = addKpi("My avg basket (7d)", money(mn7 > 0 ? mine7 / mn7 : 0));

    k = addKpi("Team sales today", money(sToday));
    k.deltaShow = true;
    k.deltaTone = 2;
    k.delta = `${nToday} orders`;

    k = addKpi("Out of stock", String(out0));
    k.deltaShow = true;
    k.deltaTone = 2;
    k.delta = "products unavailable";
  } else if (ws.kind === 2) {
    let k = addKpi("Pending", String(ws.delPending));
    k.deltaShow = true;
    k.deltaTone = 2;
    k.delta = "awaiting pickup";

    k = addKpi("In transit", String(ws.delTransit));
    k.deltaShow = true;
    k.deltaTone = 2;
    k.delta = "on the road";

    k = addKpi("Delivered", String(ws.delDone));
    k.deltaShow = true;
    k.deltaTone = 2;
    k.delta = "completed";
  }

  // ---- 7-day bars (manager) --------------------------------------------
  if (ws.kind === 0 && drows.length > 0) {
    const vals: number[] = [];
    let maxv = 0;
    for (let i = 0; i < 7; ++i) {
      const d = isoDay(now - (6 - i) * DAY_MS);
      const r = drows.find((row) => row.d === d);
      const v = r ? r.sales : 0;
      vals.push(v);
      if (v > maxv) maxv = v;
    }
    for (let i = 0; i < 7; ++i) {
      ws.bars[i].value = money(vals[i]);
      if (vals[i] <= 0) {
        ws.bars[i].pct = 0;
      } else {
        const pct = maxv > 0 ? Math.trunc((vals[i] / maxv) * BAR_H) : BAR_H;
        ws.bars[i].pct = pct < 2 ? 2 : pct;
      }
    }
    for (let i = 0; i < 7; ++i) {
      const d = isoDay(now - (6 - i) * DAY_MS);
      ws.bars[i].day = `${d.slice(8, 10)}/${d.slice(5, 7)}`;
    }
    ws.barsSum = money(s7);
  }

  // ---- attention line (priority: stock risk > stale shift > queue) ------
  if (ws.kind === 0) {
    if (low > 0) {
      ws.attentionCls = "border-red bg-red/10 text-red";
      ws.attentionHref = "/pos/stocks";
      ws.attention = `${low} products at or below minimum stock`;
    } else if (stale > 0) {
      ws.attentionCls = "border-yellow bg-yellow/10 text-yellow";
      ws.attentionHref = "/pos/shifts";
      ws.attention = `${stale} shift${stale === 1 ? "" : "s"} open since an earlier day`;
    } else if (pending > 0) {
      ws.attentionCls = "border-yellow bg-yellow/10 text-yellow";
      ws.attentionHref = "/pos/orders";
      ws.attention = `${pending} orders pending - start fulfilling`;
    }
  } else if (ws.kind === 1) {
    if (out0 > 0) {
      ws.attentionCls = "border-yellow bg-yellow/10 text-yellow";
      ws.attention = `${out0} products out of stock`;
    } else if (pending > 0) {
      ws.attentionCls = "border-yellow bg-yellow/10 text-yellow";
      ws.attentionHref = "/pos/orders";
      ws.attention = `${pending} orders pending in the queue`;
    }
  } else if (ws.kind === 2 && ws.delPending > 0) {
    ws.attentionCls = "border-yellow bg-yellow/10 text-yellow";
    ws.attention = `${ws.delPending} deliveries awaiting pickup`;
  }

  // ---- recent orders (manager: store, counter: mine) -------------------
  if (ws.kind === 0 || ws.kind === 1) {
    const mine = ws.kind === 1;
    const rows = mine
      ? queryAll<{
          number: string | null;
          created: string | null;
          total: number;
          sid: string | null;
          label: string | null;
          who: string | null;
        }>(Q_RECENT_MINE, staffId ? staffId : null)
      : queryAll<{
          number: string | null;
          created: string | null;
          total: number;
          sid: string | null;
          label: string | null;
          who: string | null;
        }>(Q_RECENT);
    for (const r of rows) {
      if (ws.recent.length >= 8) break;
      ws.recent.push({
        number: r.number ? r.number : "-",
        time: localHm(r.created),
        total: money(colNum(r.total)),
        status: r.label ? r.label : r.sid ? r.sid : "-",
        toneCls: orderTone(r.sid),
        who: r.who ? r.who : "-",
      });
    }
  }

  // ---- counter: my open shift ------------------------------------------
  if (ws.kind === 1 && staffId) {
    const r = queryOne<{
      opened: string | null;
      cash: number;
      org: string | null;
    }>(Q_SHIFT, staffId);
    if (r) {
      ws.hasShift = true;
      ws.shiftSince = localHm(r.opened);
      ws.shiftCash = money(colNum(r.cash));
      ws.shiftOrg = r.org ? r.org : "-";
    }
  }

  return ws;
}

export async function generateMetadata() {
  return { title: "Dashboard" };
}

export default async function DashboardPage() {
  const me = await getSessionUser();
  const ws = dashboardLoad(me);
  const { tr } = await getI18n();

  const kpiGridCls =
    ws.kpis.length >= 6
      ? "md:grid-cols-3 xl:grid-cols-6"
      : ws.kpis.length === 4
        ? "md:grid-cols-4"
        : "md:grid-cols-3";

  return (
    <AdminChrome title="Dashboard">
      <div className="px-3 py-3 flex flex-col gap-3">
        {/* workspace header: greeting + date + role chip + quick actions */}
        <div className="flex flex-wrap items-end justify-between gap-2">
          <div>
            <h2 className="text-xl font-bold text-text">{ws.greeting}</h2>
            <p className="text-xs text-subtext0 mt-0.5">
              {ws.dateHuman} · {ws.workspace}
            </p>
          </div>
          <div className="flex items-center gap-2 flex-wrap">
            <span
              className={`px-2 py-1.5 text-xs font-semibold border ${ws.roleCls}`}
            >
              {ws.roleLabel}
            </span>
            {ws.kind === 0 && (
              <>
                <a
                  href="/pos/sale"
                  className="px-2.5 py-1.5 text-xs font-medium bg-blue text-onbase hover:brightness-90"
                >
                  New sale
                </a>
                <a
                  href="/reports"
                  className="px-2.5 py-1.5 text-xs font-medium bg-surface1 text-text border border-surface0 hover:bg-surface0"
                >
                  Reports
                </a>
                <a
                  href="/pos/alerts"
                  className="px-2.5 py-1.5 text-xs font-medium bg-surface1 text-text border border-surface0 hover:bg-surface0"
                >
                  Alerts
                </a>
              </>
            )}
            {ws.kind === 1 && (
              <>
                <a
                  href="/pos/sale"
                  className="px-2.5 py-1.5 text-xs font-medium bg-blue text-onbase hover:brightness-90"
                >
                  New sale
                </a>
                <a
                  href="/pos/shifts"
                  className="px-2.5 py-1.5 text-xs font-medium bg-surface1 text-text border border-surface0 hover:bg-surface0"
                >
                  Shifts
                </a>
                <a
                  href="/reports"
                  className="px-2.5 py-1.5 text-xs font-medium bg-surface1 text-text border border-surface0 hover:bg-surface0"
                >
                  Reports
                </a>
              </>
            )}
            <Link
              href="/"
              className="px-2.5 py-1.5 text-xs font-medium bg-surface1 text-text border border-surface0 hover:bg-surface0"
            >
              Shopping
            </Link>
            {ws.kind === 3 && (
              <span className="text-xs text-subtext0">
                No workspace assigned - ask a manager for a role.
              </span>
            )}
          </div>
        </div>

        {/* KPI strip */}
        {ws.kpis.length > 0 && (
          <div className={`grid grid-cols-2 gap-2 ${kpiGridCls}`}>
            {ws.kpis.map((k, ki) => (
              <div
                key={ki}
                className="border border-surface0 bg-mantle px-2.5 py-2 flex flex-col gap-0.5"
              >
                <span className="text-xs text-overlay0 uppercase tracking-wide">
                  {k.label}
                </span>
                <span className="text-xl font-bold text-text">{k.value}</span>
                {k.deltaShow && (
                  <span
                    className={`text-xs ${
                      k.deltaTone === 1
                        ? "text-green"
                        : k.deltaTone === 0
                          ? "text-red"
                          : "text-subtext0"
                    }`}
                  >
                    {k.delta}
                  </span>
                )}
              </div>
            ))}
          </div>
        )}

        {/* attention line: one priority signal, straight to the fixing page */}
        {ws.kind !== 3 && (
          <>
            {ws.attentionHref ? (
              <a
                href={ws.attentionHref}
                className={`flex items-center justify-between gap-2 border-l-2 px-2.5 py-2 text-sm font-medium hover:brightness-95 ${ws.attentionCls}`}
              >
                <span>{ws.attention}</span>
                <span className="text-xs opacity-70">Open page</span>
              </a>
            ) : (
              <div
                className={`flex items-center gap-2 border-l-2 px-2.5 py-2 text-sm font-medium ${ws.attentionCls}`}
              >
                <span>{ws.attention}</span>
              </div>
            )}
          </>
        )}

        {/* manager: 7-day bars + recent orders | counter: my shift + my orders */}
        {(ws.kind === 0 || ws.kind === 1) && (
          <div className="grid grid-cols-1 xl:grid-cols-3 gap-2">
            {ws.kind === 0 && (
              <div className="border border-surface0 bg-mantle px-2.5 py-2 flex flex-col gap-2">
                <div className="flex items-center justify-between">
                  <span className="text-xs text-overlay0 uppercase tracking-wide">
                    Net sales - last 7 days
                  </span>
                  <span className="text-xs font-semibold text-text">
                    {ws.barsSum}
                  </span>
                </div>
                <div className="flex items-end gap-1.5" style={{ height: 72 }}>
                  {ws.bars.map((b, bi) => (
                    <div
                      key={bi}
                      className="flex-1 bg-blue"
                      style={{ height: `${b.pct}px` }}
                      title={b.value}
                    />
                  ))}
                </div>
                <div className="flex gap-1.5">
                  {ws.bars.map((b, bi) => (
                    <span
                      key={bi}
                      className="flex-1 text-center text-xs text-overlay0"
                    >
                      {b.day}
                    </span>
                  ))}
                </div>
              </div>
            )}
            {ws.kind === 1 && (
              <div className="border border-surface0 bg-mantle px-2.5 py-2 flex flex-col gap-2">
                <span className="text-xs text-overlay0 uppercase tracking-wide">
                  My shift
                </span>
                {ws.hasShift ? (
                  <>
                    <div className="border-l-2 border-green bg-green/10 px-2 py-1.5 flex flex-col gap-0.5">
                      <span className="text-sm font-semibold text-green">
                        Open since {ws.shiftSince}
                      </span>
                      <span className="text-xs text-subtext0">
                        Opening cash {ws.shiftCash} · {ws.shiftOrg}
                      </span>
                    </div>
                    <a
                      href="/pos/shifts"
                      className="px-2.5 py-1.5 text-xs font-medium bg-surface1 text-text border border-surface0 hover:bg-surface0 text-center"
                    >
                      Manage shift
                    </a>
                  </>
                ) : (
                  <>
                    <div className="border-l-2 border-yellow bg-yellow/10 px-2 py-1.5 flex flex-col gap-0.5">
                      <span className="text-sm font-semibold text-yellow">
                        No open shift
                      </span>
                      <span className="text-xs text-subtext0">
                        Open one before the first sale.
                      </span>
                    </div>
                    <a
                      href="/pos/shifts"
                      className="px-2.5 py-1.5 text-xs font-medium bg-blue text-onbase hover:brightness-90 text-center"
                    >
                      Open shift
                    </a>
                  </>
                )}
              </div>
            )}
            <div className="border border-surface0 bg-mantle px-2.5 py-2 flex flex-col gap-1.5 xl:col-span-2">
              <div className="flex items-center justify-between">
                <span className="text-xs text-overlay0 uppercase tracking-wide">
                  {ws.kind === 0 ? "Recent orders" : "My recent orders"}
                </span>
                <a href="/pos/orders" className="text-xs text-blue hover:underline">
                  View all
                </a>
              </div>
              {ws.recent.length > 0 ? (
                <table className="w-full text-sm">
                  <thead>
                    <tr className="text-left text-xs text-overlay0 border-b border-surface0">
                      <th className="py-1 pr-2 font-medium">
                        {tr("Order", "Order")}
                      </th>
                      <th className="py-1 pr-2 font-medium">
                        {tr("Time", "Time")}
                      </th>
                      <th className="py-1 pr-2 font-medium">
                        {tr("Cashier", "Cashier")}
                      </th>
                      <th className="py-1 pr-2 font-medium text-right">
                        {tr("Total", "Total")}
                      </th>
                      <th className="py-1 font-medium">
                        {tr("Status", "Status")}
                      </th>
                    </tr>
                  </thead>
                  <tbody>
                    {ws.recent.map((o, ri) => (
                      <tr key={ri} className="border-b border-surface0/50" data-order-row>
                        <td className="py-1 pr-2 text-text font-medium">
                          {o.number}
                        </td>
                        <td className="py-1 pr-2 text-subtext0">{o.time}</td>
                        <td className="py-1 pr-2 text-subtext0">{o.who}</td>
                        <td className="py-1 pr-2 text-right text-text">
                          {o.total}
                        </td>
                        <td className="py-1">
                          <span
                            className={`px-1.5 py-0.5 text-xs font-medium ${o.toneCls}`}
                          >
                            {o.status}
                          </span>
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              ) : (
                <span className="text-xs text-subtext0">No orders yet.</span>
              )}
            </div>
          </div>
        )}

        {/* driver: the assigned-deliveries run */}
        {ws.kind === 2 && (
          <div className="border border-surface0 bg-mantle px-2.5 py-2 flex flex-col gap-1.5">
            <div className="flex items-center justify-between flex-wrap gap-2">
              <span className="text-xs text-overlay0 uppercase tracking-wide">
                Assigned deliveries
              </span>
              <div className="flex items-center gap-1.5 text-xs">
                <span className="px-1.5 py-0.5 font-medium bg-yellow/15 text-yellow">
                  {ws.delPending} pending
                </span>
                <span className="px-1.5 py-0.5 font-medium bg-blue/15 text-blue">
                  {ws.delTransit} in transit
                </span>
                <span className="px-1.5 py-0.5 font-medium bg-green/15 text-green">
                  {ws.delDone} delivered
                </span>
              </div>
            </div>
            {ws.delivs.length > 0 ? (
              <table className="w-full text-sm">
                <thead>
                  <tr className="text-left text-xs text-overlay0 border-b border-surface0">
                    <th className="py-1 pr-2 font-medium">
                      {tr("Order", "Order")}
                    </th>
                    <th className="py-1 pr-2 font-medium">
                      {tr("Recipient", "Recipient")}
                    </th>
                    <th className="py-1 pr-2 font-medium">
                      {tr("Phone", "Phone")}
                    </th>
                    <th className="py-1 pr-2 font-medium">
                      {tr("Destination", "Destination")}
                    </th>
                    <th className="py-1 font-medium">
                      {tr("Status", "Status")}
                    </th>
                  </tr>
                </thead>
                <tbody>
                  {ws.delivs.map((d, di) => (
                    <tr key={di} className="border-b border-surface0/50" data-deliv-row>
                      <td className="py-1 pr-2 text-text font-medium">
                        {d.number}
                      </td>
                      <td className="py-1 pr-2 text-subtext0">{d.recipient}</td>
                      <td className="py-1 pr-2 text-subtext0">{d.phone}</td>
                      <td className="py-1 pr-2 text-subtext0">{d.note}</td>
                      <td className="py-1">
                        <span
                          className={`px-1.5 py-0.5 text-xs font-medium ${d.toneCls}`}
                        >
                          {d.status}
                        </span>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            ) : (
              <span className="text-xs text-subtext0">
                No deliveries assigned right now.
              </span>
            )}
          </div>
        )}

        {/* generic: no role yet */}
        {ws.kind === 3 && (
          <div className="border border-surface0 bg-mantle px-2.5 py-3 text-sm text-subtext0">
            This account has no workspace yet. A manager can grant one on the
            Roles page.
            <Link href="/" className="text-blue hover:underline">
              Open the storefront
            </Link>
            .
          </div>
        )}
      </div>
    </AdminChrome>
  );
}
