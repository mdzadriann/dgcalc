#include "platform/ce/ce_ui.h"
#include "test.h"

#include <graphx.h>
#include <keypadc.h>

#include <stdint.h>
#include <string.h>

#include "fake_ce.h"

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
        script_keys(keys, key_count);
        ASSERT_EQ_INT(expected_actions[selection], ce_ui_show_menu());
        ASSERT_EQ_INT(selection + 1, screen_blits);
        ASSERT_EQ_INT(selection + 1, buffer_blits);
        ASSERT_EQ_INT(0, swaps);
        ASSERT_EQ_INT(MODE_3_CONTINUOUS, kb_GetMode());
        ASSERT_EQ_INT((selection + 1) * 3, draw_operation_count);
        for (frame = 0; frame <= selection; frame++) {
            ASSERT_EQ_INT(TEST_COPY_SCREEN_TO_BUFFER,
                          draw_operations[frame * 3]);
            ASSERT_EQ_INT(TEST_DRAW_DIALOG,
                          draw_operations[frame * 3 + 1]);
            ASSERT_EQ_INT(TEST_COPY_BUFFER_TO_SCREEN,
                          draw_operations[frame * 3 + 2]);
        }

        app_state_init(&state);
        project_init(&project);
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
    test_every_menu_selection_preserves_normal_draw_buffer_roles();
    test_trace_closes_the_project_menu_without_an_action();
    test_footer_uses_five_non_overlapping_key_slots();
    test_footer_highlights_the_active_page();
}
