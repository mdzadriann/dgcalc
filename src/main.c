#include "app/app.h"
#include "platform/ce/appvar_backend.h"
#include "platform/ce/ce_input.h"
#include "platform/ce/ce_ui.h"
#include "project/project.h"
#include "storage/atomic_store.h"

#include <stdbool.h>
#include <stdint.h>

#define PROJECT_NAME "DGCPROJ"

static AppState app_state;
static Project project;
static StorageBackend storage;

typedef enum {
    CONFIRM_CANCEL,
    CONFIRM_PROCEED,
    CONFIRM_QUIT
} ConfirmResult;

static const char *storage_error_message(StoreError error) {
    switch (error) {
        case STORE_ERR_ENCODE: return "Project is too large";
        case STORE_ERR_WRITE: return "Write failed";
        case STORE_ERR_VALIDATE: return "Saved data is invalid";
        case STORE_ERR_RENAME: return "Replace failed; old save kept";
        case STORE_ERR_ARCHIVE: return "Archive failed; old save kept";
        case STORE_ERR_READ: return "Project could not be read";
        case STORE_ERR_MEMORY: return "Memory is insufficient";
        case STORE_OK: return "OK";
        default: return "Storage error";
    }
}

static bool save_project(bool announce, bool *quit_requested) {
    StoreResult result = project_store_atomic(&storage,
                                              PROJECT_NAME,
                                              &app_state,
                                              &project);
    *quit_requested = false;
    if (!result.ok) {
        *quit_requested =
            ce_ui_show_message(storage_error_message(result.error)) ==
            APP_ACTION_QUIT;
        return false;
    }
    project.dirty = false;
    if (announce) {
        *quit_requested =
            ce_ui_show_message("Project saved") == APP_ACTION_QUIT;
    }
    return true;
}

static ConfirmResult confirm_data_loss(const char *operation) {
    CeDirtyChoice choice;
    bool quit_requested;

    if (!project.dirty) {
        return CONFIRM_PROCEED;
    }
    choice = ce_ui_confirm_dirty(operation);
    if (choice == CE_DIRTY_QUIT) {
        return CONFIRM_QUIT;
    }
    if (choice == CE_DIRTY_CANCEL) {
        return CONFIRM_CANCEL;
    }
    if (choice == CE_DIRTY_DISCARD) {
        return CONFIRM_PROCEED;
    }
    if (save_project(false, &quit_requested)) {
        return CONFIRM_PROCEED;
    }
    return quit_requested ? CONFIRM_QUIT : CONFIRM_CANCEL;
}

static void handle_action(AppAction action, bool *running) {
    uint8_t index = app_state.selected_expression;

    switch (action) {
        case APP_ACTION_SHOW_EXPRESSIONS:
        case APP_ACTION_SHOW_SPLIT:
        case APP_ACTION_SHOW_GRAPH:
        case APP_ACTION_PAN_LEFT:
        case APP_ACTION_PAN_RIGHT:
        case APP_ACTION_PAN_UP:
        case APP_ACTION_PAN_DOWN:
        case APP_ACTION_ZOOM_IN:
        case APP_ACTION_ZOOM_OUT:
            (void)app_handle_action(&app_state, action);
            break;
        case APP_ACTION_MOVE_SELECTION_UP:
            if (index > 0) {
                app_state.selected_expression--;
            }
            break;
        case APP_ACTION_MOVE_SELECTION_DOWN:
            if (index + 1 < PROJECT_MAX_EXPRESSIONS) {
                app_state.selected_expression++;
            }
            break;
        case APP_ACTION_BEGIN_EDIT:
            (void)project_begin_edit(&project, index);
            break;
        case APP_ACTION_COMMIT_EDIT:
            if (project_is_editing(&project)) {
                CommitResult result = project_commit_line(
                    &project, project_editing_index(&project));
                if (result.out_of_memory &&
                    ce_ui_show_message("Memory is insufficient") == APP_ACTION_QUIT) {
                    handle_action(APP_ACTION_QUIT, running);
                }
            }
            break;
        case APP_ACTION_CANCEL_EDIT:
            project_cancel_edit(&project);
            break;
        case APP_ACTION_CLEAR_EXPRESSION:
            (void)project_clear_line(&project, index);
            break;
        case APP_ACTION_SAVE_PROJECT: {
            bool quit_requested;
            (void)save_project(true, &quit_requested);
            if (quit_requested) {
                handle_action(APP_ACTION_QUIT, running);
            }
            break;
        }
        case APP_ACTION_LOAD_PROJECT: {
            ConfirmResult confirmation =
                confirm_data_loss("Load saved project?");
            if (confirmation == CONFIRM_QUIT) {
                handle_action(APP_ACTION_QUIT, running);
            } else if (confirmation == CONFIRM_PROCEED) {
                StoreResult result = project_store_load(&storage,
                                                        PROJECT_NAME,
                                                        &app_state,
                                                        &project);
                if (!result.ok) {
                    if (ce_ui_show_message(storage_error_message(result.error)) ==
                        APP_ACTION_QUIT) {
                        handle_action(APP_ACTION_QUIT, running);
                    }
                }
            }
            break;
        }
        case APP_ACTION_NEW_PROJECT: {
            ConfirmResult confirmation =
                confirm_data_loss("Start new project?");
            if (confirmation == CONFIRM_QUIT) {
                handle_action(APP_ACTION_QUIT, running);
            } else if (confirmation == CONFIRM_PROCEED) {
                app_state_init(&app_state);
                project_dispose(&project);
            }
            break;
        }
        case APP_ACTION_TOGGLE_GRAPH_MODE:
            (void)app_handle_action(&app_state, action);
            project.dirty = true;
            break;
        case APP_ACTION_OPEN_MENU:
            handle_action(ce_ui_show_menu(), running);
            break;
        case APP_ACTION_QUIT: {
            ConfirmResult confirmation;
            do {
                confirmation = confirm_data_loss("Quit DGCALC?");
            } while (confirmation == CONFIRM_QUIT);
            if (confirmation == CONFIRM_PROCEED) {
                *running = false;
            }
            break;
        }
        case APP_ACTION_NONE:
            break;
    }
}

int main(void) {
    bool running = true;
    bool redraw = true;

    app_state_init(&app_state);
    project_init(&project);
    ce_ui_begin();
    ce_input_begin();
    storage = ce_appvar_backend();

    while (running) {
        EditorBuffer *editor = project_editor(&project);
        uint16_t length_before;
        uint16_t cursor_before;
        AppAction action;

        if (redraw) {
            ce_ui_draw(&app_state, &project, project_is_editing(&project), true);
            redraw = false;
        }
        length_before = editor != NULL ? editor->length : 0;
        cursor_before = editor != NULL ? editor->cursor : 0;
        action = ce_input_poll(editor, project_is_editing(&project), app_state.view);
        if (editor != NULL &&
            (editor->length != length_before || editor->cursor != cursor_before)) {
            redraw = true;
        }
        if (editor != NULL && editor->length != length_before) {
            project.dirty = true;
        }
        if (action != APP_ACTION_NONE) {
            handle_action(action, &running);
            redraw = true;
        }
        if (!redraw && running) {
            (void)ce_ui_render_step(&app_state, &project,
                                    ce_input_graph_navigation_active());
        }
    }

    ce_input_end();
    ce_ui_end();
    project_dispose(&project);
    return 0;
}
