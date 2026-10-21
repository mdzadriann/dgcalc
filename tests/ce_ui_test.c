/* Like the scheduler tests, compile an observed copy to inject a stale stamp
   without exposing test controls in the production API. */
#define ce_ui_begin observed_ui_begin
#define ce_ui_draw observed_ui_draw
#define ce_ui_refresh observed_ui_refresh
#define ce_ui_cancel_render observed_ui_cancel_render
#define ce_ui_render_status observed_ui_render_status
#define ce_ui_render_step observed_ui_render_step
#define ce_ui_show_menu observed_ui_show_menu
#define ce_ui_confirm_dirty observed_ui_confirm_dirty
#define ce_ui_show_message observed_ui_show_message
#define ce_ui_end observed_ui_end
#include "platform/ce/ce_ui.h"
#include "test.h"

#include <graphx.h>
#include <keypadc.h>

#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "fake_ce.h"
#include "../src/platform/ce/ce_ui.c"

static void commit_graph_line(Project *project, uint8_t index, const char *text) {
    ASSERT_TRUE(project_set_draft(project, index, text));
    ASSERT_TRUE(project_commit_line(project, index).ok);
}

static size_t segment_count(void) {
    size_t i, count = 0;
    for (i = 0; i < graph_line_count; i++)
        if (graph_lines[i].segment) count++;
    return count;
}

static bool has_base_line(int x0, int y0, int x1, int y1, uint8_t color) {
    size_t i;
    for (i = 0; i < graph_line_count; i++) {
        TestLine *line = &graph_lines[i];
        if (!line->segment && line->x0 == x0 && line->y0 == y0 &&
            line->x1 == x1 && line->y1 == y1 && line->color == color)
            return true;
    }
    return false;
}

/* Catches fixed-grid placeholders, misplaced axes and labels crossing a pane. */
static void test_graph_base_tracks_viewport_in_both_layouts(void) {
    AppState state;
    Project project;
    int split, moved;
    project_init(&project);
    for (split = 0; split <= 1; split++) {
        for (moved = 0; moved <= 1; moved++) {
            int left = split ? 132 : 0;
            int center = split ? 226 : 160;
            size_t i, labels = 0, origin_labels = 0;
            reset_fake_hardware();
            ce_ui_begin();
            app_state_init(&state);
            state.view = split ? APP_VIEW_SPLIT : APP_VIEW_GRAPH;
            if (moved) state.viewport = (Viewport2D){1, -1, 0.125f};
            ce_ui_draw(&state, &project, false, true);
            /* Default spacing is 2 units/32px; zoomed spacing is 5/40px. */
            ASSERT_TRUE(has_base_line(center + 32, 24,
                                      center + 32, 219, 6));
            ASSERT_TRUE(has_base_line(left, moved ? 74 : 90, 319,
                                      moved ? 74 : 90, 6));
            ASSERT_TRUE(has_base_line(center - (moved ? 8 : 0), 24,
                                      center - (moved ? 8 : 0), 219, 3));
            ASSERT_TRUE(has_base_line(left, moved ? 114 : 122, 319,
                                      moved ? 114 : 122, 3));
            for (i = 0; i < graph_print_count; i++) {
                TestPrint *p = &graph_prints[i];
                char *end;
                if (p->y < 24 || p->x < left) continue;
                (void)strtol(p->text, &end, 10);
                if (!p->text[0] || *end) continue;
                labels++;
                if (p->frame == 1 && strcmp(p->text, "0") == 0) origin_labels++;
                ASSERT_TRUE(p->x >= left);
                ASSERT_TRUE(p->x + gfx_GetStringWidth(p->text) <= 320);
                ASSERT_TRUE(p->y >= 24 && p->y + 8 <= 220);
            }
            ASSERT_TRUE(labels > 0);
            ASSERT_EQ_INT(1, origin_labels);
        }
    }
    project_dispose(&project);
}

