#include "app/app.h"
#include "project/project.h"
#include "test.h"

static void test_app_state_defaults_to_expressions_2d(void) {
    AppState state;

    app_state_init(&state);

    ASSERT_EQ_INT(APP_VIEW_EXPRESSIONS, state.view);
    ASSERT_EQ_INT(GRAPH_MODE_2D, state.mode);
    ASSERT_EQ_INT(0, state.selected_expression);
}

static void test_view_actions_preserve_mode(void) {
    AppState state;

    app_state_init(&state);
    state.mode = GRAPH_MODE_3D;

    ASSERT_TRUE(app_handle_action(&state, APP_ACTION_SHOW_SPLIT));
    ASSERT_EQ_INT(APP_VIEW_SPLIT, state.view);
    ASSERT_EQ_INT(GRAPH_MODE_3D, state.mode);
}

static void test_approved_view_keys_and_mode_toggle_preserve_selection(void) {
    AppState state;
    uint8_t selected_before = 4;

    app_state_init(&state);
    state.selected_expression = selected_before;
    app_handle_action(&state, APP_ACTION_SHOW_EXPRESSIONS);
    ASSERT_EQ_INT(APP_VIEW_EXPRESSIONS, state.view);
    app_handle_action(&state, APP_ACTION_SHOW_SPLIT);
    ASSERT_EQ_INT(APP_VIEW_SPLIT, state.view);
    app_handle_action(&state, APP_ACTION_SHOW_GRAPH);
    ASSERT_EQ_INT(APP_VIEW_GRAPH, state.view);
    ASSERT_EQ_INT(selected_before, state.selected_expression);
    app_handle_action(&state, APP_ACTION_TOGGLE_GRAPH_MODE);
    ASSERT_EQ_INT(GRAPH_MODE_3D, state.mode);
    ASSERT_EQ_INT(selected_before, state.selected_expression);
}

static void test_view_and_mode_actions_do_not_touch_invalid_drafts(void) {
    AppState state;
    Project project;

    app_state_init(&state);
    project_init(&project);
    ASSERT_TRUE(project_set_draft(&project, 0, "x^2+"));
    ASSERT_FALSE(project_commit_line(&project, 0).ok);

    app_handle_action(&state, APP_ACTION_SHOW_SPLIT);
    app_handle_action(&state, APP_ACTION_SHOW_GRAPH);
    app_handle_action(&state, APP_ACTION_SHOW_EXPRESSIONS);
    app_handle_action(&state, APP_ACTION_TOGGLE_GRAPH_MODE);
    ASSERT_STR_EQ("x^2+", project_line_display_text(&project, 0));
    ASSERT_EQ_INT(EXPRESSION_INVALID, project_line_status(&project, 0));
    project_dispose(&project);
}

static void test_graph_actions_only_move_viewport_and_preserve_project(void) {
    static const struct { AppAction action; float x, y, scale; } cases[] = {
        {APP_ACTION_PAN_LEFT, -2, 0, 0.0625f},
        {APP_ACTION_PAN_RIGHT, 2, 0, 0.0625f},
        {APP_ACTION_PAN_UP, 0, 1.225f, 0.0625f},
        {APP_ACTION_PAN_DOWN, 0, -1.225f, 0.0625f},
        {APP_ACTION_ZOOM_IN, 0, 0, 0.05f},
        {APP_ACTION_ZOOM_OUT, 0, 0, 0.078125f}
    };
    AppState state;
    Project project;
    size_t i;
    int dirty;
    AppView view;

    project_init(&project);
    ASSERT_TRUE(project_set_draft(&project, 0, "a=2"));
    ASSERT_TRUE(project_commit_line(&project, 0).ok);
    for (dirty = 0; dirty <= 1; dirty++) {
        project.dirty = dirty;
        for (view = APP_VIEW_EXPRESSIONS; view <= APP_VIEW_GRAPH; view++) {
            for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
                app_state_init(&state);
                state.view = view;
                state.selected_expression = 4;
                ASSERT_TRUE(app_handle_action(&state, cases[i].action));
                ASSERT_FLOAT_EQ(view == APP_VIEW_GRAPH ? cases[i].x : 0,
                                state.viewport.center_x);
                ASSERT_FLOAT_EQ(view == APP_VIEW_GRAPH ? cases[i].y : 0,
                                state.viewport.center_y);
                ASSERT_FLOAT_EQ(view == APP_VIEW_GRAPH ? cases[i].scale : 0.0625f,
                                state.viewport.units_per_pixel);
                ASSERT_EQ_INT(view, state.view);
                ASSERT_EQ_INT(GRAPH_MODE_2D, state.mode);
                ASSERT_EQ_INT(4, state.selected_expression);
                ASSERT_EQ_INT(dirty, project.dirty);
                ASSERT_STR_EQ("a=2", project_line_source(&project, 0));
                ASSERT_EQ_INT(EXPRESSION_VALID, project_line_status(&project, 0));
                ASSERT_EQ_INT(1, project.count);
            }
        }
    }
    project_dispose(&project);
}

void run_app_state_tests(void) {
    test_graph_actions_only_move_viewport_and_preserve_project();
    test_app_state_defaults_to_expressions_2d();
    test_view_actions_preserve_mode();
    test_approved_view_keys_and_mode_toggle_preserve_selection();
    test_view_and_mode_actions_do_not_touch_invalid_drafts();
}
