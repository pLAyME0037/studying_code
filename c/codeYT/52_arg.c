#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc > 1) {
        if (strcmp(*argv, "1")) {
            printf("Hello");
        }
        if (strcmp(*argv, "2")) {
            printf("World");
        }
    }
    printf("End");

    return 0;
}
