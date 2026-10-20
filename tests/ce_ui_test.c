#include "platform/ce/ce_ui.h"
#include "test.h"

#include <graphx.h>
#include <keypadc.h>

#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "fake_ce.h"

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
            ce_ui_draw(&state, &project, false);
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
    ce_ui_draw(&state, &project, false);
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
    ce_ui_draw(&state, &project, false);
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
    ce_ui_draw(&state, &project, false);
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
    ce_ui_draw(&state, &project, false);
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
    ce_ui_draw(&state, &project, false);
    before = segment_count();
    for (i = 0; i < 10 && ce_ui_render_step(&state, &project, false); i++) {}
    ASSERT_TRUE(i < 10);
    ASSERT_EQ_INT(before, segment_count());
    ASSERT_EQ_INT(8, swaps);
    state.mode = GRAPH_MODE_3D;
    ce_ui_draw(&state, &project, false);
    before = graph_line_count;
    ASSERT_FALSE(ce_ui_render_step(&state, &project, false));
    ASSERT_EQ_INT(before, graph_line_count);
    state.view = APP_VIEW_EXPRESSIONS;
    ce_ui_draw(&state, &project, false);
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
        ce_ui_draw(&state, &project, false);
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
        ce_ui_draw(&state, &project, false);
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
        ce_ui_draw(&state, &project, false);
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
    ce_ui_draw(&state, &project, false);

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
        ce_ui_draw(&state, &project, false);

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

void run_ce_ui_tests(void) {
    test_graph_base_tracks_viewport_in_both_layouts();
    test_progressive_rows_colors_navigation_and_swaps();
    test_redraw_cancels_pending_geometry_and_modal_work();
    test_empty_and_nonfinite_graphs_finish_without_geometry();
    test_every_menu_selection_preserves_normal_draw_buffer_roles();
    test_trace_closes_the_project_menu_without_an_action();
    test_footer_uses_five_non_overlapping_key_slots();
    test_footer_highlights_the_active_page();
}
