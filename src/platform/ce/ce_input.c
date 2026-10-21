#include "platform/ce/ce_input.h"

#include <keypadc.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint8_t previous_keys[8];
// shortcut: simultaneous ordinary keys use deterministic keypad order, upgrade to an interrupt ring only if physical tests require chronological ordering
static uint8_t pending_keys[8];
static bool alpha_latched;
static bool second_latched;

static void capture_new_keys(void) {
    uint8_t group;

    for (group = 1; group <= 7; group++) {
        uint8_t current = kb_Data[group];
        pending_keys[group] |= (uint8_t)(current & (uint8_t)~previous_keys[group]);
        previous_keys[group] = current;
    }
}

static kb_lkey_t next_pending_key(void) {
    uint8_t group;

    if ((pending_keys[kb_Key2nd >> 8] & (uint8_t)kb_Key2nd) != 0) {
        pending_keys[kb_Key2nd >> 8] &= (uint8_t)~kb_Key2nd;
        return kb_Key2nd;
    }
    if ((pending_keys[kb_KeyAlpha >> 8] & (uint8_t)kb_KeyAlpha) != 0) {
        pending_keys[kb_KeyAlpha >> 8] &= (uint8_t)~kb_KeyAlpha;
        return kb_KeyAlpha;
    }
    for (group = 1; group <= 7; group++) {
        uint8_t bit;
        for (bit = 1; bit != 0; bit <<= 1) {
            if ((pending_keys[group] & bit) != 0) {
                pending_keys[group] &= (uint8_t)~bit;
                return (kb_lkey_t)(((kb_lkey_t)group << 8) | bit);
            }
        }
    }
    return 0;
}

static bool insert_text(EditorBuffer *editor, const char *text) {
    size_t length = strlen(text);
    size_t index;

    if ((size_t)editor->length + length >= EDITOR_CAPACITY) {
        return false;
    }
    for (index = 0; index < length; index++) {
        (void)editor_insert(editor, text[index]);
    }
    return true;
}

static bool relation_character(char value) {
    return value == '=' || value == '<' || value == '>';
}

static void cycle_relation(EditorBuffer *editor) {
    size_t start = editor->cursor;
    size_t end;
    size_t length;
    size_t replacement_length;
    const char *replacement;
    char first;

    if (start > 0 && relation_character(editor->text[start - 1])) {
        start--;
        if (start > 0 && editor->text[start] == '=' &&
            (editor->text[start - 1] == '<' || editor->text[start - 1] == '>')) {
            start--;
        }
    }
    first = editor->text[start];
    if (!relation_character(first)) {
        (void)editor_insert(editor, '=');
        return;
    }
    end = start + 1;
    if ((first == '<' || first == '>') && editor->text[end] == '=') {
        end++;
    }
    /* Reject the entire invalid cluster rather than cycling a valid prefix. */
    if ((start > 0 && relation_character(editor->text[start - 1])) ||
        relation_character(editor->text[end])) {
        return;
    }
    if (first == '=') {
        replacement = "<";
    } else if (first == '<') {
        replacement = end - start == 2 ? ">=" : ">";
    } else {
        replacement = end - start == 2 ? "=" : "<=";
    }
    replacement_length = strlen(replacement);
    length = editor->length - (end - start) + replacement_length;
    if (length >= EDITOR_CAPACITY) {
        return;
    }
    memmove(&editor->text[start + replacement_length], &editor->text[end],
            editor->length - end + 1);
    memcpy(&editor->text[start], replacement, replacement_length);
    editor->length = (uint16_t)length;
    editor->cursor = (uint16_t)(start + replacement_length);
}

static char alpha_character(kb_lkey_t key) {
    switch (key) {
        case kb_KeyMath: return 'a';
        case kb_KeyApps: return 'b';
        case kb_KeyPrgm: return 'c';
        case kb_KeyVars: return 'd';
        case kb_KeySin: return 'e';
        case kb_KeyCos: return 'f';
        case kb_KeyTan: return 'g';
        case kb_KeyPower: return 'h';
        case kb_KeySquare: return 'i';
        case kb_KeyComma: return 'j';
        case kb_KeyLParen: return 'k';
        case kb_KeyRParen: return 'l';
        case kb_KeyDiv: return 'm';
        case kb_KeyLog: return 'n';
        case kb_Key7: return 'o';
        case kb_Key8: return 'p';
        case kb_Key9: return 'q';
        case kb_KeyMul: return 'r';
        case kb_KeyLn: return 's';
        case kb_Key4: return 't';
        case kb_Key5: return 'u';
        case kb_Key6: return 'v';
        case kb_KeySub: return 'w';
        case kb_KeySto: return 'x';
        case kb_Key1: return 'y';
        case kb_Key2: return 'z';
        default: return '\0';
    }
}

static const char *second_text(kb_lkey_t key) {
    switch (key) {
        case kb_KeySquare: return "sqrt(";
        case kb_KeySin: return "arcsin(";
        case kb_KeyCos: return "arccos(";
        case kb_KeyTan: return "arctan(";
        default: return NULL;
    }
}

