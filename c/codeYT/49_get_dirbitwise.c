#include <stdio.h>

#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

#define EXIT_FAILURE 1

int main(void) {
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir("/mnt/disk2/mythings/studying_code/c/webc/src/")) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") && strcmp(ent->d_name, "..")) {
                printf ("%s\n", ent->d_name);
            } else { sleep(0); }
        }
        closedir(dir);
    } else {
        perror("");
        return EXIT_FAILURE;
    }

    return 0;
}