/* Vertical equalities expose multiple rows per call without scheduler spies. */
static void test_progressive_rows_colors_navigation_and_swaps(void) {
    AppState state;
    Project project;
    size_t calls, i, before;
    bool active = true;
    bool colors[8] = {false};
    reset_fake_hardware();
    ce_ui_begin();
    app_state_init(&state);
    state.view = APP_VIEW_GRAPH;
    project_init(&project);
    commit_graph_line(&project, 0, "x=2");
    commit_graph_line(&project, 1, "x=-2");
    ce_ui_draw(&state, &project, false, true);
    ASSERT_EQ_INT(1, swaps);
    ASSERT_EQ_INT(0, segment_count());
    for (calls = 0; swaps == 1 && calls < 100; calls++) {
        before = graph_line_count;
        ASSERT_TRUE(ce_ui_render_step(&state, &project, true));
        ASSERT_TRUE(graph_line_count - before <= 1);
        for (i = before; i < graph_line_count; i++) {
            TestLine *line = &graph_lines[i];
            ASSERT_TRUE(line->segment);
            ASSERT_TRUE(line->x0 == line->x1);
            ASSERT_TRUE(line->y1 - line->y0 <= 8);
            ASSERT_TRUE(line->x0 >= 0 && line->x1 < 320);
            ASSERT_TRUE(line->y0 >= 24 && line->y1 < 220);
            ASSERT_TRUE(line->color == 4 || line->color == 5);
            colors[line->color] = true;
        }
    }
    ASSERT_EQ_INT(51, calls);
    ASSERT_EQ_INT(2, swaps);
    ASSERT_EQ_INT(50, segment_count());
    ASSERT_TRUE(colors[4] && colors[5]);
    ASSERT_EQ_INT(2, swapped_frames[1]);
    before = graph_line_count;
    for (i = 0; i < 5; i++) ASSERT_TRUE(ce_ui_render_step(&state, &project, true));
    ASSERT_EQ_INT(before, graph_line_count);
    ASSERT_EQ_INT(2, swaps);
    ASSERT_TRUE(ce_ui_render_step(&state, &project, false));
    ASSERT_EQ_INT(50, segment_count()); /* Fine begins without processing a row. */
    for (calls = 0; active && calls < 120; calls++) {
        before = graph_line_count;
        active = ce_ui_render_step(&state, &project, false);
        ASSERT_TRUE(graph_line_count - before <= 1);
        for (i = before; i < graph_line_count; i++)
            ASSERT_TRUE(graph_lines[i].y1 - graph_lines[i].y0 <= 4);
    }
    ASSERT_FALSE(active);
    ASSERT_EQ_INT(99, calls);
    ASSERT_EQ_INT(3, swaps);
    ASSERT_EQ_INT(3, swapped_frames[2]);
    before = graph_line_count;
    for (i = 0; i < 5; i++) ASSERT_FALSE(ce_ui_render_step(&state, &project, false));
    ASSERT_EQ_INT(before, graph_line_count);
    ASSERT_EQ_INT(3, swaps);
    project_dispose(&project);
}

static void test_redraw_cancels_pending_geometry_and_modal_work(void) {
    static const kb_lkey_t close_keys[] = {0, kb_KeyTrace, 0};
    AppState state;
    Project project;
    size_t i, before;
    reset_fake_hardware();
    ce_ui_begin();
    app_state_init(&state);
    state.view = APP_VIEW_GRAPH;
    project_init(&project);
    commit_graph_line(&project, 0, "x=2");
    ce_ui_draw(&state, &project, false, true);
    ASSERT_TRUE(ce_ui_render_step(&state, &project, true));
    before = graph_line_count;
    script_keys(close_keys, 3);
    ASSERT_EQ_INT(APP_ACTION_NONE, ce_ui_show_menu());
    ASSERT_EQ_INT(before, graph_line_count);
    ASSERT_EQ_INT(1, swaps);
    ASSERT_EQ_INT(1, screen_blits);
    ASSERT_EQ_INT(1, buffer_blits);
    ASSERT_TRUE(state.viewport.center_x == 0);
    state.viewport.center_x = 2;
    ce_ui_draw(&state, &project, false, true);
    ASSERT_EQ_INT(2, swaps);
    before = graph_line_count;
    for (i = 0; swaps == 2 && i < 40; i++)
        ASSERT_TRUE(ce_ui_render_step(&state, &project, true));
    ASSERT_EQ_INT(3, swaps);
    ASSERT_EQ_INT(4, swapped_frames[2]);
    for (i = before; i < graph_line_count; i++)
        ASSERT_EQ_INT(160, graph_lines[i].x0);
    ASSERT_TRUE(ce_ui_render_step(&state, &project, false));
    ASSERT_TRUE(ce_ui_render_step(&state, &project, false));
    state.view = APP_VIEW_SPLIT;
    ce_ui_draw(&state, &project, false, true);
    before = graph_line_count;
    for (i = 0; swaps == 4 && i < 40; i++)
        ASSERT_TRUE(ce_ui_render_step(&state, &project, true));
    ASSERT_EQ_INT(5, swaps);
    for (i = before; i < graph_line_count; i++) {
        ASSERT_TRUE(graph_lines[i].x0 >= 132 && graph_lines[i].x1 < 320);
        ASSERT_EQ_INT(7, graph_lines[i].frame);
    }
    /* A project replacement must discard the old line, including during fine. */
    ASSERT_TRUE(ce_ui_render_step(&state, &project, false));
    project_dispose(&project);
    ce_ui_draw(&state, &project, false, true);
    before = segment_count();
    for (i = 0; i < 10 && ce_ui_render_step(&state, &project, false); i++) {}
    ASSERT_TRUE(i < 10);
    ASSERT_EQ_INT(before, segment_count());
    ASSERT_EQ_INT(8, swaps);
    state.mode = GRAPH_MODE_3D;
    ce_ui_draw(&state, &project, false, true);
    before = graph_line_count;
    ASSERT_FALSE(ce_ui_render_step(&state, &project, false));
    ASSERT_EQ_INT(before, graph_line_count);
    state.view = APP_VIEW_EXPRESSIONS;
    ce_ui_draw(&state, &project, false, true);
    before = graph_line_count;
    ASSERT_FALSE(ce_ui_render_step(&state, &project, false));
    ASSERT_EQ_INT(before, graph_line_count);
    ce_ui_begin();
    ASSERT_FALSE(ce_ui_render_step(&state, &project, false));
    project_dispose(&project);
}

