#ifndef SRC_NOTES_H_
#define SRC_NOTES_H_

#include <stdbool.h>
#include <stddef.h>
#include "sqlite3.h"
#include "module/nob.h"

typedef struct {
    int id;
    const char *title;
    const char *created_at;
    const char *body;
} Note;

DA_NEW(Note, Notes)

bool load_notes(sqlite3 *db, Notes *notes);
bool insert_note(sqlite3 *db, String_View *values, size_t count);
bool update_note(sqlite3 *db, String_View *values, size_t count, int id);
bool delete_note(sqlite3 *db, int id);

#endif // SRC_NOTES_H_
