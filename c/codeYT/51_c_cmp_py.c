#include <stdio.h>

int main(void) {
    int num = 0;

    for (int i = 0; i < 100000000; ++i) {
        num += 1 % 7;
        printf("Num %d: %d\n", i, num);
    }
}
