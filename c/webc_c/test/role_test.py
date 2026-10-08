#!/usr/bin/env python3
"""Phase 14 role workspaces: cashier + driver navigation, page gate (403)
and the role-dispatched /dashboard centers.

Spawned by test/run.sh after shop_test.py - it signs the other two demo
staff in (sd.staff2/3 get their passwords from migration 0008) - and before
the CRUD contract suite. Every other suite logs in as sd.staff1 (manager,
all grants), so least-privilege gating stays invisible to them.

Covered:
  - cashier (SD-CASHIER: SELL + DISCOUNT + REPORT.VIEW): counter dashboard
    (my shift / my numbers / my orders), sales nav kept, manager-only nav
    hidden, SELL pages 200 vs finance/users/products/stocks/alerts 403
  - driver (SD-DRIVER: no grants): delivery dashboard (assigned queue +
    run counts), sidebar down to Overview + Demo, /pos and /reports 403
  - group headers hide when every row under them is filtered out
  - logout still kills the session (303)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from testlib import Client, Checker  # noqa: E402

HOST = sys.argv[1]
PORT = int(sys.argv[2])
TMP = sys.argv[3]  # noqa: F841 - shared suite signature

c = Client(HOST, PORT)
t = Checker()

HEADER_CLS = ('class="px-2 py-1 text-xs font-semibold uppercase '
              'tracking-wide text-overlay0 bg-surface0/60 nav-label"')


def main():
    # ---- cashier (sd.staff2: SELL + DISCOUNT + REPORT.VIEW) --------------
    st, _ = c.login_as("sd.staff2", "posadmin1")
    t.chk("cashier login redirects", st, 303)

    st, body, _ = c.get("/dashboard")
    d = body.decode()
    t.chk("cashier dashboard", st, 200)
    t.chk("cashier = counter workspace", "Counter workspace" in d, True)
    t.chk("cashier KPI = my numbers", "My sales (7d)" in d, True)
    t.chk("cashier sees no manager KPI", "Net sales (7d)" not in d, True)
    t.chk("cashier shift card", "My shift" in d, True)
    t.chk("cashier owns the seeded open shift", "Open since" in d, True)
    t.chk("cashier role chip (Khmer)", "អ្នកគិតលុយ" in d, True)
    t.chk("cashier quick actions", "New sale" in d, True)
    t.chk("cashier attention line",
          ("products out of stock" in d or "pending in the queue" in d),
          "True")

    # nav: sales pages + reports + demo, nothing manager-only
    t.chk("cashier nav keeps orders", 'href="/pos/orders"' in d, True)
    t.chk("cashier nav keeps reports", 'href="/reports"' in d, True)
    t.chk("cashier nav hides finance", 'href="/pos/finance"' not in d, True)
    t.chk("cashier nav hides users", 'href="/pos/users"' not in d, True)
    t.chk("cashier nav hides stocks", 'href="/pos/stocks"' not in d, True)
    t.chk("cashier nav hides geo settings",
          all(p not in d for p in ('href="/pos/provinces"',
                                   'href="/pos/districts"',
                                   'href="/pos/communes"',
                                   'href="/pos/villages"')), True)
    t.chk("cashier nav groups (Dashboard/Sales/Party/Reports/Demo)",
          d.count(HEADER_CLS), 5)
    t.chk("cashier nav links (10 entries + logout)",
          d.count('class="nav-link'), 11)

    # page gate: SELL + report pages open, everything else 403
    t.chk("cashier /pos/orders allowed", c.get("/pos/orders")[0], 200)
    t.chk("cashier /pos/shifts allowed", c.get("/pos/shifts")[0], 200)
    t.chk("cashier /pos/customers allowed", c.get("/pos/customers")[0], 200)
    t.chk("cashier /reports allowed", c.get("/reports")[0], 200)

    st, body, _ = c.get("/pos/finance")
    t.chk("cashier /pos/finance gated", st, 403)
    t.chk("cashier 403 body", b"Access restricted" in body, True)
    t.chk("cashier 403 names required perm", b"SD.FINANCE" in body, True)
    t.chk("cashier /pos/users gated", c.get("/pos/users")[0], 403)
    t.chk("cashier /pos/products gated", c.get("/pos/products")[0], 403)
    t.chk("cashier /pos/stocks gated", c.get("/pos/stocks")[0], 403)
    t.chk("cashier /pos/alerts gated", c.get("/pos/alerts")[0], 403)
    t.chk("cashier /pos/provinces gated (SD.SETTINGS)",
          c.get("/pos/provinces")[0], 403)

    st, _, _, _ = c.req_full("POST", "/logout")
    t.chk("cashier logout", st, 303)

    # ---- driver (sd.staff3: no grants at all) ---------------------------
    st, _ = c.login_as("sd.staff3", "posadmin1")
    t.chk("driver login redirects", st, 303)

    st, body, _ = c.get("/dashboard")
    d = body.decode()
    t.chk("driver dashboard", st, 200)
    t.chk("driver = delivery workspace", "Delivery workspace" in d, True)
    t.chk("driver run KPIs", "In transit" in d, True)
    t.chk("driver delivery list", "Assigned deliveries" in d, True)
    t.chk("driver delivery rows (11 seeded, capped at 12)",
          d.count("data-deliv-row") >= 10, True)
    t.chk("driver role chip (Khmer)", "អ្នកដឹកជញ្ជូន" in d, True)
    t.chk("driver attention = pending run", "awaiting pickup" in d, True)
    t.chk("driver sees no manager/cashier content",
          ("Net sales (7d)" not in d and "My sales (7d)" not in d), True)

    t.chk("driver nav = overview + demo only",
          'href="/pos/orders"' not in d, True)
    t.chk("driver nav groups (Dashboard/Demo)", d.count(HEADER_CLS), 2)
    t.chk("driver nav links (6 entries + logout)",
          d.count('class="nav-link'), 7)

    t.chk("driver /pos/orders gated", c.get("/pos/orders")[0], 403)
    t.chk("driver /pos/shifts gated", c.get("/pos/shifts")[0], 403)
    t.chk("driver /pos/users gated", c.get("/pos/users")[0], 403)
    t.chk("driver /reports gated", c.get("/reports")[0], 403)

    st, _, _, _ = c.req_full("GET", "/logout")
    t.chk("driver logout", st, 303)

    ok = t.summary()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
