#define NOB_STRIP_PREFIX
#include "module/nob.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dashboard.h"
#include "src/db/db.h"

// Phase 14 role workspaces: the loader behind /dashboard. sqlite and mysql
// share every query text below (positional ? placeholders, portable
// substr/CASE/COALESCE); only the postgres stub needs $1..$n.
#define Q_DIA(sqm, pg) [SQL_SQLITE] = sqm, [SQL_MYSQL] = sqm, [SQL_POSTGRES] = pg

#define DAY_SECS 86400
#define BAR_H    72

// ---------------------------------------------------------------------------
// Tiny helpers (strings are formatted once, the template only STR()s them)
// ---------------------------------------------------------------------------

static void iso_day(char *out /* 11 */, time_t t) {
    struct tm tmv;
    gmtime_r(&t, &tmv);
    strftime(out, 11, "%Y-%m-%d", &tmv);
}

static void money(char *buf, size_t n, double v) {
    snprintf(buf, n, "%.2f $", v);
}

static double col_double(sql_stmt *stmt, int col) {
    const char *v = sql_col_text(stmt, col);
    return v ? strtod(v, NULL) : 0.0;
}

static Dash_Kpi *next_kpi(Dashboard_Data *ws) {
    Dash_Kpi *k = &ws->kpis[ws->kpi_count++];
    memset(k, 0, sizeof(*k));
    return k;
}

static void delta_pct(Dash_Kpi *k, double cur, double prev) {
    k->delta_show = 1;
    if (prev <= 0) {
        snprintf(k->delta, sizeof(k->delta), "no prior-period data");
        k->delta_tone = 2;
        return;
    }
    snprintf(k->delta, sizeof(k->delta), "%+.1f%% vs prior 7 days",
             (cur - prev) * 100.0 / prev);
    k->delta_tone = cur >= prev ? 1 : 0;
}

static const char *order_tone(const char *status) {
    if (!status) return "bg-surface1 text-subtext0";
    if (!strcmp(status, "DELIVERED")) return "bg-green/15 text-green";
    if (!strcmp(status, "CANCELLED")) return "bg-red/15 text-red";
    if (!strcmp(status, "PENDING"))   return "bg-yellow/15 text-yellow";
    if (!strcmp(status, "PACKED"))    return "bg-teal/15 text-teal";
    if (!strcmp(status, "PAID"))      return "bg-blue/15 text-blue";
    if (!strcmp(status, "SHIPPED"))   return "bg-mauve/15 text-mauve";
    return "bg-surface1 text-subtext0";
}

static const char *deliv_label(const char *status) {
    if (status && !strcmp(status, "pending"))    return "Pending";
    if (status && !strcmp(status, "in_transit")) return "In transit";
    if (status && !strcmp(status, "delivered"))  return "Delivered";
    return status ? status : "-";
}

