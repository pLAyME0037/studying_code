#include <stdio.h>

int main(void) {
    int a = 64;
    int ssize = 6;
    int b = a | ssize;

    printf("%d | %d: %d",a, ssize, b);

    return 0;
}
