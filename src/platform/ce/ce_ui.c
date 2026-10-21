#include "platform/ce/ce_ui.h"
#include "graph/project_plot2d.h"

#include <graphx.h>
#include <keypadc.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>
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

static ProjectPlot2D render_job;
static enum { RENDER_IDLE, RENDER_COARSE, RENDER_WAIT, RENDER_FINE } render_phase;
static bool render_editing;
static CeRenderStatus render_status;
static uint32_t render_generation, job_generation;

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

static void draw_render_header(void) {
    const char *label;
    bool active = render_phase != RENDER_IDLE;
    switch (render_status.state) {
        case CE_RENDER_COARSE: label = "1/3 Coarse"; break;
        case CE_RENDER_RELEASE_KEYS: label = "2/3 Release keys"; break;
        case CE_RENDER_FINE: label = "3/3 Refining"; break;
        case CE_RENDER_PREVIEW:
            label = render_status.manual_reload_required
                        ? "Canceled - preview only" : "2/3 Preview";
            break;
        case CE_RENDER_CANCELED: label = "Canceled - not loaded"; break;
        case CE_RENDER_OUT_OF_DATE: label = "Out of date - press GRAPH/WINDOW"; break;
        case CE_RENDER_ERROR: label = "Unable to load"; break;
        default: label = "Current"; break;
    }
    gfx_SetColor(COLOR_BLUE);
    gfx_FillRectangle_NoClip(0, 0, 320, 24);
    gfx_SetTextFGColor(COLOR_PANEL);
    gfx_PrintStringXY(label, 6, 2);
    if (render_status.state == CE_RENDER_COARSE || render_status.state == CE_RENDER_FINE) {
        gfx_SetTextXY(166, 2);
        gfx_PrintUInt(render_status.completed, 1);
        gfx_PrintStringXY("/", 182, 2);
        gfx_SetTextXY(190, 2);
        gfx_PrintUInt(render_status.total, 1);
    }
    if (render_phase == RENDER_WAIT || render_phase == RENDER_FINE)
        gfx_PrintStringXY("Preview", 6, 13);
    if (active) gfx_PrintStringXY("DEL: Cancel", 226, 13);
}

static void refresh_render_header(void) {
    uint8_t previous = gfx_GetDraw();
    gfx_SetDraw(gfx_screen);
    draw_render_header();
    gfx_SetDraw(gfx_buffer);
    draw_render_header();
    gfx_SetDraw(previous);
}

