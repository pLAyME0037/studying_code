#ifndef SRC_NOTES_H_
#define SRC_NOTES_H_

#include <stdbool.h>
#include <stddef.h>
#include "sqlite3.h"
#include "nob.h"

typedef struct {
    int id;
    const char *title;
    const char *created_at;
    const char *body;
} Note;

DA_NEW(Note, Notes)

bool load_notes(sqlite3 *db, Notes *notes);
bool insert_note(sqlite3 *db, const char *title, const char *body);
bool update_note(sqlite3 *db, int id, const char *title, const char *body);
bool delete_note(sqlite3 *db, int id);

#endif // SRC_NOTES_H_
