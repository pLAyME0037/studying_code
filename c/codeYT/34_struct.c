#include <stdio.h>
#include <string.h>

struct Player {
    char name[12];
    int score;
};

typedef struct User {
    char a[10];
    bool b;
} UserTS;

int main() {
    /* struct Player player1; */
    /* struct Player player2; */
    /**/
    /* strcpy(player1.name, "apple"); */
    /* player1.score= 4; */
    /**/
    /* strcpy(player2.name, "banana"); */
    /* player2.score= 2; */
    /**/
    /* printf("%s\n", player1.name); */
    /* printf("%d\n", player1.score); */
    /**/
    /* printf("%s\n", player2.name); */
    /* printf("%d\n", player2.score); */

    UserTS A1;
    size_t user_size = sizeof(A1);

    strcpy(A1.a, "hello");
    A1.b = true;

    printf("%s %d\n", A1.a, A1.b);
    /* printf("%s\n", A1.b); */
    return 0;
}
