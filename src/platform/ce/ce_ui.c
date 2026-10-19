#include "platform/ce/ce_ui.h"

#include <graphx.h>
#include <keypadc.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    COLOR_BACKGROUND,
    COLOR_PANEL,
    COLOR_TEXT,
    COLOR_MUTED,
    COLOR_BLUE,
    COLOR_RED,
    COLOR_GRID,
    COLOR_GREEN
};

static void configure_palette(void) {
    gfx_palette[COLOR_BACKGROUND] = gfx_RGBTo1555(248, 249, 250);
    gfx_palette[COLOR_PANEL] = gfx_RGBTo1555(255, 255, 255);
    gfx_palette[COLOR_TEXT] = gfx_RGBTo1555(38, 41, 45);
    gfx_palette[COLOR_MUTED] = gfx_RGBTo1555(108, 117, 125);
    gfx_palette[COLOR_BLUE] = gfx_RGBTo1555(47, 111, 237);
    gfx_palette[COLOR_RED] = gfx_RGBTo1555(214, 51, 63);
    gfx_palette[COLOR_GRID] = gfx_RGBTo1555(218, 220, 224);
    gfx_palette[COLOR_GREEN] = gfx_RGBTo1555(25, 135, 84);
}

static const char *view_name(AppView view) {
    switch (view) {
        case APP_VIEW_EXPRESSIONS: return "Expressions";
        case APP_VIEW_SPLIT: return "Split";
        case APP_VIEW_GRAPH: return "Graph";
        default: return "";
    }
}

static const char *status_name(ExpressionStatus status) {
    switch (status) {
        case EXPRESSION_INVALID: return "syntax";
        case EXPRESSION_UNDEFINED: return "undefined";
        case EXPRESSION_CYCLE: return "cycle";
        default: return "";
    }
}

static void print_clipped(const char *text, int x, int y, uint8_t characters) {
    char clipped[41];
    size_t length = strlen(text);

    if (characters > 40) {
        characters = 40;
    }
    if (length > characters) {
        length = characters;
    }
    memcpy(clipped, text, length);
    clipped[length] = '\0';
    gfx_PrintStringXY(clipped, x, y);
}

static void draw_header(const AppState *state) {
    gfx_SetColor(COLOR_BLUE);
    gfx_FillRectangle_NoClip(0, 0, 320, 24);
    gfx_SetTextFGColor(COLOR_PANEL);
    gfx_PrintStringXY("DGCALC", 6, 8);
    gfx_PrintStringXY(view_name(state->view), 72, 8);
    gfx_PrintStringXY(state->mode == GRAPH_MODE_2D ? "2D" : "3D", 292, 8);
}

static void draw_key_bar(const AppState *state) {
    static const char *const labels[] = {
        "Expr", "Split", "2D/3D", "Menu", "Graph"
    };
    uint8_t index;

    gfx_SetColor(COLOR_PANEL);
    gfx_FillRectangle_NoClip(0, 220, 320, 20);
    gfx_SetColor(COLOR_GRID);
    gfx_HorizLine_NoClip(0, 220, 320);
    for (index = 0; index < 5; index++) {
        int left = index * 64;
        int text_width = gfx_GetStringWidth(labels[index]);
        bool active = (index == 0 && state->view == APP_VIEW_EXPRESSIONS) ||
                      (index == 1 && state->view == APP_VIEW_SPLIT) ||
                      (index == 4 && state->view == APP_VIEW_GRAPH);

        if (index > 0) {
            gfx_SetColor(COLOR_GRID);
            gfx_VertLine_NoClip(left, 220, 20);
        }
        if (active) {
            gfx_SetColor(COLOR_BLUE);
            gfx_FillRectangle_NoClip(left + 1, 221, 63, 19);
            gfx_SetTextFGColor(COLOR_PANEL);
        } else {
            gfx_SetTextFGColor(COLOR_MUTED);
        }
        gfx_PrintStringXY(labels[index],
                          left + (64 - text_width) / 2,
                          227);
    }
}

