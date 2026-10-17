#ifndef DGCALC_CE_INPUT_H
#define DGCALC_CE_INPUT_H

#include "app/app.h"
#include "editor/editor.h"

#include <stdbool.h>

void ce_input_begin(void);
AppAction ce_input_poll(EditorBuffer *active_editor, bool editing);
void ce_input_end(void);

#endif
