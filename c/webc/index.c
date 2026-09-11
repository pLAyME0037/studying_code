#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#define OUT STDOUT_FILENO
#define INT(x) dprintf(STDOUT_FILENO, "%d", x);
#define CLS(cond, t, f) printf("%s", (cond) ? (t) : (f));
#define STR(x) printf("%s", x);

#include "core/user_mockup_data/user_data.h"

void create_html(void) {
    User u = User_data();
    bool sidebar_collapsed = false;
    bool is_active = true;
#include "auto_ctrl/cttochtml/header.h"
#include "index.h"
#include "auto_ctrl/cttochtml/sample.h"
#include "auto_ctrl/cttochtml/sqlite.h"
#include "auto_ctrl/cttochtml/footer.h"
}

int main(void) {
    create_html();

    return 0;
}