static void draw_expression_list(const AppState *state,
                                 const Project *project,
                                 int width,
                                 bool editing) {
    uint8_t visible = 8;
    uint8_t start = state->selected_expression >= visible
                        ? (uint8_t)(state->selected_expression - visible + 1)
                        : 0;
    uint8_t row;

    gfx_SetColor(COLOR_PANEL);
    gfx_FillRectangle_NoClip(0, 24, width, 196);
    for (row = 0; row < visible; row++) {
        uint8_t index = (uint8_t)(start + row);
        const ExpressionLine *line;
        int y = 30 + row * 23;
        const char *error;
        uint8_t characters;
        uint16_t text_start = 0;

        if (index >= PROJECT_MAX_EXPRESSIONS) {
            break;
        }
        line = &project->lines[index];
        error = status_name(line->status);
        if (index == state->selected_expression) {
            gfx_SetColor(COLOR_BLUE);
            gfx_Rectangle_NoClip(2, y - 3, width - 5, 21);
        }
        gfx_SetTextFGColor(COLOR_MUTED);
        gfx_SetTextXY(7, y + 2);
        gfx_PrintUInt((unsigned int)index + 1, index >= 9 ? 2 : 1);
        gfx_SetTextFGColor(COLOR_TEXT);
        characters = width > 200 ? (error[0] == '\0' ? 34 : 25)
                                 : (error[0] == '\0' ? 12 : 10);
        if (editing && index == state->selected_expression &&
            line->draft.cursor >= characters) {
            text_start = (uint16_t)(line->draft.cursor - characters + 1);
        }
        print_clipped(&line->draft.text[text_start],
                      27,
                      y + 2,
                      characters);
        if (editing && index == state->selected_expression) {
            char prefix[41];
            uint16_t prefix_length =
                (uint16_t)(line->draft.cursor - text_start);
            int caret_x;

            memcpy(prefix, &line->draft.text[text_start], prefix_length);
            prefix[prefix_length] = '\0';
            caret_x = 27 + gfx_GetStringWidth(prefix);
            gfx_SetColor(COLOR_BLUE);
            gfx_VertLine_NoClip(caret_x, y + 1, 11);
        }
        if (error[0] != '\0') {
            gfx_SetTextFGColor(COLOR_RED);
            print_clipped(width > 200 ? error : "!",
                          width > 200 ? width - 67 : width - 11,
                          y + 2,
                          width > 200 ? 8 : 1);
        }
    }
}

static void draw_graph_panel(int x, int width, GraphMode mode) {
    int grid_x;
    int grid_y;
    int center_x = x + width / 2;

    gfx_SetColor(COLOR_BACKGROUND);
    gfx_FillRectangle_NoClip(x, 24, width, 196);
    gfx_SetColor(COLOR_GRID);
    for (grid_x = x; grid_x < x + width; grid_x += 20) {
        gfx_VertLine_NoClip(grid_x, 24, 196);
    }
    for (grid_y = 32; grid_y < 220; grid_y += 20) {
        gfx_HorizLine_NoClip(x, grid_y, width);
    }
    gfx_SetColor(COLOR_MUTED);
    gfx_VertLine_NoClip(center_x, 24, 196);
    gfx_HorizLine_NoClip(x, 122, width);
    if (mode == GRAPH_MODE_3D) {
        gfx_Line(center_x, 122, x + width - 18, 52);
    }
    gfx_SetColor(COLOR_PANEL);
    gfx_FillRectangle_NoClip(center_x - 55, 91, 110, 42);
    gfx_SetTextFGColor(COLOR_TEXT);
    gfx_PrintStringXY(mode == GRAPH_MODE_2D ? "2D graph" : "3D graph",
                      center_x - 28,
                      100);
    gfx_SetTextFGColor(COLOR_MUTED);
    gfx_PrintStringXY("plotting later", center_x - 48, 117);
}

void ce_ui_begin(void) {
    gfx_Begin();
    gfx_SetDrawBuffer();
    configure_palette();
    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(COLOR_TEXT);
    gfx_SetTextBGColor(COLOR_PANEL);
    gfx_SetTextTransparentColor(COLOR_PANEL);
}

void ce_ui_draw(const AppState *state,
                const Project *project,
                bool editing) {
    gfx_FillScreen(COLOR_BACKGROUND);
    draw_header(state);
    if (state->view == APP_VIEW_EXPRESSIONS) {
        draw_expression_list(state, project, 320, editing);
    } else if (state->view == APP_VIEW_SPLIT) {
        draw_expression_list(state, project, 132, editing);
        draw_graph_panel(132, 188, state->mode);
    } else {
        draw_graph_panel(0, 320, state->mode);
    }
    draw_key_bar(state);
    gfx_SwapDraw();
}

