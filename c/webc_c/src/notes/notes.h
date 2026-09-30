#ifndef SRC_NOTES_H_
#define SRC_NOTES_H_

#include <stdbool.h>
#include <stddef.h>
#include "module/nob.h"
#include "../db/sql.h"

typedef struct {
    const char *id;
    const char *user_id;
    const char *title;
    const char *created_at;
    const char *body;
} Note;

DA_NEW(Note, Notes)

bool read_notes(db_t *db, Notes *notes);
bool create_note(db_t *db, String_View *values, size_t count);
bool update_note(db_t *db, String_View *values, size_t count, String_View id);
bool delete_note(db_t *db, String_View id);

#endif // SRC_NOTES_H_
