#include "app/app.h"
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

void run_app_state_tests(void) {
    test_app_state_defaults_to_expressions_2d();
    test_view_actions_preserve_mode();
}