static void test_empty_and_nonfinite_graphs_finish_without_geometry(void) {
    static const Viewport2D views[] = {
        {0, 0, 0.0625f}, {INFINITY, 0, 0.0625f}, {0, 0, 0},
        {0, 0, NAN}, {1e30f, -1e30f, 0.0001f}
    };
    AppState state;
    Project project;
    size_t v, i;
    project_init(&project);
    for (v = 0; v < sizeof(views)/sizeof(views[0]); v++) {
        reset_fake_hardware();
        ce_ui_begin();
        app_state_init(&state);
        state.view = APP_VIEW_GRAPH;
        state.viewport = views[v];
        ce_ui_draw(&state, &project, false, true);
        for (i = 0; i < 10 && ce_ui_render_step(&state, &project, false); i++) {}
        ASSERT_TRUE(i < 10);
        ASSERT_EQ_INT(0, segment_count());
        ASSERT_EQ_INT(v == 0 || v == 4 ? 3 : 1, swaps);
        ASSERT_FALSE(ce_ui_render_step(&state, &project, false));
    }
    project_dispose(&project);
}

static void test_every_menu_selection_preserves_normal_draw_buffer_roles(void) {
    static const AppAction expected_actions[] = {
        APP_ACTION_NEW_PROJECT,
        APP_ACTION_SAVE_PROJECT,
        APP_ACTION_LOAD_PROJECT,
        APP_ACTION_TOGGLE_GRAPH_MODE,
        APP_ACTION_QUIT
    };
    AppState state;
    Project project;
    size_t selection;

    for (selection = 0; selection < 5; selection++) {
        kb_lkey_t keys[15];
        size_t key_count = 0;
        size_t down;
        size_t frame;

        for (down = 0; down < selection; down++) {
            keys[key_count++] = 0;
            keys[key_count++] = kb_KeyDown;
            keys[key_count++] = 0;
        }
        keys[key_count++] = 0;
        keys[key_count++] = kb_KeyEnter;
        keys[key_count++] = 0;

        reset_fake_hardware();
        ce_ui_begin();
        app_state_init(&state);
        state.view = APP_VIEW_GRAPH;
        project_init(&project);
        commit_graph_line(&project, 0, "y=x");
        ce_ui_draw(&state, &project, false, true);
        script_keys(keys, key_count);
        ASSERT_EQ_INT(expected_actions[selection], ce_ui_show_menu());
        ASSERT_EQ_INT(selection + 1, screen_blits);
        ASSERT_EQ_INT(selection + 1, buffer_blits);
        ASSERT_EQ_INT(1, swaps);
        ASSERT_EQ_INT(0, segment_count());
        ASSERT_TRUE(state.viewport.center_x == 0 && state.viewport.center_y == 0);
        ASSERT_EQ_INT(MODE_3_CONTINUOUS, kb_GetMode());
        ASSERT_EQ_INT((selection + 1) * 3 + 1, draw_operation_count);
        for (frame = 0; frame <= selection; frame++) {
            ASSERT_EQ_INT(TEST_COPY_SCREEN_TO_BUFFER,
                          draw_operations[frame * 3 + 1]);
            ASSERT_EQ_INT(TEST_DRAW_DIALOG,
                          draw_operations[frame * 3 + 2]);
            ASSERT_EQ_INT(TEST_COPY_BUFFER_TO_SCREEN,
                          draw_operations[frame * 3 + 3]);
        }

        app_state_init(&state);
        ce_ui_draw(&state, &project, false, true);
        ASSERT_EQ_INT(TEST_SWAP_BUFFERS,
                      draw_operations[draw_operation_count - 1]);
        project_dispose(&project);
    }
}

static void test_trace_closes_the_project_menu_without_an_action(void) {
    static const kb_lkey_t keys[] = {
        0, kb_KeyTrace, 0,
        0, kb_KeyClear, 0
    };

    reset_fake_hardware();
    script_keys(keys, sizeof(keys) / sizeof(keys[0]));

    ASSERT_EQ_INT(APP_ACTION_NONE, ce_ui_show_menu());
    ASSERT_EQ_INT(3, scan_key_index);
    ASSERT_EQ_INT(MODE_3_CONTINUOUS, kb_GetMode());
    ASSERT_EQ_INT(APP_ACTION_NONE, ce_ui_show_menu());
    ASSERT_EQ_INT(6, scan_key_index);
}

static void test_footer_uses_five_non_overlapping_key_slots(void) {
    static const char *const expected[] = {
        "Expr", "Split", "2D/3D", "Menu", "Graph"
    };
    AppState state;
    Project project;
    size_t index;

    reset_fake_hardware();
    app_state_init(&state);
    project_init(&project);
    ce_ui_draw(&state, &project, false, true);

    ASSERT_EQ_INT(5, footer_print_count);
    for (index = 0; index < 5 && index < footer_print_count; index++) {
        int right = footer_prints[index].x +
                    gfx_GetStringWidth(footer_prints[index].text);

        ASSERT_STR_EQ(expected[index], footer_prints[index].text);
        ASSERT_TRUE(footer_prints[index].x >= (int)index * 64);
        ASSERT_TRUE(right <= (int)(index + 1) * 64);
    }
    project_dispose(&project);
}

