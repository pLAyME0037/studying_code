#ifndef SRC_DASHBOARD_H_
#define SRC_DASHBOARD_H_

#include <stddef.h>

typedef struct {
    const char *path;
    const char *icon;   // SVG `d` attribute data
    const char *title;
} Dashboard_Card;

typedef struct {
    Dashboard_Card *items;
    size_t count;
    size_t capacity;
} Dashboard_Cards;

// Fills `cards` with the custom (non-CRUD) dashboard entries.
// Memory is arena-scoped and valid until the end of the request.
void dashboard_custom_cards(Dashboard_Cards *cards);

// Returns the icon path for the i-th CRUD module slot.
const char *dashboard_icon(size_t i);

#endif // SRC_DASHBOARD_H_