static const char *deliv_tone(const char *status) {
    if (status && !strcmp(status, "delivered"))  return "bg-green/15 text-green";
    if (status && !strcmp(status, "in_transit")) return "bg-blue/15 text-blue";
    if (status && !strcmp(status, "pending"))    return "bg-yellow/15 text-yellow";
    return "bg-surface1 text-subtext0";
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

// Daily rollup for the last 14 days (7-day window vs prior 7 + today +
// mine). One GROUP BY instead of a wall of CASE aggregates; the loader
// buckets the (at most 14) rows by date string.
static const char *const q_days[SQL_LANG_COUNT] = {
    Q_DIA(
        "SELECT substr(created_at, 1, 10), "
               "COALESCE(SUM(total_amount), 0), COUNT(*), "
               "COALESCE(SUM(CASE WHEN staff_id = ? "
                    "THEN total_amount ELSE 0 END), 0), "
               "SUM(CASE WHEN staff_id = ? THEN 1 ELSE 0 END) "
        "FROM orders "
        "WHERE deleted_at IS NULL "
          "AND substr(created_at, 1, 10) >= ? "
        "GROUP BY 1 ORDER BY 1;",
        "SELECT substr(created_at, 1, 10), "
               "COALESCE(SUM(total_amount), 0), COUNT(*), "
               "COALESCE(SUM(CASE WHEN staff_id = $1 "
                    "THEN total_amount ELSE 0 END), 0), "
               "SUM(CASE WHEN staff_id = $2 THEN 1 ELSE 0 END) "
        "FROM orders "
        "WHERE deleted_at IS NULL "
          "AND substr(created_at, 1, 10) >= $3 "
        "GROUP BY 1 ORDER BY 1;"
    ),
};

// Live ops counters for the attention line: low stock, out of stock, open
// shifts, stale open shifts, pending orders. One row of scalar subqueries.
static const char *const q_counts[SQL_LANG_COUNT] = {
    Q_DIA(
        "SELECT "
        "(SELECT COUNT(*) FROM inventory_stocks "
          "WHERE deleted_at IS NULL AND quantity <= min_threshold), "
        "(SELECT COUNT(*) FROM inventory_stocks "
          "WHERE deleted_at IS NULL AND quantity <= 0), "
        "(SELECT COUNT(*) FROM cash_shifts "
          "WHERE deleted_at IS NULL AND closed_at IS NULL), "
        "(SELECT COUNT(*) FROM cash_shifts "
          "WHERE deleted_at IS NULL AND closed_at IS NULL "
           "AND substr(opened_at, 1, 10) < ?), "
        "(SELECT COUNT(*) FROM orders "
          "WHERE deleted_at IS NULL AND order_status_dict_id = 'PENDING');",
        "SELECT "
        "(SELECT COUNT(*) FROM inventory_stocks "
          "WHERE deleted_at IS NULL AND quantity <= min_threshold), "
        "(SELECT COUNT(*) FROM inventory_stocks "
          "WHERE deleted_at IS NULL AND quantity <= 0), "
        "(SELECT COUNT(*) FROM cash_shifts "
          "WHERE deleted_at IS NULL AND closed_at IS NULL), "
        "(SELECT COUNT(*) FROM cash_shifts "
          "WHERE deleted_at IS NULL AND closed_at IS NULL "
           "AND substr(opened_at, 1, 10) < $1), "
        "(SELECT COUNT(*) FROM orders "
          "WHERE deleted_at IS NULL AND order_status_dict_id = 'PENDING');"
    ),
};

// Recent orders (store-wide; the counter variant filters to my staff row).
// Status rides the dictionary label, the raw dict id picks the tone.
#define Q_RECENT(WHERE) \
    "SELECT o.order_number, o.created_at, o.total_amount, " \
           "o.order_status_dict_id, COALESCE(d.label, ''), " \
           "COALESCE(s.first_name, ''), COALESCE(s.last_name, '') " \
    "FROM orders o " \
    "LEFT JOIN dictionaries d ON d.id = o.order_status_dict_id " \
                            "AND d.deleted_at IS NULL " \
    "LEFT JOIN staff s ON s.id = o.staff_id AND s.deleted_at IS NULL " \
    "WHERE o.deleted_at IS NULL " WHERE \
    "ORDER BY o.created_at DESC LIMIT 8;"

static const char *const q_recent[SQL_LANG_COUNT] = {
    Q_DIA(Q_RECENT(""), Q_RECENT("")),
};

static const char *const q_recent_mine[SQL_LANG_COUNT] = {
    Q_DIA(Q_RECENT("AND o.staff_id = ? "),
          Q_RECENT("AND o.staff_id = $1 ")),
};

// My open shift (counter workspace): the seeded cashier owns one.
static const char *const q_shift[SQL_LANG_COUNT] = {
    Q_DIA(
        "SELECT s.opened_at, s.opening_cash, COALESCE(o.ou_name, '') "
        "FROM cash_shifts s "
        "LEFT JOIN org_units o ON o.id = s.org_unit_id "
                             "AND o.deleted_at IS NULL "
        "WHERE s.staff_id = ? AND s.closed_at IS NULL "
          "AND s.deleted_at IS NULL "
        "ORDER BY s.opened_at DESC LIMIT 1;",
        "SELECT s.opened_at, s.opening_cash, COALESCE(o.ou_name, '') "
        "FROM cash_shifts s "
        "LEFT JOIN org_units o ON o.id = s.org_unit_id "
                             "AND o.deleted_at IS NULL "
        "WHERE s.staff_id = $1 AND s.closed_at IS NULL "
          "AND s.deleted_at IS NULL "
        "ORDER BY s.opened_at DESC LIMIT 1;"
    ),
};

static const char *const q_open_shifts[SQL_LANG_COUNT] = {
    Q_DIA(
        "SELECT COUNT(*) FROM cash_shifts "
        "WHERE deleted_at IS NULL AND closed_at IS NULL;",
        "SELECT COUNT(*) FROM cash_shifts "
        "WHERE deleted_at IS NULL AND closed_at IS NULL;"
    ),
};

// Driver run: status counts + the assignment queue (pending first).
static const char *const q_del_counts[SQL_LANG_COUNT] = {
    Q_DIA(
        "SELECT delivery_status, COUNT(*) FROM deliveries "
        "WHERE driver_staff_id = ? AND deleted_at IS NULL "
          "AND delivery_status <> 'cancelled' "
        "GROUP BY delivery_status;",
        "SELECT delivery_status, COUNT(*) FROM deliveries "
        "WHERE driver_staff_id = $1 AND deleted_at IS NULL "
          "AND delivery_status <> 'cancelled' "
        "GROUP BY delivery_status;"
    ),
};

static const char *const q_del_list[SQL_LANG_COUNT] = {
    Q_DIA(
        "SELECT d.delivery_status, d.recipient_name, d.recipient_phone, "
               "d.delivery_address, COALESCE(o.order_number, '') "
        "FROM deliveries d "
        "LEFT JOIN orders o ON o.id = d.order_id "
        "WHERE d.driver_staff_id = ? AND d.deleted_at IS NULL "
          "AND d.delivery_status <> 'cancelled' "
        "ORDER BY CASE d.delivery_status "
                 "WHEN 'pending' THEN 0 WHEN 'in_transit' THEN 1 ELSE 2 END, "
                 "COALESCE(d.dispatched_at, '') DESC "
        "LIMIT 12;",
        "SELECT d.delivery_status, d.recipient_name, d.recipient_phone, "
               "d.delivery_address, COALESCE(o.order_number, '') "
        "FROM deliveries d "
        "LEFT JOIN orders o ON o.id = d.order_id "
        "WHERE d.driver_staff_id = $1 AND d.deleted_at IS NULL "
          "AND d.delivery_status <> 'cancelled' "
        "ORDER BY CASE d.delivery_status "
                 "WHEN 'pending' THEN 0 WHEN 'in_transit' THEN 1 ELSE 2 END, "
                 "COALESCE(d.dispatched_at, '') DESC "
        "LIMIT 12;"
    ),
};

// ---------------------------------------------------------------------------
// Loader
// ---------------------------------------------------------------------------

void dashboard_load(Dashboard_Data *ws, const Auth_User *au) {
    memset(ws, 0, sizeof(*ws));
    ws->role_cls = "bg-surface1 text-subtext0 border-surface0";
    ws->attention_href = "";
    ws->attention_cls = "border-green bg-green/10 text-green";
    snprintf(ws->attention, sizeof(ws->attention),
             "All clear - nothing needs attention");

    time_t now = time(NULL);
    char today[11];
    iso_day(today, now);
    struct tm tmv;
    gmtime_r(&now, &tmv);
    strftime(ws->date_human, sizeof(ws->date_human), "%a %d %b %Y", &tmv);
    snprintf(ws->greeting, sizeof(ws->greeting), "Good day, %s",
             (au && au->name && au->name[0]) ? au->name : "staff");
    snprintf(ws->role_label, sizeof(ws->role_label), "%s",
             (au && au->role && au->role[0]) ? au->role : "No role");
    snprintf(ws->workspace, sizeof(ws->workspace), "Workspace");

    // Workspace dispatch: role_code decides the center. No role (or no
    // session - only reachable for a soft-deleted account, the auth gate
    // already blocks guests) gets the generic welcome and no queries.
    const char *rc = (au && au->role_code) ? au->role_code : "";
    if (!strcmp(rc, "SD-MANAGER")) {
        ws->kind = 0;
        ws->role_cls = "bg-lavender/15 text-lavender border-lavender/40";
        snprintf(ws->workspace, sizeof(ws->workspace), "Manager workspace");
    } else if (!strcmp(rc, "SD-CASHIER")) {
        ws->kind = 1;
        ws->role_cls = "bg-blue/15 text-blue border-blue/40";
        snprintf(ws->workspace, sizeof(ws->workspace), "Counter workspace");
    } else if (!strcmp(rc, "SD-DRIVER")) {
        ws->kind = 2;
        ws->role_cls = "bg-peach/15 text-peach border-peach/40";
        snprintf(ws->workspace, sizeof(ws->workspace), "Delivery workspace");
    } else {
        ws->kind = 3;
        return;
    }

    db_t *db = open_webc_db();
    if (!db) return;  // struct stays valid: empty but well-formed center

    long low = 0, out0 = 0, stale = 0, pending = 0;
    if (ws->kind != 2) {
        sql_stmt stmt = {0};
        if (sql_prepare(db, q_counts[db->lang], &stmt)
            && sql_bind(&stmt, 1, SQL_SV(sv_from_cstr(today)))) {
            if (sql_step(&stmt) == SQL_ROW) {
                low    = sql_col_int64(&stmt, 0);
                out0   = sql_col_int64(&stmt, 1);
                stale  = sql_col_int64(&stmt, 3);
                pending = sql_col_int64(&stmt, 4);
            }
        }
        sql_finalize(&stmt);
        ws->out_of_stock = (int) out0;
    }

    // ---- driver: assigned deliveries (before the KPI strip reads them) ---
    if (ws->kind == 2 && au->staff_id && au->staff_id[0]) {
        sql_val staff_val = SQL_SV(sv_from_cstr(au->staff_id));
        sql_stmt stmt = {0};
        if (sql_prepare(db, q_del_counts[db->lang], &stmt)
            && sql_bind(&stmt, 1, staff_val)) {
            while (sql_step(&stmt) == SQL_ROW) {
                const char *s = sql_col_text(&stmt, 0);
                long n = sql_col_int64(&stmt, 1);
                if (s && !strcmp(s, "pending"))    ws->del_pending = (int) n;
                else if (s && !strcmp(s, "in_transit")) ws->del_transit = (int) n;
                else if (s && !strcmp(s, "delivered"))  ws->del_done = (int) n;
            }
        }
        sql_finalize(&stmt);

        if (sql_prepare(db, q_del_list[db->lang], &stmt)
            && sql_bind(&stmt, 1, staff_val)) {
            while (sql_step(&stmt) == SQL_ROW && ws->deliv_count < 12) {
                Dash_Deliv *d = &ws->delivs[ws->deliv_count++];
                const char *s = sql_col_text(&stmt, 0);
                const char *v;
                snprintf(d->status, sizeof(d->status), "%s", deliv_label(s));
                d->tone_cls = deliv_tone(s);
                v = sql_col_text(&stmt, 1);
                snprintf(d->recipient, sizeof(d->recipient), "%s",
                         (v && v[0]) ? v : "-");
                v = sql_col_text(&stmt, 2);
                snprintf(d->phone, sizeof(d->phone), "%s",
                         (v && v[0]) ? v : "-");
                v = sql_col_text(&stmt, 3);
                snprintf(d->note, sizeof(d->note), "%s",
                         (v && v[0]) ? v : "-");
                v = sql_col_text(&stmt, 4);
                snprintf(d->number, sizeof(d->number), "%s",
                         (v && v[0]) ? v : "-");
            }
        }
        sql_finalize(&stmt);
    }

    // ---- 14-day rollup (manager + counter) -------------------------------
    struct Day_Row { char d[11]; double sales, mine_sales; long n, mine_n; };
    struct Day_Row drows[16];
    int ndays = 0;
    double s7 = 0, p7 = 0, s_today = 0, mine7 = 0;
    long n7 = 0, pn7 = 0, n_today = 0, mn7 = 0;

    if (ws->kind == 0 || ws->kind == 1) {
        char w14[11];
        iso_day(w14, now - 13 * DAY_SECS);
        sql_val staff_val = (au->staff_id && au->staff_id[0])
            ? SQL_SV(sv_from_cstr(au->staff_id)) : SQL_NIL();
        sql_stmt stmt = {0};
        if (sql_prepare(db, q_days[db->lang], &stmt)
            && sql_bind(&stmt, 1, staff_val)
            && sql_bind(&stmt, 2, staff_val)
            && sql_bind(&stmt, 3, SQL_SV(sv_from_cstr(w14)))) {
            while (sql_step(&stmt) == SQL_ROW && ndays < 16) {
                struct Day_Row *r = &drows[ndays];
                const char *d = sql_col_text(&stmt, 0);
                snprintf(r->d, sizeof(r->d), "%s", d ? d : "");
                r->sales     = col_double(&stmt, 1);
                r->n         = sql_col_int64(&stmt, 2);
                r->mine_sales = col_double(&stmt, 3);
                r->mine_n    = sql_col_int64(&stmt, 4);
                ndays++;
            }
        }
        sql_finalize(&stmt);

        for (int i = 0; i < 14; ++i) {
            char d[11];
            iso_day(d, now - (time_t) i * DAY_SECS);
            const struct Day_Row *r = NULL;
            for (int j = 0; j < ndays; ++j)
                if (!strcmp(drows[j].d, d)) { r = &drows[j]; break; }
            if (!r) continue;
            if (i <= 6) {
                s7 += r->sales; n7 += r->n;
                mine7 += r->mine_sales; mn7 += r->mine_n;
            } else {
                p7 += r->sales; pn7 += r->n;
            }
            if (i == 0) { s_today = r->sales; n_today = r->n; }
        }
    }

    // ---- KPI strip --------------------------------------------------------
    if (ws->kind == 0) {
        Dash_Kpi *k = next_kpi(ws);
        k->label = "Net sales (7d)";
        money(k->value, sizeof(k->value), s7);
        delta_pct(k, s7, p7);

        k = next_kpi(ws);
        k->label = "Transactions (7d)";
        snprintf(k->value, sizeof(k->value), "%ld", n7);
        delta_pct(k, (double) n7, (double) pn7);

        k = next_kpi(ws);
        k->label = "Avg basket (7d)";
        money(k->value, sizeof(k->value), n7 > 0 ? s7 / n7 : 0);
        delta_pct(k, n7 > 0 ? s7 / n7 : 0, pn7 > 0 ? p7 / pn7 : 0);

        k = next_kpi(ws);
        k->label = "Sales today";
        money(k->value, sizeof(k->value), s_today);
        k->delta_show = 1;
        k->delta_tone = 2;
        snprintf(k->delta, sizeof(k->delta), "%ld orders", n_today);

        k = next_kpi(ws);
        k->label = "Low stock";
        snprintf(k->value, sizeof(k->value), "%ld", low);
        k->delta_show = 1;
        k->delta_tone = 2;
        snprintf(k->delta, sizeof(k->delta), "at or below minimum");

        k = next_kpi(ws);
        k->label = "Open shifts";
        {
            sql_stmt stmt = {0};
            long open = 0;
            if (sql_prepare(db, q_open_shifts[db->lang], &stmt)
                && sql_step(&stmt) == SQL_ROW) {
                open = sql_col_int64(&stmt, 0);
            }
            sql_finalize(&stmt);
            snprintf(k->value, sizeof(k->value), "%ld", open);
        }
        k->delta_show = 1;
        k->delta_tone = 2;
        if (stale > 0)
            snprintf(k->delta, sizeof(k->delta),
                     "%ld open since an earlier day", stale);
        else
            snprintf(k->delta, sizeof(k->delta), "all opened today");
    } else if (ws->kind == 1) {
        Dash_Kpi *k = next_kpi(ws);
        k->label = "My sales (7d)";
        money(k->value, sizeof(k->value), mine7);
        k->delta_show = 1;
        k->delta_tone = 2;
        snprintf(k->delta, sizeof(k->delta), "%ld orders", mn7);

        k = next_kpi(ws);
        k->label = "My avg basket (7d)";
        money(k->value, sizeof(k->value), mn7 > 0 ? mine7 / mn7 : 0);

        k = next_kpi(ws);
        k->label = "Team sales today";
        money(k->value, sizeof(k->value), s_today);
        k->delta_show = 1;
        k->delta_tone = 2;
        snprintf(k->delta, sizeof(k->delta), "%ld orders", n_today);

        k = next_kpi(ws);
        k->label = "Out of stock";
        snprintf(k->value, sizeof(k->value), "%ld", out0);
        k->delta_show = 1;
        k->delta_tone = 2;
        snprintf(k->delta, sizeof(k->delta), "products unavailable");
    } else if (ws->kind == 2) {
        Dash_Kpi *k = next_kpi(ws);
        k->label = "Pending";
        snprintf(k->value, sizeof(k->value), "%d", ws->del_pending);
        k->delta_show = 1;
        k->delta_tone = 2;
        snprintf(k->delta, sizeof(k->delta), "awaiting pickup");

        k = next_kpi(ws);
        k->label = "In transit";
        snprintf(k->value, sizeof(k->value), "%d", ws->del_transit);
        k->delta_show = 1;
        k->delta_tone = 2;
        snprintf(k->delta, sizeof(k->delta), "on the road");

        k = next_kpi(ws);
        k->label = "Delivered";
        snprintf(k->value, sizeof(k->value), "%d", ws->del_done);
        k->delta_show = 1;
        k->delta_tone = 2;
        snprintf(k->delta, sizeof(k->delta), "completed");
    }

    // ---- 7-day bars (manager) --------------------------------------------
    if (ws->kind == 0 && ndays > 0) {
        double vals[7], maxv = 0;
        for (int i = 0; i < 7; ++i) {
            char d[11];
            iso_day(d, now - (time_t) (6 - i) * DAY_SECS);
            vals[i] = 0;
            for (int j = 0; j < ndays; ++j)
                if (!strcmp(drows[j].d, d)) { vals[i] = drows[j].sales; break; }
            if (vals[i] > maxv) maxv = vals[i];
        }
        for (int i = 0; i < 7; ++i) {
            money(ws->bars[i].value, sizeof(ws->bars[i].value), vals[i]);
            if (vals[i] <= 0) {
                ws->bars[i].pct = 0;
            } else {
                int pct = maxv > 0 ? (int) (vals[i] / maxv * BAR_H) : BAR_H;
                ws->bars[i].pct = pct < 2 ? 2 : pct;
            }
        }
        for (int i = 0; i < 7; ++i) {
            char d[11];
            iso_day(d, now - (time_t) (6 - i) * DAY_SECS);
            snprintf(ws->bars[i].day, sizeof(ws->bars[i].day),
                     "%c%c/%c%c", d[8], d[9], d[5], d[6]);
        }
        money(ws->bars_sum, sizeof(ws->bars_sum), s7);
    }

    // ---- attention line (priority: stock risk > stale shift > queue) ------
    if (ws->kind == 0) {
        if (low > 0) {
            ws->attention_cls = "border-red bg-red/10 text-red";
            ws->attention_href = "/pos/stocks";
            snprintf(ws->attention, sizeof(ws->attention),
                     "%ld products at or below minimum stock", low);
        } else if (stale > 0) {
            ws->attention_cls = "border-yellow bg-yellow/10 text-yellow";
            ws->attention_href = "/pos/shifts";
            snprintf(ws->attention, sizeof(ws->attention),
                     "%ld shift%s open since an earlier day", stale,
                     stale == 1 ? "" : "s");
        } else if (pending > 0) {
            ws->attention_cls = "border-yellow bg-yellow/10 text-yellow";
            ws->attention_href = "/pos/orders";
            snprintf(ws->attention, sizeof(ws->attention),
                     "%ld orders pending - start fulfilling", pending);
        }
    } else if (ws->kind == 1) {
        if (out0 > 0) {
            ws->attention_cls = "border-yellow bg-yellow/10 text-yellow";
            snprintf(ws->attention, sizeof(ws->attention),
                     "%ld products out of stock", out0);
        } else if (pending > 0) {
            ws->attention_cls = "border-yellow bg-yellow/10 text-yellow";
            ws->attention_href = "/pos/orders";
            snprintf(ws->attention, sizeof(ws->attention),
                     "%ld orders pending in the queue", pending);
        }
    } else if (ws->kind == 2 && ws->del_pending > 0) {
        ws->attention_cls = "border-yellow bg-yellow/10 text-yellow";
        snprintf(ws->attention, sizeof(ws->attention),
                 "%d deliveries awaiting pickup", ws->del_pending);
    }

    // ---- recent orders (manager: store, counter: mine) -------------------
    if (ws->kind == 0 || ws->kind == 1) {
        sql_stmt stmt = {0};
        bool mine = ws->kind == 1;
        const char *q = mine ? q_recent_mine[db->lang] : q_recent[db->lang];
        bool ok = sql_prepare(db, q, &stmt);
        if (ok && mine) {
            ok = sql_bind(&stmt, 1,
                          (au->staff_id && au->staff_id[0])
                              ? SQL_SV(sv_from_cstr(au->staff_id))
                              : SQL_NIL());
        }
        if (ok) {
            while (sql_step(&stmt) == SQL_ROW && ws->recent_count < 8) {
                Dash_Order *o = &ws->recent[ws->recent_count++];
                const char *v;
                v = sql_col_text(&stmt, 0);
                snprintf(o->number, sizeof(o->number), "%s",
                         (v && v[0]) ? v : "-");
                v = sql_col_text(&stmt, 1);
                if (v && strlen(v) >= 16) snprintf(o->time, sizeof(o->time), "%.5s", v + 11);
                else snprintf(o->time, sizeof(o->time), "-");
                money(o->total, sizeof(o->total), col_double(&stmt, 2));
                const char *sid = sql_col_text(&stmt, 3);
                v = sql_col_text(&stmt, 4);
                snprintf(o->status, sizeof(o->status), "%s",
                         (v && v[0]) ? v : (sid ? sid : "-"));
                o->tone_cls = order_tone(sid);
                const char *f = sql_col_text(&stmt, 5);
                const char *l = sql_col_text(&stmt, 6);
                if (f && f[0] && l && l[0])
                    snprintf(o->who, sizeof(o->who), "%s %s", f, l);
                else if (f && f[0]) snprintf(o->who, sizeof(o->who), "%s", f);
                else if (l && l[0]) snprintf(o->who, sizeof(o->who), "%s", l);
                else snprintf(o->who, sizeof(o->who), "-");
            }
        }
        sql_finalize(&stmt);
    }

    // ---- counter: my open shift ------------------------------------------
    if (ws->kind == 1 && au->staff_id && au->staff_id[0]) {
        sql_stmt stmt = {0};
        if (sql_prepare(db, q_shift[db->lang], &stmt)
            && sql_bind(&stmt, 1, SQL_SV(sv_from_cstr(au->staff_id)))
            && sql_step(&stmt) == SQL_ROW) {
            const char *v;
            ws->has_shift = 1;
            v = sql_col_text(&stmt, 0);
            if (v && strlen(v) >= 16)
                snprintf(ws->shift_since, sizeof(ws->shift_since), "%.5s", v + 11);
            else
                snprintf(ws->shift_since, sizeof(ws->shift_since), "-");
            money(ws->shift_cash, sizeof(ws->shift_cash),
                  col_double(&stmt, 1));
            v = sql_col_text(&stmt, 2);
            snprintf(ws->shift_org, sizeof(ws->shift_org), "%s",
                     (v && v[0]) ? v : "-");
        }
        sql_finalize(&stmt);
    }

    db_close(db);
}