static void draw_header(const AppState *state) {
    if (state->view != APP_VIEW_EXPRESSIONS && state->mode == GRAPH_MODE_2D) {
        draw_render_header();
        return;
    }
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
        const char *text = project_line_display_text(project, index);
        uint16_t cursor = project_editor_cursor(project);
        bool active = editing && index == project_editing_index(project);
        int y = 30 + row * 23;
        const char *error;
        uint8_t characters;
        uint16_t text_start = 0;

        if (index >= PROJECT_MAX_EXPRESSIONS) {
            break;
        }
        error = status_name(project_line_status(project, index));
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
        if (active && cursor >= characters) {
            text_start = (uint16_t)(cursor - characters + 1);
        }
        print_clipped(&text[text_start],
                      27,
                      y + 2,
                      characters);
        if (active) {
            char prefix[41];
            uint16_t prefix_length =
                (uint16_t)(cursor - text_start);
            int caret_x;

            memcpy(prefix, &text[text_start], prefix_length);
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

static void draw_3d_panel(int x, int width) {
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
    gfx_Line(center_x, 122, x + width - 18, 52);
    gfx_SetColor(COLOR_PANEL);
    gfx_FillRectangle_NoClip(center_x - 55, 91, 110, 42);
    gfx_SetTextFGColor(COLOR_TEXT);
    gfx_PrintStringXY("3D graph",
                      center_x - 28,
                      100);
    gfx_SetTextFGColor(COLOR_MUTED);
    gfx_PrintStringXY("plotting later", center_x - 48, 117);
}

static void draw_tick(float value, int x, int y, GraphRect rect) {
    char text[8], reversed[6];
    unsigned long number;
    uint8_t count = 0, length = 0;
    if (!isfinite(value) || value < -999999 || value > 999999 ||
        floorf(value) != value) return;
    if (value < 0) text[length++] = '-';
    number = (unsigned long)fabsf(value);
    do {
        reversed[count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number);
    while (count) text[length++] = reversed[--count];
    text[length] = '\0';
    if (x >= rect.x && x + (int)gfx_GetStringWidth(text) <= rect.x + rect.width &&
        y >= rect.y && y + 8 <= rect.y + rect.height)
        gfx_PrintStringXY(text, x, y);
}

static void draw_graph_panel(GraphRect rect, const Viewport2D *viewport) {
    float step, min_x, min_y, unused, first, sx, sy, axis_x, axis_y;
    int tick_x, tick_y;
    unsigned i;
    gfx_SetColor(COLOR_BACKGROUND);
    gfx_FillRectangle_NoClip(rect.x, rect.y, rect.width, rect.height);
    if (!isfinite(viewport->center_x) || !isfinite(viewport->center_y) ||
        !isfinite(viewport->units_per_pixel) ||
        viewport->units_per_pixel < VIEWPORT2D_MIN_SCALE ||
        viewport->units_per_pixel > VIEWPORT2D_MAX_SCALE) return;
    step = viewport2d_grid_step(viewport);
    if (!isfinite(step) || step <= 0) return;
    viewport2d_world_to_screen(viewport, rect, 0, 0, &axis_x, &axis_y);
    tick_x = rect.x + 2;
    tick_y = rect.y + rect.height - 8;
    if (isfinite(axis_x) && axis_x >= rect.x && axis_x < rect.x + rect.width)
        tick_x = (int)axis_x + 3;
    if (isfinite(axis_y) && axis_y >= rect.y && axis_y < rect.y + rect.height - 11)
        tick_y = (int)axis_y + 3;
    viewport2d_screen_to_world(viewport, rect, rect.x, rect.y + rect.height - 1,
                               &min_x, &min_y);
    gfx_SetColor(COLOR_GRID);
    gfx_SetTextFGColor(COLOR_MUTED);
    first = ceilf(min_x / step) * step;
    for (i = 0; i < (unsigned)rect.width / 20 + 2; i++) {
        float value = first + i * step;
        viewport2d_world_to_screen(viewport, rect, value, 0, &sx, &unused);
        if (!isfinite(sx) || sx < rect.x || sx > rect.x + rect.width - 1) continue;
        gfx_VertLine_NoClip((int)floorf(sx + 0.5f), rect.y, rect.height);
        draw_tick(value, (int)floorf(sx + 0.5f) + 2, tick_y, rect);
    }
    first = ceilf(min_y / step) * step;
    for (i = 0; i < (unsigned)rect.height / 20 + 2; i++) {
        float value = first + i * step;
        viewport2d_world_to_screen(viewport, rect, 0, value, &unused, &sy);
        if (!isfinite(sy) || sy < rect.y || sy > rect.y + rect.height - 1) continue;
        gfx_HorizLine_NoClip(rect.x, (int)floorf(sy + 0.5f), rect.width);
        if (value != 0) draw_tick(value, tick_x, (int)floorf(sy + 0.5f) + 2, rect);
    }
    gfx_SetColor(COLOR_MUTED);
    if (isfinite(axis_x) && axis_x >= rect.x && axis_x <= rect.x + rect.width - 1)
        gfx_VertLine_NoClip((int)floorf(axis_x + 0.5f), rect.y, rect.height);
    if (isfinite(axis_y) && axis_y >= rect.y && axis_y <= rect.y + rect.height - 1)
        gfx_HorizLine_NoClip(rect.x, (int)floorf(axis_y + 0.5f), rect.width);
}

void ce_ui_begin(void) {
    render_generation++;
    project_plot2d_cancel(&render_job);
    render_phase = RENDER_IDLE;
    render_status = (CeRenderStatus){CE_RENDER_CURRENT, 0, 0, false};
    gfx_Begin();
    gfx_SetDrawBuffer();
    configure_palette();
    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(COLOR_TEXT);
    gfx_SetTextBGColor(255);
    gfx_SetTextTransparentColor(255);
}

static void draw_static_frame(const AppState *state, const Project *project,
                               bool editing) {
    gfx_FillScreen(COLOR_BACKGROUND);
    draw_header(state);
    if (state->view == APP_VIEW_EXPRESSIONS) {
        draw_expression_list(state, project, 320, editing);
    } else if (state->view == APP_VIEW_SPLIT) {
        draw_expression_list(state, project, 132, editing);
    }
    if (state->view != APP_VIEW_EXPRESSIONS) {
        GraphRect rect = {state->view == APP_VIEW_SPLIT ? 132 : 0, 24,
                          state->view == APP_VIEW_SPLIT ? 188 : 320, 196};
        if (state->mode == GRAPH_MODE_2D) draw_graph_panel(rect, &state->viewport);
        else draw_3d_panel(rect.x, rect.width);
    }
    draw_key_bar(state);
}

void ce_ui_draw(const AppState *state, const Project *project, bool editing,
                bool load_graph) {
    render_generation++;
    project_plot2d_cancel(&render_job);
    render_phase = RENDER_IDLE;
    render_editing = editing;
    render_status = (CeRenderStatus){CE_RENDER_OUT_OF_DATE, 0, 0,
                                     !load_graph && render_status.manual_reload_required};
    if (load_graph && state->mode == GRAPH_MODE_2D && state->view != APP_VIEW_EXPRESSIONS) {
        GraphRect rect = {state->view == APP_VIEW_SPLIT ? 132 : 0, 24,
                          state->view == APP_VIEW_SPLIT ? 188 : 320, 196};
        project_plot2d_begin(&render_job, project, state->viewport, rect, PLOT2D_COARSE_STEP);
        job_generation = render_generation;
        render_status.total = project_plot2d_total(&render_job);
        render_status.state = render_job.state == PROJECT_PLOT2D_ERROR
                                  ? CE_RENDER_ERROR : CE_RENDER_COARSE;
        if (render_status.state != CE_RENDER_ERROR) render_phase = RENDER_COARSE;
    }
    draw_static_frame(state, project, editing);
    gfx_SwapDraw();
    if (state->mode == GRAPH_MODE_2D && state->view != APP_VIEW_EXPRESSIONS)
        draw_static_frame(state, project, editing);
}

void ce_ui_refresh(const AppState *state, const Project *project, bool editing) {
    uint8_t previous = gfx_GetDraw();
    uint8_t role;
    render_editing = editing;
    for (role = gfx_screen; role <= gfx_buffer; role++) {
        gfx_SetDraw(role);
        draw_header(state);
        if (state->view != APP_VIEW_GRAPH)
            draw_expression_list(state, project,
                                  state->view == APP_VIEW_SPLIT ? 132 : 320, editing);
    }
    gfx_SetDraw(previous);
}

bool ce_ui_cancel_render(void) {
    if (render_phase == RENDER_IDLE) return false;
    render_status.state = render_phase == RENDER_COARSE ? CE_RENDER_CANCELED : CE_RENDER_PREVIEW;
    render_status.manual_reload_required = true;
    render_generation++;
    project_plot2d_cancel(&render_job);
    render_phase = RENDER_IDLE;
    refresh_render_header();
    return true;
}

CeRenderStatus ce_ui_render_status(void) {
    return render_status;
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

static void draw_segment(void *user, uint8_t index, int16_t x0, int16_t y0,
                          int16_t x1, int16_t y1) {
    static const uint8_t colors[] = {COLOR_BLUE, COLOR_RED, COLOR_GREEN};
    if (*(const uint32_t *)user != render_generation) return;
    gfx_SetColor(colors[index % sizeof(colors)]);
    gfx_Line(x0, y0, x1, y1);
}

bool ce_ui_render_step(const AppState *state, const Project *project,
                       bool navigation_active) {
    ProjectPlot2DStep result;
    uint32_t generation = job_generation;
    uint8_t completed = render_status.completed;
    if (render_phase == RENDER_IDLE) return false;
    if (generation != render_generation) return false;
    if (render_phase == RENDER_WAIT) {
        if (navigation_active) {
            if (render_status.state != CE_RENDER_RELEASE_KEYS) {
                render_status.state = CE_RENDER_RELEASE_KEYS;
                refresh_render_header();
            }
            return true;
        }
        project_plot2d_begin(&render_job, project, state->viewport, render_job.job.rect,
                             PLOT2D_FINE_STEP);
        render_phase = RENDER_FINE;
        render_status.state = CE_RENDER_FINE;
        render_status.completed = 0;
        render_status.total = project_plot2d_total(&render_job);
        if (render_job.state == PROJECT_PLOT2D_ERROR) {
            render_phase = RENDER_IDLE;
            render_status.state = CE_RENDER_ERROR;
            refresh_render_header();
            return false;
        }
        draw_static_frame(state, project, render_editing);
        refresh_render_header();
        /* Starting refinement is bounded separately; its first row waits for input. */
        return true;
    }
    result = project_plot2d_step(&render_job, project, draw_segment, &generation);
    if (generation != render_generation) return false;
    render_status.completed = project_plot2d_completed(&render_job);
    if (result == PROJECT_PLOT2D_PROGRESS) {
        if (completed != render_status.completed) refresh_render_header();
        return true;
    }
    render_phase = result == PROJECT_PLOT2D_DONE && render_phase == RENDER_COARSE
                       ? RENDER_WAIT : RENDER_IDLE;
    render_status.state = result == PROJECT_PLOT2D_ERROR ? CE_RENDER_ERROR
                          : render_phase == RENDER_WAIT
                              ? (navigation_active ? CE_RENDER_RELEASE_KEYS : CE_RENDER_PREVIEW)
                              : CE_RENDER_CURRENT;
    if (result == PROJECT_PLOT2D_DONE) {
        /* Publish the completed geometry and its new label in the same swap. */
        draw_render_header();
        gfx_SwapDraw();
    }
    refresh_render_header();
    return render_phase != RENDER_IDLE;
}

static void wait_for_release(void) {
    do {
        kb_Scan();
    } while (any_key_down());
    kb_SetMode(MODE_3_CONTINUOUS);
}

static kb_lkey_t wait_for_key(bool accept_menu_key) {
    static const kb_lkey_t accepted[] = {
        kb_KeyUp, kb_KeyDown, kb_KeyLeft, kb_KeyRight,
        kb_KeyEnter, kb_KeyClear
    };
    bool second = false;
    size_t index;

    wait_for_release();
    for (;;) {
        kb_Scan();
        if (accept_menu_key && kb_IsDown(kb_KeyTrace)) {
            wait_for_release();
            return kb_KeyTrace;
        }
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
        key = wait_for_key(true);
        if (key == kb_KeyUp && selected > 0) {
            selected--;
        } else if (key == kb_KeyDown && selected < 4) {
            selected++;
        } else if (key == kb_KeyEnter) {
            return actions[selected];
        } else if (key == kb_KeyMode) {
            return APP_ACTION_QUIT;
        } else if (key == kb_KeyTrace || key == kb_KeyClear) {
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
        key = wait_for_key(false);
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
    return wait_for_key(false) == kb_KeyMode ? APP_ACTION_QUIT
                                              : APP_ACTION_NONE;
}

void ce_ui_end(void) {
    render_generation++;
    project_plot2d_cancel(&render_job);
    render_phase = RENDER_IDLE;
    gfx_End();
}
