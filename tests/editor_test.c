#include "editor/editor.h"
#include "test.h"

#include <string.h>

static void test_editor_inserts_and_deletes_at_cursor(void) {
    EditorBuffer editor;

    editor_init(&editor);
    ASSERT_TRUE(editor_set_text(&editor, "x+1"));
    ASSERT_TRUE(editor_move(&editor, -1));
    ASSERT_TRUE(editor_insert(&editor, '2'));
    ASSERT_STR_EQ("x+21", editor.text);
    ASSERT_EQ_INT(3, editor.cursor);

    ASSERT_TRUE(editor_backspace(&editor));
    ASSERT_STR_EQ("x+1", editor.text);
    ASSERT_EQ_INT(2, editor.cursor);

    ASSERT_TRUE(editor_delete(&editor));
    ASSERT_STR_EQ("x+", editor.text);
    ASSERT_EQ_INT(2, editor.cursor);
}

static void test_editor_rejects_256th_byte_without_mutation(void) {
    EditorBuffer editor;
    char full[EDITOR_CAPACITY];

    memset(full, 'x', EDITOR_CAPACITY - 1);
    full[EDITOR_CAPACITY - 1] = '\0';

    editor_init(&editor);
    ASSERT_TRUE(editor_set_text(&editor, full));
    ASSERT_FALSE(editor_insert(&editor, '+'));
    ASSERT_EQ_INT(EDITOR_CAPACITY - 1, editor.length);
    ASSERT_EQ_INT(EDITOR_CAPACITY - 1, editor.cursor);
    ASSERT_STR_EQ(full, editor.text);
}

void run_editor_tests(void) {
    test_editor_inserts_and_deletes_at_cursor();
    test_editor_rejects_256th_byte_without_mutation();
}