static bool any_key_down(void) {
    uint8_t group;
    for (group = 1; group <= 7; group++) {
        if (kb_Data[group] != 0) {
            return true;
        }
    }
    return false;
}

static void wait_for_release(void) {
    do {
        kb_Scan();
    } while (any_key_down());
}

static kb_lkey_t wait_for_key(void) {
    static const kb_lkey_t accepted[] = {
        kb_KeyUp, kb_KeyDown, kb_KeyLeft, kb_KeyRight,
        kb_KeyEnter, kb_KeyClear
    };
    bool second = false;
    size_t index;

    wait_for_release();
    for (;;) {
        kb_Scan();
        if (kb_IsDown(kb_Key2nd)) {
            wait_for_release();
            second = true;
            continue;
        }
        if (second && kb_IsDown(kb_KeyMode)) {
            wait_for_release();
            return kb_KeyMode;
        }
        for (index = 0; index < sizeof(accepted) / sizeof(accepted[0]); index++) {
            if (kb_IsDown(accepted[index])) {
                kb_lkey_t key = accepted[index];
                wait_for_release();
                return key;
            }
        }
    }
}

static void draw_dialog(const char *title,
                        const char *const *options,
                        uint8_t count,
                        uint8_t selected) {
    uint8_t index;
    int height = 38 + count * 18;
    int top = (240 - height) / 2;

    gfx_BlitScreen();
    gfx_SetColor(COLOR_PANEL);
    gfx_FillRectangle_NoClip(55, top, 210, height);
    gfx_SetColor(COLOR_BLUE);
    gfx_Rectangle_NoClip(55, top, 210, height);
    gfx_SetTextFGColor(COLOR_TEXT);
    gfx_PrintStringXY(title, 68, top + 10);
    for (index = 0; index < count; index++) {
        int y = top + 29 + index * 18;
        if (index == selected) {
            gfx_SetColor(COLOR_BLUE);
            gfx_FillRectangle_NoClip(65, y - 3, 190, 15);
            gfx_SetTextFGColor(COLOR_PANEL);
        } else {
            gfx_SetTextFGColor(COLOR_TEXT);
        }
        gfx_PrintStringXY(options[index], 73, y);
    }
    gfx_BlitBuffer();
}

AppAction ce_ui_show_menu(void) {
    static const char *const options[] = {
        "New project", "Save project", "Load project", "Toggle 2D / 3D", "Quit"
    };
    static const AppAction actions[] = {
        APP_ACTION_NEW_PROJECT,
        APP_ACTION_SAVE_PROJECT,
        APP_ACTION_LOAD_PROJECT,
        APP_ACTION_TOGGLE_GRAPH_MODE,
        APP_ACTION_QUIT
    };
    uint8_t selected = 0;

    for (;;) {
        kb_lkey_t key;
        draw_dialog("Project menu", options, 5, selected);
        key = wait_for_key();
        if (key == kb_KeyUp && selected > 0) {
            selected--;
        } else if (key == kb_KeyDown && selected < 4) {
            selected++;
        } else if (key == kb_KeyEnter) {
            return actions[selected];
        } else if (key == kb_KeyMode) {
            return APP_ACTION_QUIT;
        } else if (key == kb_KeyClear) {
            return APP_ACTION_NONE;
        }
    }
}

CeDirtyChoice ce_ui_confirm_dirty(const char *operation) {
    static const char *const options[] = {"Save", "Discard", "Cancel"};
    uint8_t selected = 2;

    for (;;) {
        kb_lkey_t key;
        draw_dialog(operation, options, 3, selected);
        key = wait_for_key();
        if (key == kb_KeyUp && selected > 0) {
            selected--;
        } else if (key == kb_KeyDown && selected < 2) {
            selected++;
        } else if (key == kb_KeyEnter) {
            return selected == 0 ? CE_DIRTY_SAVE
                                 : selected == 1 ? CE_DIRTY_DISCARD
                                                 : CE_DIRTY_CANCEL;
        } else if (key == kb_KeyMode) {
            return CE_DIRTY_QUIT;
        } else if (key == kb_KeyClear) {
            return CE_DIRTY_CANCEL;
        }
    }
}

AppAction ce_ui_show_message(const char *message) {
    static const char *const options[] = {"OK"};
    draw_dialog(message, options, 1, 0);
    return wait_for_key() == kb_KeyMode ? APP_ACTION_QUIT : APP_ACTION_NONE;
}

void ce_ui_end(void) {
    gfx_End();
}
