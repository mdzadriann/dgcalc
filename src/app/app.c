#include "app/app.h"

void app_state_init(AppState *state) {
    state->view = APP_VIEW_EXPRESSIONS;
    state->mode = GRAPH_MODE_2D;
    state->selected_expression = 0;
}

bool app_handle_action(AppState *state, AppAction action) {
    switch (action) {
        case APP_ACTION_SHOW_EXPRESSIONS:
            state->view = APP_VIEW_EXPRESSIONS;
            break;
        case APP_ACTION_SHOW_SPLIT:
            state->view = APP_VIEW_SPLIT;
            break;
        case APP_ACTION_SHOW_GRAPH:
            state->view = APP_VIEW_GRAPH;
            break;
        case APP_ACTION_TOGGLE_GRAPH_MODE:
            state->mode = state->mode == GRAPH_MODE_2D ? GRAPH_MODE_3D
                                                       : GRAPH_MODE_2D;
            break;
        case APP_ACTION_QUIT:
            return false;
        case APP_ACTION_NONE:
        case APP_ACTION_MOVE_SELECTION_UP:
        case APP_ACTION_MOVE_SELECTION_DOWN:
        case APP_ACTION_BEGIN_EDIT:
        case APP_ACTION_COMMIT_EDIT:
        case APP_ACTION_CANCEL_EDIT:
        case APP_ACTION_NEW_PROJECT:
        case APP_ACTION_SAVE_PROJECT:
        case APP_ACTION_LOAD_PROJECT:
        case APP_ACTION_OPEN_MENU:
            break;
    }

    return true;
}