static void test_footer_highlights_the_active_page(void) {
    static const AppView views[] = {
        APP_VIEW_EXPRESSIONS, APP_VIEW_SPLIT, APP_VIEW_GRAPH
    };
    static const int expected_left[] = {1, 65, 257};
    static const size_t expected_label[] = {0, 1, 4};
    AppState state;
    Project project;
    size_t view_index;
    size_t label;

    for (view_index = 0; view_index < 3; view_index++) {
        reset_fake_hardware();
        ce_ui_begin();
        app_state_init(&state);
        state.view = views[view_index];
        project_init(&project);
        ce_ui_draw(&state, &project, false, true);

        ASSERT_EQ_INT(1, footer_highlight_count);
        ASSERT_EQ_INT(expected_left[view_index], footer_highlights[0]);
        ASSERT_TRUE(footer_highlight_colors[0] != 0);
        ASSERT_EQ_INT(5, footer_print_count);
        for (label = 0; label < 5; label++) {
            if (label == expected_label[view_index]) {
                ASSERT_TRUE(footer_prints[label].color !=
                            footer_prints[(label + 1) % 5].color);
                ASSERT_TRUE(footer_prints[label].color !=
                            transparent_text_color);
            }
        }
        project_dispose(&project);
    }
}

static void assert_status(CeRenderState state, unsigned completed, unsigned total,
                          bool manual) {
    CeRenderStatus status = ce_ui_render_status();
    ASSERT_EQ_INT(state, status.state);
    ASSERT_EQ_INT(completed, status.completed);
    ASSERT_EQ_INT(total, status.total);
    ASSERT_EQ_INT(manual, status.manual_reload_required);
}

static bool visible_header_has(const char *text) {
    return strstr(header_text[visible_buffer][0], text) ||
           strstr(header_text[visible_buffer][1], text);
}

/* Catches coarse pixels mislabeled current and counters advancing per row. */
static void test_render_status_tracks_passes_and_equations(void) {
    AppState state;
    Project project;
    int row;
    size_t print;
    reset_fake_hardware();
    ce_ui_begin();
    app_state_init(&state);
    state.view = APP_VIEW_GRAPH;
    project_init(&project);
    commit_graph_line(&project, 0, "x=2");
    commit_graph_line(&project, 1, "x=-2");
    commit_graph_line(&project, 2, "a=5");
    ce_ui_draw(&state, &project, false, true);
    assert_status(CE_RENDER_COARSE, 0, 2, false);
    ASSERT_TRUE(visible_header_has("1/3 Coarse"));
    ASSERT_TRUE(visible_header_has("0 / 2"));
    ASSERT_TRUE(visible_header_has("DEL: Cancel"));
    for (row = 0; row < 25; row++) {
        ASSERT_TRUE(ce_ui_render_step(&state, &project, true));
        ASSERT_EQ_INT(1, swaps);
    }
    assert_status(CE_RENDER_COARSE, 1, 2, false);
    ASSERT_TRUE(visible_header_has("1 / 2"));
    for (; row < 50; row++) {
        ASSERT_TRUE(ce_ui_render_step(&state, &project, true));
        ASSERT_EQ_INT(1, swaps);
    }
    ASSERT_TRUE(ce_ui_render_step(&state, &project, true));
    assert_status(CE_RENDER_RELEASE_KEYS, 2, 2, false);
    ASSERT_EQ_INT(2, swaps);
    ASSERT_TRUE(visible_header_has("2/3 Release keys"));
    ASSERT_TRUE(visible_header_has("Preview"));
    ASSERT_TRUE(visible_header_has("DEL: Cancel"));
    ASSERT_TRUE(ce_ui_render_step(&state, &project, false));
    assert_status(CE_RENDER_FINE, 0, 2, false);
    ASSERT_TRUE(visible_header_has("3/3 Refining"));
    for (row = 0; row < 98; row++) {
        ASSERT_TRUE(ce_ui_render_step(&state, &project, false));
        ASSERT_EQ_INT(2, swaps);
        if (row == 48) {
            assert_status(CE_RENDER_FINE, 1, 2, false);
            ASSERT_TRUE(visible_header_has("1 / 2"));
        }
    }
    ASSERT_FALSE(ce_ui_render_step(&state, &project, false));
    assert_status(CE_RENDER_CURRENT, 2, 2, false);
    ASSERT_EQ_INT(3, swaps);
    ASSERT_TRUE(visible_header_has("Current"));
    ASSERT_FALSE(visible_header_has("DEL: Cancel"));
    ASSERT_FALSE(visible_header_has("Refining"));
    for (print = 0; print < graph_print_count; print++) {
        TestPrint *p = &graph_prints[print];
        if (p->visible && strcmp(p->text, "Current") == 0)
            ASSERT_EQ_INT(3, p->swaps);
        if (p->y < 24) ASSERT_TRUE(p->x + gfx_GetStringWidth(p->text) <= 320);
    }
    ASSERT_FALSE(ce_ui_cancel_render());
    project_dispose(&project);
}

