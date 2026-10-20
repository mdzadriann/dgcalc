#ifndef DGCALC_CE_UI_H
#define DGCALC_CE_UI_H

#include "app/app.h"
#include "project/project.h"

#include <stdbool.h>

typedef enum {
    CE_DIRTY_CANCEL,
    CE_DIRTY_DISCARD,
    CE_DIRTY_SAVE,
    CE_DIRTY_QUIT
} CeDirtyChoice;

void ce_ui_begin(void);
void ce_ui_draw(const AppState *state,
                const Project *project,
                bool editing);
bool ce_ui_render_step(const AppState *state, const Project *project,
                       bool navigation_active);
AppAction ce_ui_show_menu(void);
CeDirtyChoice ce_ui_confirm_dirty(const char *operation);
AppAction ce_ui_show_message(const char *message);
void ce_ui_end(void);

#endif
