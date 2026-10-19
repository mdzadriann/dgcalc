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

void run_app_state_tests(void) {
    test_app_state_defaults_to_expressions_2d();
    test_view_actions_preserve_mode();
    test_approved_view_keys_and_mode_toggle_preserve_selection();
    test_view_and_mode_actions_do_not_touch_invalid_drafts();
}
