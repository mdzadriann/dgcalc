#include "editor/editor.h"

#include <stddef.h>
#include <string.h>

void editor_init(EditorBuffer *editor) {
    editor->text[0] = '\0';
    editor->length = 0;
    editor->cursor = 0;
}

bool editor_insert(EditorBuffer *editor, char value) {
    if (editor->length >= EDITOR_CAPACITY - 1) {
        return false;
    }

    memmove(&editor->text[editor->cursor + 1],
            &editor->text[editor->cursor],
            (size_t)(editor->length - editor->cursor) + 1);
    editor->text[editor->cursor] = value;
    editor->cursor++;
    editor->length++;
    return true;
}

bool editor_backspace(EditorBuffer *editor) {
    if (editor->cursor == 0) {
        return false;
    }

    memmove(&editor->text[editor->cursor - 1],
            &editor->text[editor->cursor],
            (size_t)(editor->length - editor->cursor) + 1);
    editor->cursor--;
    editor->length--;
    return true;
}

bool editor_delete(EditorBuffer *editor) {
    if (editor->cursor >= editor->length) {
        return false;
    }

    memmove(&editor->text[editor->cursor],
            &editor->text[editor->cursor + 1],
            (size_t)(editor->length - editor->cursor));
    editor->length--;
    return true;
}

bool editor_move(EditorBuffer *editor, int16_t delta) {
    int32_t destination = (int32_t)editor->cursor + delta;

    if (destination < 0 || destination > editor->length) {
        return false;
    }

    editor->cursor = (uint16_t)destination;
    return true;
}

bool editor_set_text(EditorBuffer *editor, const char *text) {
    size_t length = 0;

    while (length < EDITOR_CAPACITY && text[length] != '\0') {
        length++;
    }
    if (length == EDITOR_CAPACITY) {
        return false;
    }

    memcpy(editor->text, text, length + 1);
    editor->length = (uint16_t)length;
    editor->cursor = (uint16_t)length;
    return true;
}
