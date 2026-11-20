#include <stdio.h>

int main(void) {
    size_t a = 8;
    int sz = 6;
    int b = a;

    a = a << 4;
    a = a << 4;
    a = a >> 4;

    a = (a << 1) | (a << 2) | (a << 3) | (a << 4);
    b = (a << 1) | (a << 2) | (a >> 3) | (a << 4);

    b & a;

    return 0;
}