/* Catches cancel leaking another row, swapping a partial frame, or auto-restart. */
static void test_cancel_preserves_only_completed_coarse_geometry(void) {
    AppState state;
    Project project;
    int phase, row;
    size_t before;
    static uint8_t visible_graph[196][320];
    project_init(&project);
    commit_graph_line(&project, 0, "x=2");
    for (phase = 0; phase < 3; phase++) {
        reset_fake_hardware();
        ce_ui_begin();
        app_state_init(&state);
        state.view = APP_VIEW_GRAPH;
        ce_ui_draw(&state, &project, false, true);
        ASSERT_TRUE(ce_ui_render_step(&state, &project, false));
        if (phase) {
            for (row = 1; row < 26; row++)
                ASSERT_TRUE(ce_ui_render_step(&state, &project, false));
            assert_status(CE_RENDER_PREVIEW, 1, 1, false);
            ASSERT_TRUE(visible_header_has("Preview"));
        }
        if (phase == 2) {
            ASSERT_TRUE(ce_ui_render_step(&state, &project, false));
            ASSERT_TRUE(ce_ui_render_step(&state, &project, false));
        }
        memcpy(visible_graph, &buffer_pixels[visible_buffer][24], sizeof(visible_graph));
        before = segment_count();
        ASSERT_TRUE(ce_ui_cancel_render());
        ASSERT_FALSE(ce_ui_cancel_render());
        ce_ui_refresh(&state, &project, false);
        assert_status(phase ? CE_RENDER_PREVIEW : CE_RENDER_CANCELED,
                      phase == 1 ? 1 : 0, 1, true);
        ASSERT_TRUE(visible_header_has("Canceled - GRAPH/WINDOW to load"));
        ASSERT_EQ_INT(phase != 0, visible_header_has("Preview"));
        ASSERT_FALSE(visible_header_has("DEL: Cancel"));
        for (row = 0; row < 110; row++)
            ASSERT_FALSE(ce_ui_render_step(&state, &project, false));
        ASSERT_EQ_INT(before, segment_count());
        ASSERT_EQ_INT(phase ? 2 : 1, swaps);
        ASSERT_EQ_INT(0, memcmp(visible_graph, &buffer_pixels[visible_buffer][24], sizeof(visible_graph)));
        state.viewport.center_x = 3;
        ce_ui_draw(&state, &project, false, false);
        assert_status(CE_RENDER_OUT_OF_DATE, 0, 0, true);
        ASSERT_TRUE(visible_header_has("Out of date"));
        ASSERT_FALSE(ce_ui_render_step(&state, &project, false));
        ce_ui_draw(&state, &project, false, true);
        assert_status(CE_RENDER_COARSE, 0, 1, false);
    }
    project_dispose(&project);
}

/* Catches partial refresh clearing either graph or losing editor updates on swap. */
static void test_refresh_mirrors_ui_without_restarting_rows(void) {
    AppState state;
    Project project;
    static uint8_t graphs[2][196][188];
    int b, y, row;
    size_t before, first_print, print;
    bool draft_seen[2] = {false};
    reset_fake_hardware();
    ce_ui_begin();
    app_state_init(&state);
    state.view = APP_VIEW_SPLIT;
    project_init(&project);
    commit_graph_line(&project, 0, "x=2");
    ce_ui_draw(&state, &project, false, true);
    for (row = 0; row < 10; row++) ASSERT_TRUE(ce_ui_render_step(&state, &project, true));
    for (b = 0; b < 2; b++)
        for (y = 0; y < 196; y++) memcpy(graphs[b][y], &buffer_pixels[b][y + 24][132], 188);
    before = segment_count();
    ASSERT_TRUE(project_set_draft(&project, 0, "x=3"));
    state.selected_expression = 1;
    first_print = graph_print_count;
    gfx_SetDraw(gfx_screen);
    ce_ui_refresh(&state, &project, true);
    ASSERT_EQ_INT(gfx_screen, gfx_GetDraw());
    gfx_SetDraw(gfx_buffer);
    ASSERT_EQ_INT(1, swaps);
    ASSERT_EQ_INT(2, static_frames);
    ASSERT_EQ_INT(before, segment_count());
    for (print = first_print; print < graph_print_count; print++) {
        TestPrint *p = &graph_prints[print];
        if (strcmp(p->text, "x=3") == 0) draft_seen[p->buffer] = true;
    }
    ASSERT_TRUE(draft_seen[0] && draft_seen[1]);
    for (b = 0; b < 2; b++)
        for (y = 0; y < 196; y++)
            ASSERT_EQ_INT(0, memcmp(graphs[b][y], &buffer_pixels[b][y + 24][132], 188));
    for (; row < 26; row++) ASSERT_TRUE(ce_ui_render_step(&state, &project, true));
    ASSERT_EQ_INT(2, swaps);
    ASSERT_STR_EQ(header_text[0][0], header_text[1][0]);
    ASSERT_STR_EQ(header_text[0][1], header_text[1][1]);
    ASSERT_TRUE(ce_ui_render_step(&state, &project, false));
    ASSERT_TRUE(ce_ui_render_step(&state, &project, false));
    ce_ui_refresh(&state, &project, false);
    for (row = 1; row < 49; row++) ASSERT_TRUE(ce_ui_render_step(&state, &project, false));
    ASSERT_FALSE(ce_ui_render_step(&state, &project, false));
    ASSERT_EQ_INT(3, swaps);
    assert_status(CE_RENDER_CURRENT, 1, 1, false);
    project_dispose(&project);
}

