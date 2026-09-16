#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uchar.h>

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} Lists;

static Lists *list = {0};

Lists *list_new(void) {
    list = malloc(sizeof(*list));

    list->capacity = 5;
    list->items = malloc(list->capacity * sizeof(char*));
    list->count = 0;

    return list;
}

Lists *add(char *item) {
    if (list->capacity == list->count) {
        list->capacity *= 2;
        list->items = realloc(list->items, list->capacity * sizeof(char*));
    }
    list->items[list->count++] = strdup(item);

    return list;
}

Lists *pop(void) {
    if (list->count < 1) return list;
    list->items[list->count] = NULL;
    free(list->items[list->count]);
    if (list->count == list->capacity/2) {
        list->capacity /= 2;
        list->items = realloc(list->items, list->capacity * sizeof(char*));
    }
    list->count--;

    return list;
}

Lists *dump(void) {
    if (list->count < 1) return list;
    if (list->capacity == list->capacity/list->count) {
        list->capacity /= 2;
        list->items = realloc(list->items, list->capacity * sizeof(char*));
    }
    free(list->items[0]);
    memmove(&list->items[0], &list->items[1], (list->count - 1) * sizeof(char*));
    list->count--;

    return list;
}

typedef enum {
    INDEX = 0,
    ELEMENT,
    NONE,
} TYPE;

// Lists *del(TYPE type, ...) {
//     if (list->count < 1) return list;
//     if (list->capacity == list->capacity/list->count) {
//         list->capacity /= 2;
//         list->items = realloc(list->items, list->capacity * sizeof(char*));
//     }
//
//     va_list args;
//     va_start(args, type);
//     char *item = va_arg(args, char*);
//
//     va_end(args);
//
//     switch (type) {
//     case INDEX: {
//         for (int i = 0; i < type; ++i) {
//             free(list->items[(size_t)item]);
//             memmove(&list->items[(size_t)item], &list->items[(size_t)item + 1], (list->count - 1) * sizeof(char*));
//             list->count--;
//         }
//     } break;
//
//     case ELEMENT: {
//     } break;
//
//     default: dump();
//
//     return list;
// }

inline Lists *peek(Lists *e) {
    if (e == NULL || e->count == 0) return NULL;
    e->items[e->count - 1];
}

Lists *list_print(void) {
    printf("[");
    size_t full_list = list->count;
    for (size_t i = 0; i < list->count; ++i) {
        printf("%s", list->items[i]);
        if ((full_list--) > 1) printf(", ");
    }
    printf("]\n");

    free(list->items);
    list->items = NULL;
    list->count = list->capacity = 0;

    return list;
}

int main(void) {
    list_new();
    add("Hello, ");
    add("World");
    add("!");
    // del(INDEX, 1);
    list_print();
    //
    // list_new();
    // pop();
    // add("This");
    // add("is");
    // add("1");
    // add("2");
    // add("3");
    // add("4");
    // add("5");
    // add("6");
    // add("7");
    // add("8");
    // add("9");
    // add("10");
    // add("11");
    // add("12");
    // add("13");
    // dump();
    // pop();
    // pop();
    // pop();
    // pop();
    // pop();
    // pop();
    // list_print();

    return 0;
}
