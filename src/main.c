#include "app/app.h"
#include "platform/ce/appvar_backend.h"
#include "platform/ce/ce_input.h"
#include "platform/ce/ce_ui.h"
#include "project/project.h"
#include "storage/atomic_store.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define PROJECT_NAME "DGCPROJ"

static AppState app_state;
static Project project;
static StorageBackend storage;
static bool ui_dirty, graph_dirty;

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

static AppAction show_message(const char *message) {
    AppAction action;
    graph_dirty = true;
    ce_input_discard_pending();
    action = ce_ui_show_message(message);
    ce_input_discard_pending();
    return action;
}

static bool save_project(bool announce, bool *quit_requested) {
    StoreResult result = project_store_atomic(&storage,
                                              PROJECT_NAME,
                                              &app_state,
                                              &project);
    *quit_requested = false;
    if (!result.ok) {
        *quit_requested =
            show_message(storage_error_message(result.error)) ==
            APP_ACTION_QUIT;
        return false;
    }
    project.dirty = false;
    if (announce) {
        *quit_requested =
            show_message("Project saved") == APP_ACTION_QUIT;
    }
    return true;
}

static ConfirmResult confirm_data_loss(const char *operation) {
    CeDirtyChoice choice;
    bool quit_requested;

    if (!project.dirty) {
        return CONFIRM_PROCEED;
    }
    graph_dirty = true;
    ce_input_discard_pending();
    choice = ce_ui_confirm_dirty(operation);
    ce_input_discard_pending();
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

/* Return only this action's explicit reload request; render status owns the latch. */
static bool handle_action(AppAction action, bool *running) {
    uint8_t index = app_state.selected_expression;
    bool changed = false;
    bool reload = false;
    bool changes_graph = app_action_changes_graph(action);

    switch (action) {
        case APP_ACTION_SHOW_EXPRESSIONS:
        case APP_ACTION_SHOW_SPLIT:
        case APP_ACTION_SHOW_GRAPH:
        {
            AppView previous = app_state.view;
            CeRenderStatus status = ce_ui_render_status();
            (void)app_handle_action(&app_state, action);
            reload = app_state.view != APP_VIEW_EXPRESSIONS &&
                     (previous != app_state.view || status.manual_reload_required ||
                      status.state == CE_RENDER_OUT_OF_DATE ||
                      status.state == CE_RENDER_ERROR || status.state == CE_RENDER_CANCELED);
            graph_dirty |= previous != app_state.view || reload;
            break;
        }
        case APP_ACTION_PAN_LEFT:
        case APP_ACTION_PAN_RIGHT:
        case APP_ACTION_PAN_UP:
        case APP_ACTION_PAN_DOWN:
        case APP_ACTION_ZOOM_IN:
        case APP_ACTION_ZOOM_OUT:
        {
            Viewport2D previous = app_state.viewport;
            (void)app_handle_action(&app_state, action);
            changed = previous.center_x != app_state.viewport.center_x ||
                      previous.center_y != app_state.viewport.center_y ||
                      previous.units_per_pixel != app_state.viewport.units_per_pixel;
            break;
        }
        case APP_ACTION_MOVE_SELECTION_UP:
            if (index > 0) {
                app_state.selected_expression--;
                changed = true;
            }
            break;
        case APP_ACTION_MOVE_SELECTION_DOWN:
            if (index + 1 < PROJECT_MAX_EXPRESSIONS) {
                app_state.selected_expression++;
                changed = true;
            }
            break;
        case APP_ACTION_BEGIN_EDIT:
            changed = project_begin_edit(&project, index);
            break;
        case APP_ACTION_COMMIT_EDIT:
            if (project_is_editing(&project)) {
                CommitResult result = project_commit_line(
                    &project, project_editing_index(&project));
                changed = result.ok;
                ui_dirty = true;
                if (result.out_of_memory &&
                    show_message("Memory is insufficient") == APP_ACTION_QUIT) {
                    reload = handle_action(APP_ACTION_QUIT, running);
                }
            }
            break;
        case APP_ACTION_CANCEL_EDIT:
            changed = project_is_editing(&project);
            project_cancel_edit(&project);
            break;
        case APP_ACTION_CLEAR_EXPRESSION:
            changes_graph = project_line_has_committed(&project, index);
            changed = project_clear_line(&project, index);
            break;
        case APP_ACTION_TOGGLE_VISIBILITY:
            changed = project_toggle_line_visibility(&project, index);
            break;
        case APP_ACTION_CANCEL_RENDER:
            (void)ce_ui_cancel_render();
            break;
        case APP_ACTION_SAVE_PROJECT: {
            bool quit_requested;
            (void)save_project(true, &quit_requested);
            if (quit_requested) {
                reload = handle_action(APP_ACTION_QUIT, running);
            }
            break;
        }
        case APP_ACTION_LOAD_PROJECT: {
            ConfirmResult confirmation;
            ce_input_discard_pending();
            confirmation = confirm_data_loss("Load saved project?");
            if (confirmation == CONFIRM_QUIT) {
                reload = handle_action(APP_ACTION_QUIT, running);
            } else if (confirmation == CONFIRM_PROCEED) {
                StoreResult result = project_store_load(&storage,
                                                        PROJECT_NAME,
                                                        &app_state,
                                                        &project);
                changed = reload = result.ok;
                if (!result.ok) {
                    if (show_message(storage_error_message(result.error)) ==
                        APP_ACTION_QUIT) {
                        reload = handle_action(APP_ACTION_QUIT, running);
                    }
                }
            }
            ce_input_discard_pending();
            break;
        }
        case APP_ACTION_NEW_PROJECT: {
            ConfirmResult confirmation;
            ce_input_discard_pending();
            confirmation = confirm_data_loss("Start new project?");
            if (confirmation == CONFIRM_QUIT) {
                reload = handle_action(APP_ACTION_QUIT, running);
            } else if (confirmation == CONFIRM_PROCEED) {
                app_state_init(&app_state);
                project_dispose(&project);
                changed = reload = true;
            }
            ce_input_discard_pending();
            break;
        }
        case APP_ACTION_TOGGLE_GRAPH_MODE:
            (void)app_handle_action(&app_state, action);
            project.dirty = true;
            changed = true;
            break;
        case APP_ACTION_OPEN_MENU:
            graph_dirty = true;
            ce_input_discard_pending();
            reload = handle_action(ce_ui_show_menu(), running);
            ce_input_discard_pending();
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
    ui_dirty |= changed;
    graph_dirty |= changed && changes_graph;
    return reload;
}

int main(void) {
    bool running = true;
    ui_dirty = false;
    graph_dirty = true;

    app_state_init(&app_state);
    project_init(&project);
    ce_ui_begin();
    ce_input_begin();
    storage = ce_appvar_backend();

    while (running) {
        EditorBuffer *editor = project_editor(&project);
        char text_before[EDITOR_CAPACITY];
        uint16_t cursor_before = editor != NULL ? editor->cursor : 0;
        AppAction action;
        bool reload;

        if (editor != NULL) memcpy(text_before, editor->text, editor->length + 1);
        action = ce_input_poll(editor, project_is_editing(&project), app_state.view);
        if (editor != NULL) {
            bool source_changed = strcmp(text_before, editor->text) != 0;
            ui_dirty |= source_changed || editor->cursor != cursor_before;
            project.dirty |= source_changed;
        }
        reload = handle_action(action, &running);
        if (!running) break;
        if (graph_dirty) {
            ce_ui_draw(&app_state, &project, project_is_editing(&project),
                        reload || !ce_ui_render_status().manual_reload_required);
        } else if (ui_dirty) {
            ce_ui_refresh(&app_state, &project, project_is_editing(&project));
        }
        graph_dirty = ui_dirty = false;
        (void)ce_ui_render_step(&app_state, &project,
                                ce_input_graph_navigation_active());
    }

    ce_input_end();
    ce_ui_end();
    project_dispose(&project);
    return 0;
}