/* Catches empty work spinning, failed startup reported current, and stale swaps. */
static void test_empty_error_and_replaced_generations(void) {
    AppState state;
    Project project;
    int row, previous_swaps;
    reset_fake_hardware();
    ce_ui_begin();
    app_state_init(&state);
    state.view = APP_VIEW_GRAPH;
    project_init(&project);
    ce_ui_draw(&state, &project, false, true);
    for (row = 0; row < 4 && ce_ui_render_step(&state, &project, false); row++) {}
    assert_status(CE_RENDER_CURRENT, 0, 0, false);
    ASSERT_FALSE(visible_header_has("DEL: Cancel"));
    state.viewport.units_per_pixel = NAN;
    ce_ui_draw(&state, &project, false, true);
    ASSERT_FALSE(ce_ui_render_step(&state, &project, false));
    assert_status(CE_RENDER_ERROR, 0, 0, false);
    ASSERT_TRUE(visible_header_has("Unable to load"));
    app_state_init(&state);
    state.view = APP_VIEW_GRAPH;
    commit_graph_line(&project, 0, "x=2");
    ce_ui_draw(&state, &project, false, true);
    for (row = 0; row < 25; row++) ASSERT_TRUE(ce_ui_render_step(&state, &project, true));
    state.viewport.center_x = 2;
    ce_ui_draw(&state, &project, false, true);
    previous_swaps = swaps;
    ASSERT_TRUE(ce_ui_render_step(&state, &project, false));
    ASSERT_EQ_INT(previous_swaps, swaps);
    ASSERT_TRUE(ce_ui_cancel_render());
    ASSERT_FALSE(ce_ui_render_step(&state, &project, false));
    ASSERT_EQ_INT(previous_swaps, swaps);
    ce_ui_draw(&state, &project, false, true);
    ASSERT_FALSE(ce_ui_render_step(&state, NULL, false));
    assert_status(CE_RENDER_ERROR, 0, 1, false);
    ASSERT_TRUE(visible_header_has("Unable to load"));
    ASSERT_FALSE(visible_header_has("DEL: Cancel"));
    ce_ui_draw(&state, &project, false, false);
    assert_status(CE_RENDER_OUT_OF_DATE, 0, 0, false);
    ASSERT_FALSE(ce_ui_render_step(&state, &project, false));
    project_dispose(&project);
}

/* Catches deletion of the generation gate even though the main loop is serial. */
static void test_obsolete_generation_cannot_draw_or_swap(void) {
    AppState state;
    Project project;
    uint32_t old_generation;
    int action, row, before_swaps;
    size_t before_segments;
    project_init(&project);
    commit_graph_line(&project, 0, "x=2");
    for (action = 0; action < 2; action++) {
        reset_fake_hardware();
        ce_ui_begin();
        app_state_init(&state);
        state.view = APP_VIEW_GRAPH;
        ce_ui_draw(&state, &project, false, true);
        for (row = 0; row < 25; row++) ASSERT_TRUE(ce_ui_render_step(&state, &project, true));
        old_generation = job_generation;
        if (action) ASSERT_TRUE(ce_ui_cancel_render());
        else ce_ui_draw(&state, &project, false, true);
        ASSERT_TRUE(render_generation != old_generation);
        /* Present an obsolete, swap-ready request after the invalidation. */
        job_generation = old_generation;
        render_phase = RENDER_COARSE;
        project_plot2d_cancel(&render_job);
        before_swaps = swaps;
        before_segments = segment_count();
        draw_segment(&old_generation, 0, 1, 24, 1, 32);
        ASSERT_FALSE(ce_ui_render_step(&state, &project, false));
        ASSERT_EQ_INT(before_swaps, swaps);
        ASSERT_EQ_INT(before_segments, segment_count());
    }
    ce_ui_begin();
    project_dispose(&project);
}

static void open_diagnostics(void) {
    static const kb_lkey_t keys[] = {
        0, kb_KeyDown, 0, 0, kb_KeyDown, 0, 0, kb_KeyDown, 0,
        0, kb_KeyDown, 0, 0, kb_KeyDown, 0, 0, kb_KeyEnter, 0,
        0, kb_KeyLeft, 0, 0, kb_KeyRight, 0, 0, kb_KeyUp, 0,
        0, kb_KeyClear, 0
    };
    script_keys(keys, sizeof(keys) / sizeof(keys[0]));
    ASSERT_EQ_INT(APP_ACTION_NONE, ce_ui_show_menu());
    ASSERT_EQ_INT(sizeof(keys) / sizeof(keys[0]), scan_key_index);
}

static void assert_diagnostic_row(int y, const char *label, const char *value) {
    size_t i;
    bool label_seen = false, value_seen = false;
    char number[41] = "";
    for (i = 0; i < graph_print_count; i++) {
        TestPrint *p = &graph_prints[i];
        if (p->y != y) continue;
        if (p->x == 12 && strcmp(p->text, label) == 0) label_seen = true;
        if (p->x == (y == 172 ? 172 : 220) && strcmp(p->text, value) == 0) value_seen = true;
        if (y != 172 && p->x >= 220 && strlen(number) + strlen(p->text) < sizeof(number))
            strcat(number, p->text);
    }
    ASSERT_TRUE(label_seen);
    ASSERT_TRUE(value_seen || strcmp(number, value) == 0);
}

