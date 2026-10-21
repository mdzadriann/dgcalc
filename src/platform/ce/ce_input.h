#ifndef DGCALC_CE_INPUT_H
#define DGCALC_CE_INPUT_H

#include "app/app.h"
#include "editor/editor.h"

#include <stdbool.h>

void ce_input_begin(void);
void ce_input_discard_pending(void);
AppAction ce_input_poll(EditorBuffer *active_editor, bool editing, AppView view);
bool ce_input_graph_navigation_active(void);
void ce_input_end(void);

#endif
