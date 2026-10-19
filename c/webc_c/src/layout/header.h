#ifndef HEADER
#define HEADER

#include "third_party/nob/nob.h"

void render_page_header(String_Builder *sb,
                        const char     *page_title,
                        const char     *current_path);
#endif // !HEADER