/* Catches estimates instead of measured ticks, counting promotions as restarts,
   canceled samples replacing completed times, and modal keys leaking to work. */
static void test_diagnostics_reports_measured_completed_work_without_mutation(void) {
    AppState state, original_state;
    Project project, original_project;
    size_t before;
    unsigned ticks_before;
    int row;
    reset_fake_hardware();
    ce_ui_begin();
    app_state_init(&state);
    state.view = APP_VIEW_GRAPH;
    project_init(&project);
    commit_graph_line(&project, 0, "x=2");
    commit_graph_line(&project, 1, "a=4");
    fake_clock_increment = 10;
    ce_ui_draw(&state, &project, false, true);
    for (row = 0; row < 26; row++) ASSERT_TRUE(ce_ui_render_step(&state, &project, false));
    ASSERT_TRUE(ce_ui_render_step(&state, &project, false));
    for (row = 0; row < 49; row++) ASSERT_TRUE(ce_ui_render_step(&state, &project, false));
    ASSERT_FALSE(ce_ui_render_step(&state, &project, false));
    ce_ui_draw(&state, &project, false, true); /* Completed load is not a restart. */
    ASSERT_TRUE(ce_ui_render_step(&state, &project, false));
    state.viewport.center_x = 2;
    ce_ui_draw(&state, &project, false, true); /* Replaces unfinished work once. */
    ASSERT_TRUE(ce_ui_cancel_render());
    ASSERT_FALSE(ce_ui_cancel_render());
    fake_free_ram = 12345;
    ASSERT_EQ_INT(0, ram_queries);
    original_state = state;
    original_project = project;
    before = segment_count();
    ticks_before = clock_queries;
    graph_print_count = 0;
    open_diagnostics();
    assert_diagnostic_row(46, "Free RAM (bytes)", "12345");
    assert_diagnostic_row(64, "Coarse (ticks)", "520");
    assert_diagnostic_row(82, "Fine (ticks)", "1000");
    assert_diagnostic_row(100, "Longest row (ticks)", "10");
    assert_diagnostic_row(118, "Render restarts", "1");
    assert_diagnostic_row(136, "User cancels", "1");
    assert_diagnostic_row(154, "Drawable equations", "1");
    assert_diagnostic_row(172, "Freshness", "Canceled");
    ASSERT_EQ_INT(1, ram_queries);
    ASSERT_EQ_INT(ticks_before, clock_queries);
    ASSERT_EQ_INT(before, segment_count());
    ASSERT_EQ_INT(0, memcmp(&state, &original_state, sizeof(state)));
    ASSERT_EQ_INT(0, memcmp(&project, &original_project, sizeof(project)));
    ASSERT_TRUE(ce_ui_render_status().manual_reload_required);
    ASSERT_FALSE(ce_ui_render_step(&state, &project, false));
    ce_ui_draw(&state, &project, false, false); /* Main's latched return path. */
    ASSERT_FALSE(ce_ui_render_step(&state, &project, false));
    fake_free_ram = 23456;
    graph_print_count = 0;
    open_diagnostics();
    assert_diagnostic_row(46, "Free RAM (bytes)", "23456");
    assert_diagnostic_row(172, "Freshness", "Out of date");
    ASSERT_EQ_INT(2, ram_queries);
    project_dispose(&project);
}

/* Catches wrapping counters and truncating the CE's 32-bit clock to GraphX's
   24-bit integer, using existing source observation rather than a public hook. */
static void test_diagnostics_saturates_counts_and_prints_full_clock_values(void) {
    AppState state;
    Project project;
    reset_fake_hardware();
    ce_ui_begin();
    app_state_init(&state);
    state.view = APP_VIEW_GRAPH;
    project_init(&project);
    commit_graph_line(&project, 0, "x=2");
    ce_ui_draw(&state, &project, false, true);
    diagnostics.restarts = UINT16_MAX;
    diagnostics.cancels = UINT16_MAX;
    ce_ui_draw(&state, &project, false, true);
    ASSERT_TRUE(ce_ui_cancel_render());
    diagnostics.coarse_ticks = UINT32_MAX;
    diagnostics.fine_ticks = 1000001;
    graph_print_count = 0;
    open_diagnostics();
    assert_diagnostic_row(64, "Coarse (ticks)", "4294967295");
    assert_diagnostic_row(82, "Fine (ticks)", "1000001");
    assert_diagnostic_row(118, "Render restarts", "65535");
    assert_diagnostic_row(136, "User cancels", "65535");
    project_dispose(&project);
}

/* Catches rollover subtraction, counting modal redraws as user cancels, and
   losing the page's current state while invalidating unfinished work. */
