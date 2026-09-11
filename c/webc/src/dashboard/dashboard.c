#define NOB_STRIP_PREFIX
#include "../../nob.h"

#include "dashboard.h"

static const char *DASHBOARD_ICONS[] = {
    "M9 12l2 2 4-4m5.618-4.016A11.955 11.955 0 0112 2.944a11.955 11.955 0 01-8.618 3.04A12.02 12.02 0 003 9c0 5.591 3.824 10.29 9 11.622 5.176-1.332 9-6.03 9-11.622 0-1.042-.133-2.052-.382-3.016z",
    "M12 8c-1.657 0-3 .895-3 2s1.343 2 3 2 3 .895 3 2-1.343 2-3 2m0-8c1.11 0 2.08.402 2.599 1M12 8V7m0 1v8m0 0v1m0-1c-1.11 0-2.08-.402-2.599-1M21 12a9 9 0 11-18 0 9 9 0 0118 0z",
    "M19 21V5a2 2 0 00-2-2H7a2 2 0 00-2 2v16m14 0h2m-2 0h-5m-9 0H3m2 0h5M9 7h1m-1 4h1m4-4h1m-1 4h1m-5 10v-5a1 1 0 011-1h2a1 1 0 011 1v5m-4 0h4",
    "M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0M15 7a3 3 0 11-6 0 3 3 0 016 0zm6 3a2 2 0 11-4 0 2 2 0 014 0zM7 10a2 2 0 11-4 0 2 2 0 014 0z",
    "M12 6.253v13m0-13C10.832 5.477 9.246 5 7.5 5S4.168 5.477 3 6.253v13C4.168 18.477 5.754 18 7.5 18s3.332.477 4.5 1.253m0-13C13.168 5.477 14.754 5 16.5 5c1.747 0 3.332.477 4.5 1.253v13C19.832 18.477 18.247 18 16.5 18c-1.746 0-3.332.477-4.5 1.253",
    "M21 21h-6m-3 0H6m-3 0V9m3 12V9m-3 3V5m3 0h9m3 0v7m0 0v4",
    "M3 6l3 1m0 0l-3 9a5.002 5.002 0 006.001 0M6 7l3 9M6 7l6-2m6 2l3-1m-3 1l-3 9a5.002 5.002 0 006.001 0M18 7l3 9m-3-9l-6-2m0-2v2m0 16V5m0 16H9m3 0h3",
    "M19 14l-7 7m0 0l-7-7m7 7V3",
    "M12 18h.01M8 21h8a2 2 0 002-2V5a2 2 0 00-2-2H8a2 2 0 00-2 2v14a2 2 0 002 2z",
};

void dashboard_custom_cards(Dashboard_Cards *cards) {
    memset(cards, 0, sizeof(*cards));
    static const char *paths[] = {
        "/medicine-stock",
        "/medicine-imports",
        "/patient-medicine-invoices",
        "/patient-invoice-out",
        "/organization-daily-invoices",
        "/organization-balance",
        "/organization-invoice-out",
    };
    static const char *titles[] = {
        "Medicine Stock",
        "Medicine Import",
        "Patient Medicine Invoice",
        "Patient Invoice Out",
        "Org Daily Invoice",
        "Org Balance",
        "Org Invoice Out",
    };
    static const size_t icon_ids[] = {0, 1, 2, 3, 4, 5, 6};
    const size_t n = ARRAY_LEN(paths);
    for (size_t i = 0; i < n; ++i) {
        Dashboard_Card card = {
            .path  = paths[i],
            .icon  = DASHBOARD_ICONS[icon_ids[i]],
            .title = titles[i],
        };
        da_append(cards, card);
    }
}

const char *dashboard_icon(size_t i) {
    return DASHBOARD_ICONS[i % ARRAY_LEN(DASHBOARD_ICONS)];
}
