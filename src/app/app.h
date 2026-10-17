#ifndef DGCALC_APP_H
#define DGCALC_APP_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_VIEW_EXPRESSIONS,
    APP_VIEW_SPLIT,
    APP_VIEW_GRAPH
} AppView;

typedef enum {
    GRAPH_MODE_2D,
    GRAPH_MODE_3D
} GraphMode;

typedef enum {
    APP_ACTION_NONE,
    APP_ACTION_SHOW_EXPRESSIONS,
    APP_ACTION_SHOW_SPLIT,
    APP_ACTION_SHOW_GRAPH,
    APP_ACTION_QUIT
} AppAction;

typedef struct {
    AppView view;
    GraphMode mode;
    uint8_t selected_expression;
} AppState;

void app_state_init(AppState *state);
bool app_handle_action(AppState *state, AppAction action);

#endif