static const char *normal_text(kb_lkey_t key) {
    switch (key) {
        case kb_Key0: return "0";
        case kb_Key1: return "1";
        case kb_Key2: return "2";
        case kb_Key3: return "3";
        case kb_Key4: return "4";
        case kb_Key5: return "5";
        case kb_Key6: return "6";
        case kb_Key7: return "7";
        case kb_Key8: return "8";
        case kb_Key9: return "9";
        case kb_KeyDecPnt: return ".";
        case kb_KeyComma: return ",";
        case kb_KeyAdd: return "+";
        case kb_KeySub: return "-";
        case kb_KeyMul: return "*";
        case kb_KeyDiv: return "/";
        case kb_KeyPower: return "^";
        case kb_KeyLParen: return "(";
        case kb_KeyRParen: return ")";
        case kb_KeyChs: return "-";
        case kb_KeySquare: return "^2";
        case kb_KeyGraphVar: return "x";
        case kb_KeySin: return "sin(";
        case kb_KeyCos: return "cos(";
        case kb_KeyTan: return "tan(";
        case kb_KeyLn: return "ln(";
        case kb_KeyLog: return "log(";
        default: return NULL;
    }
}

void ce_input_begin(void) {
    uint8_t group;

    kb_SetMode(MODE_3_CONTINUOUS);
    for (group = 1; group <= 7; group++) {
        previous_keys[group] = kb_Data[group];
    }
    ce_input_discard_pending();
    alpha_latched = false;
    second_latched = false;
}

void ce_input_discard_pending(void) {
    memset(pending_keys, 0, sizeof(pending_keys));
}

AppAction ce_input_poll(EditorBuffer *active_editor, bool editing, AppView view) {
    kb_lkey_t key;
    const char *text;

    capture_new_keys();
    key = next_pending_key();
    if (key == 0) {
        return APP_ACTION_NONE;
    }
    if (key == kb_Key2nd) {
        second_latched = !second_latched;
        alpha_latched = false;
        return APP_ACTION_NONE;
    }
    if (second_latched && key == kb_KeyMode) {
        second_latched = false;
        ce_input_discard_pending();
        return APP_ACTION_QUIT;
    }
    if (key == kb_KeyYequ) {
        alpha_latched = false;
        second_latched = false;
        ce_input_discard_pending();
        return APP_ACTION_SHOW_EXPRESSIONS;
    }
    if (key == kb_KeyWindow) {
        alpha_latched = false;
        second_latched = false;
        ce_input_discard_pending();
        return APP_ACTION_SHOW_SPLIT;
    }
    if (key == kb_KeyZoom) {
        alpha_latched = false;
        second_latched = false;
        ce_input_discard_pending();
        return APP_ACTION_TOGGLE_GRAPH_MODE;
    }
    if (key == kb_KeyTrace) {
        alpha_latched = false;
        second_latched = false;
        ce_input_discard_pending();
        return APP_ACTION_OPEN_MENU;
    }
    if (key == kb_KeyGraph) {
        alpha_latched = false;
        second_latched = false;
        ce_input_discard_pending();
        return APP_ACTION_SHOW_GRAPH;
    }
    if (view == APP_VIEW_GRAPH) {
        alpha_latched = false;
        second_latched = false;
        switch (key) {
            case kb_KeyLeft: return APP_ACTION_PAN_LEFT;
            case kb_KeyRight: return APP_ACTION_PAN_RIGHT;
            case kb_KeyUp: return APP_ACTION_PAN_UP;
            case kb_KeyDown: return APP_ACTION_PAN_DOWN;
            case kb_KeyAdd: return APP_ACTION_ZOOM_IN;
            case kb_KeySub: return APP_ACTION_ZOOM_OUT;
            default: return APP_ACTION_NONE;
        }
    }
    if (!editing) {
        alpha_latched = false;
        second_latched = false;
        if (key == kb_KeyUp) {
            return APP_ACTION_MOVE_SELECTION_UP;
        }
        if (key == kb_KeyDown) {
            return APP_ACTION_MOVE_SELECTION_DOWN;
        }
        if (key == kb_KeyEnter) {
            return APP_ACTION_BEGIN_EDIT;
        }
        if (key == kb_KeyClear) {
            return APP_ACTION_CLEAR_EXPRESSION;
        }
        return APP_ACTION_NONE;
    }

    if (key == kb_KeyAlpha) {
        alpha_latched = !alpha_latched;
        second_latched = false;
        return APP_ACTION_NONE;
    }
    if (alpha_latched) {
        char character = alpha_character(key);
        alpha_latched = false;
        if (character != '\0') {
            (void)editor_insert(active_editor, character);
        }
        return APP_ACTION_NONE;
    }
    if (second_latched) {
        text = second_text(key);
        second_latched = false;
        if (text != NULL) {
            (void)insert_text(active_editor, text);
        }
        return APP_ACTION_NONE;
    }

    if (key == kb_KeyEnter) {
        return APP_ACTION_COMMIT_EDIT;
    }
    if (key == kb_KeyClear) {
        return APP_ACTION_CLEAR_EXPRESSION;
    }
    if (key == kb_KeyDel) {
        (void)editor_backspace(active_editor);
        return APP_ACTION_NONE;
    }
    if (key == kb_KeyLeft) {
        (void)editor_move(active_editor, -1);
        return APP_ACTION_NONE;
    }
    if (key == kb_KeyRight) {
        (void)editor_move(active_editor, 1);
        return APP_ACTION_NONE;
    }
    if (key == kb_KeyMath) {
        cycle_relation(active_editor);
        return APP_ACTION_NONE;
    }
    text = normal_text(key);
    if (text != NULL) {
        (void)insert_text(active_editor, text);
    }
    return APP_ACTION_NONE;
}

bool ce_input_graph_navigation_active(void) {
    return kb_IsDown(kb_KeyLeft) || kb_IsDown(kb_KeyRight) ||
           kb_IsDown(kb_KeyUp) || kb_IsDown(kb_KeyDown) ||
           kb_IsDown(kb_KeyAdd) || kb_IsDown(kb_KeySub);
}

void ce_input_end(void) {
    kb_SetMode(MODE_0_IDLE);
    kb_Reset();
}
