#ifndef DGCALC_EDITOR_H
#define DGCALC_EDITOR_H

#include <stdbool.h>
#include <stdint.h>

#define EDITOR_CAPACITY 256

typedef struct {
    char text[EDITOR_CAPACITY];
    uint16_t length;
    uint16_t cursor;
} EditorBuffer;

void editor_init(EditorBuffer *editor);
bool editor_insert(EditorBuffer *editor, char value);
bool editor_backspace(EditorBuffer *editor);
bool editor_delete(EditorBuffer *editor);
bool editor_move(EditorBuffer *editor, int16_t delta);
bool editor_set_text(EditorBuffer *editor, const char *text);

#endif
