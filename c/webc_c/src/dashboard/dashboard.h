#ifndef SRC_DASHBOARD_H_
#define SRC_DASHBOARD_H_

#include "core/auth/auth.h"

// Phase 14 enterprise role workspaces: /dashboard loads one Dashboard_Data
// and the template renders the center matching the signed-in role -
// kind 0 = manager (KPI strip + attention line + 7-day bars + recent
// orders), kind 1 = counter (my shift + my numbers + my recent orders),
// kind 2 = driver (assigned deliveries run), kind 3 = generic welcome
// (no role, or nobody signed in - no queries run).
// Every string is preformatted for the template; labels, chip classes and
// tones are literal pointers (tailwind scans this file's literals).

typedef struct {
    const char *label;    // static literal
    char        value[40];
    char        delta[64]; // "" + delta_show 0 = hidden
    int         delta_tone; // 0 red, 1 green, 2 neutral
    int         delta_show;
} Dash_Kpi;

typedef struct {
    char        number[64];
    char        time[6];   // HH:MM
    char        who[96];
    char        total[32];
    char        status[64]; // Khmer dictionary label
    const char *tone_cls;   // literal tailwind classes
} Dash_Order;

typedef struct {
    char        number[64];
    char        recipient[96];
    char        phone[40];
    char        note[128];  // delivery address
    char        status[32]; // Pending / In transit / Delivered
    const char *tone_cls;
} Dash_Deliv;

typedef struct {
    char        greeting[160];   // "Good day, <name>"
    char        date_human[64];  // "Tue 06 Oct 2026"
    char        role_label[128]; // Khmer role_name, "No role" fallback
    char        workspace[40];   // "Manager workspace" / ... (display text)
    const char *role_cls;        // literal chip classes
    int         kind;            // 0 manager, 1 counter, 2 driver, 3 generic

    Dash_Kpi    kpis[6];
    int         kpi_count;

    char        attention[200];
    const char *attention_href;  // "" = plain text, no link
    const char *attention_cls;   // literal border/bg/text classes

    struct {
        char day[8];             // "dd/mm"
        char value[32];          // money
        int  pct;                // bar height px (0..72)
    } bars[7];
    char        bars_sum[32];

    Dash_Order  recent[8];
    int         recent_count;

    // counter workspace
    int         has_shift;
    char        shift_since[8];  // HH:MM
    char        shift_cash[32];
    char        shift_org[96];
    int         out_of_stock;

    // driver workspace
    int         del_pending;
    int         del_transit;
    int         del_done;
    Dash_Deliv  delivs[12];
    int         deliv_count;
} Dashboard_Data;

// Fills `ws` for `au` (NULL = nobody signed in). Workspace-scoped, one
// connection, no writes. Safe to call with a role that needs no queries.
void dashboard_load(Dashboard_Data *ws, const Auth_User *au);

#endif // SRC_DASHBOARD_H_
