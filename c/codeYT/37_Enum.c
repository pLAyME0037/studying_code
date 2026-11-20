#include <stdio.h>

enum Day {Sun=1, Mon=2, Tue=3, Wed=4, Thu=5, Fri=6, Sat=7};

int main(void) {
    enum Day today= Sun;

    //if (today == Sun || today == Sat )
    if (today == 5 || today == 6 ) {
        printf("\nIt is the weekend");
    } else{
        printf("\nIt is not the weekend");
    }

    return 0;
}