static void test_diagnostics_preserves_freshness_and_excludes_wait_time(void) {
    static const char *const expected[] = {
        "Coarse", "Release keys", "Preview", "Refining", "Canceled preview",
        "Current", "Out of date", "Error"
    };
    AppState state;
    Project project;
    int phase, row;
    project_init(&project);
    commit_graph_line(&project, 0, "x=2");
    for (phase = 0; phase < 8; phase++) {
        CeRenderStatus before;
        unsigned clocks_before;
        size_t lines_before;
        reset_fake_hardware();
        ce_ui_begin();
        app_state_init(&state);
        state.view = APP_VIEW_GRAPH;
        fake_clock_ticks = UINT32_MAX - 200;
        fake_clock_increment = 10;
        if (phase == 7) state.viewport.units_per_pixel = NAN;
        ce_ui_draw(&state, &project, false, phase != 6);
        if (phase >= 1 && phase <= 5) {
            for (row = 0; row < 26; row++)
                ASSERT_TRUE(ce_ui_render_step(&state, &project, phase == 1));
            ASSERT_EQ_INT(520, diagnostics.coarse_ticks);
            ASSERT_EQ_INT(10, diagnostics.longest_row_ticks);
            /* Waiting on held keys must not enter the completed fine time. */
            fake_clock_ticks += 20000;
            if (phase >= 3) ASSERT_TRUE(ce_ui_render_step(&state, &project, false));
            if (phase == 4) ASSERT_TRUE(ce_ui_cancel_render());
            if (phase == 5) {
                for (row = 0; row < 49; row++) ASSERT_TRUE(ce_ui_render_step(&state, &project, false));
                ASSERT_FALSE(ce_ui_render_step(&state, &project, false));
                ASSERT_EQ_INT(1000, diagnostics.fine_ticks);
            }
        }
        before = ce_ui_render_status();
        clocks_before = clock_queries;
        lines_before = segment_count();
        graph_print_count = 0;
        open_diagnostics();
        assert_diagnostic_row(172, "Freshness", expected[phase]);
        ASSERT_EQ_INT(before.state, ce_ui_render_status().state);
        ASSERT_EQ_INT(before.completed, ce_ui_render_status().completed);
        ASSERT_EQ_INT(before.total, ce_ui_render_status().total);
        ASSERT_EQ_INT(before.manual_reload_required, ce_ui_render_status().manual_reload_required);
        ASSERT_EQ_INT(phase == 4 ? 1 : 0, diagnostics.cancels);
        ASSERT_EQ_INT(0, diagnostics.restarts);
        ASSERT_EQ_INT(clocks_before, clock_queries);
        ASSERT_EQ_INT(lines_before, segment_count());
        ASSERT_FALSE(ce_ui_render_step(&state, &project, false));
        if (phase == 0) {
            ce_ui_draw(&state, &project, false, true); /* Main restores interrupted work. */
            ASSERT_EQ_INT(1, diagnostics.restarts);
            ASSERT_TRUE(ce_ui_render_step(&state, &project, false));
        }
    }
    project_dispose(&project);
}

void run_ce_ui_tests(void) {
    test_diagnostics_saturates_counts_and_prints_full_clock_values();
    test_diagnostics_preserves_freshness_and_excludes_wait_time();
    test_diagnostics_reports_measured_completed_work_without_mutation();
    {
        static const kb_lkey_t close_keys[] = {0, kb_KeyClear, 0};
        AppState state;
        Project project;
        unsigned buffer;
        reset_fake_hardware();
        ce_ui_begin();
        app_state_init(&state);
        state.view = APP_VIEW_SPLIT;
        project_init(&project);
        commit_graph_line(&project, 0, "x=2");
        commit_graph_line(&project, 1, "y=3");
        commit_graph_line(&project, 2, "a=2");
        commit_graph_line(&project, 3, "4");
        ce_ui_draw(&state, &project, false, true);
        for (buffer = 0; buffer < 2; buffer++) {
            ASSERT_EQ_INT(COLOR_BLUE, buffer_pixels[buffer][34][21]);
            ASSERT_EQ_INT(COLOR_RED, buffer_pixels[buffer][57][21]);
            ASSERT_EQ_INT(COLOR_PANEL, buffer_pixels[buffer][80][21]);
            ASSERT_EQ_INT(COLOR_PANEL, buffer_pixels[buffer][103][21]);
        }
        ASSERT_TRUE(project_toggle_line_visibility(&project, 0));
        ce_ui_refresh(&state, &project, false);
        for (buffer = 0; buffer < 2; buffer++) {
            ASSERT_EQ_INT(COLOR_BLUE, buffer_pixels[buffer][33][20]);
            ASSERT_EQ_INT(COLOR_PANEL, buffer_pixels[buffer][34][21]);
        }
        script_keys(close_keys, 3);
        ASSERT_EQ_INT(APP_ACTION_NONE, ce_ui_show_menu());
        ASSERT_FALSE(ce_ui_render_step(&state, &project, false));
        ASSERT_FALSE(ce_ui_render_status().manual_reload_required);
        project_dispose(&project);
    }
    test_obsolete_generation_cannot_draw_or_swap();
    test_render_status_tracks_passes_and_equations();
    test_cancel_preserves_only_completed_coarse_geometry();
    test_refresh_mirrors_ui_without_restarting_rows();
    test_empty_error_and_replaced_generations();
    test_graph_base_tracks_viewport_in_both_layouts();
    test_progressive_rows_colors_navigation_and_swaps();
    test_redraw_cancels_pending_geometry_and_modal_work();
    test_empty_and_nonfinite_graphs_finish_without_geometry();
    test_every_menu_selection_preserves_normal_draw_buffer_roles();
    test_trace_closes_the_project_menu_without_an_action();
    test_footer_uses_five_non_overlapping_key_slots();
    test_footer_highlights_the_active_page();
}
