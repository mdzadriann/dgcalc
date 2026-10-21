/* Observe the real loop and renderer without a production testing interface. */
#define ce_ui_begin loop_ui_begin
#define ce_ui_draw loop_ui_draw
#define ce_ui_refresh loop_ui_refresh
#define ce_ui_cancel_render loop_ui_cancel_render
#define ce_ui_render_status loop_ui_render_status
#define ce_ui_render_step loop_ui_render_step
#define ce_ui_show_menu loop_ui_show_menu
#define ce_ui_confirm_dirty loop_ui_confirm_dirty
#define ce_ui_show_message loop_ui_show_message
#define ce_ui_end loop_ui_end
#include "../src/platform/ce/ce_ui.c"
#include "test.h"
#include "fake_ce.h"
#include "platform/ce/ce_input.h"
#include "platform/ce/appvar_backend.h"
#include "storage/atomic_store.h"

#undef ce_ui_draw
#undef ce_ui_refresh
#undef ce_ui_render_step
static void track_draw(const AppState *, const Project *, bool, bool);
static void track_refresh(const AppState *, const Project *, bool);
static bool track_step(const AppState *, const Project *, bool);
static AppAction scripted_poll(EditorBuffer *, bool, AppView);
static StorageBackend test_backend(void);
static StoreResult test_load(StorageBackend *, const char *, AppState *, Project *);
static StoreResult test_save(StorageBackend *, const char *, const AppState *, const Project *);
static void track_discard(void);
#define ce_ui_draw track_draw
#define ce_ui_refresh track_refresh
#define ce_ui_render_step track_step
#define ce_input_poll scripted_poll
#define ce_input_discard_pending track_discard
#define ce_appvar_backend test_backend
#define project_store_load test_load
#define project_store_atomic test_save
#define main calculator_main
#include "../src/main.c"
#undef main
#undef ce_input_discard_pending

typedef struct {
    AppAction action;
    const char *text;
    int cursor;
    int draws, refreshes;
    CeRenderState state;
} LoopStep;
static const LoopStep *steps;
static size_t step_count, poll_count;
static int draws, refreshes, rows, discards, discards_before;
static uint32_t generation_before;
static int16_t row_before;
static bool storage_ok;
static CeDirtyChoice dirty_choice;
static bool second_line, invalid_viewport;
static AppView initial_view;
static uint8_t initial_selection, completed_before;
static int phase_before;
static bool row_active_before;
static const kb_lkey_t *modal_keys;
static size_t modal_key_count;

static void track_discard(void) { discards++; ce_input_discard_pending(); }
static StorageBackend test_backend(void) { return (StorageBackend){0}; }
static StoreResult test_load(StorageBackend *backend, const char *name,
                             AppState *state, Project *target) {
    (void)backend; (void)name;
    if (!storage_ok) return (StoreResult){false, STORE_ERR_READ};
    project_dispose(target);
    app_state_init(state);
    state->view = APP_VIEW_GRAPH;
    ASSERT_TRUE(project_set_draft(target, 0, "x=-2"));
    ASSERT_TRUE(project_commit_line(target, 0).ok);
    target->dirty = false;
    return (StoreResult){true, STORE_OK};
}
static StoreResult test_save(StorageBackend *backend, const char *name,
                             const AppState *state, const Project *target) {
    (void)backend; (void)name; (void)state; (void)target;
    return (StoreResult){storage_ok, storage_ok ? STORE_OK : STORE_ERR_WRITE};
}
static void track_draw(const AppState *state, const Project *target, bool editing, bool load) {
    ASSERT_TRUE(poll_count > 0);
    ASSERT_EQ_INT(0, rows);
    draws++;
    loop_ui_draw(state, target, editing, load);
}
static void track_refresh(const AppState *state, const Project *target, bool editing) {
    ASSERT_TRUE(poll_count > 0);
    ASSERT_EQ_INT(0, rows);
    refreshes++;
    loop_ui_refresh(state, target, editing);
}
static bool track_step(const AppState *state, const Project *target, bool navigation) {
    int16_t before = render_job.job.row_y;
    bool active;
    ASSERT_TRUE(poll_count > 0);
    ASSERT_EQ_INT(0, rows++);
    active = loop_ui_render_step(state, target, navigation);
    if (render_phase == RENDER_COARSE)
        ASSERT_TRUE(render_job.job.row_y - before <= PLOT2D_COARSE_STEP);
    return active;
}
static AppAction scripted_poll(EditorBuffer *editor, bool editing, AppView view) {
    static const kb_lkey_t close_dialog[] = {0, kb_KeyClear, 0};
    static const kb_lkey_t discard_dialog[] = {0, kb_KeyUp, 0, 0, kb_KeyEnter, 0};
    const LoopStep *step;
    (void)editing; (void)view;
    if (poll_count) {
        const LoopStep *previous = &steps[poll_count - 1];
        ASSERT_EQ_INT(previous->draws, draws);
        ASSERT_EQ_INT(previous->refreshes, refreshes);
        ASSERT_EQ_INT(previous->state, loop_ui_render_status().state);
        ASSERT_EQ_INT(1, rows);
        if (!draws && previous->action != APP_ACTION_CANCEL_RENDER) {
            ASSERT_EQ_INT(generation_before, render_generation);
            if (render_phase == RENDER_COARSE && row_active_before &&
                render_status.completed == completed_before)
                ASSERT_TRUE(render_job.job.row_y > row_before);
            if ((int)render_phase == phase_before)
                ASSERT_TRUE(render_status.completed >= completed_before);
        }
        if (draws && previous->action != APP_ACTION_OPEN_MENU &&
            previous->action != APP_ACTION_SAVE_PROJECT &&
            previous->action != APP_ACTION_LOAD_PROJECT &&
            previous->action != APP_ACTION_NEW_PROJECT && previous->action != APP_ACTION_QUIT)
            ASSERT_EQ_INT(generation_before + 1, render_generation);
        if (previous->text) ASSERT_TRUE(project.dirty);
        if (!storage_ok && (previous->action == APP_ACTION_NEW_PROJECT ||
                            previous->action == APP_ACTION_LOAD_PROJECT))
            ASSERT_STR_EQ("x=2", project_line_source(&project, 0));
        if (!storage_ok && previous->action == APP_ACTION_SAVE_PROJECT) {
            ASSERT_TRUE(project.dirty);
            ASSERT_STR_EQ("x=4", project_line_display_text(&project, 0));
        }
        if (previous->action == APP_ACTION_OPEN_MENU ||
            previous->action == APP_ACTION_LOAD_PROJECT ||
            previous->action == APP_ACTION_NEW_PROJECT ||
            previous->action == APP_ACTION_SAVE_PROJECT)
            ASSERT_TRUE(discards > discards_before);
    } else {
        app_state.view = initial_view;
        app_state.selected_expression = initial_selection;
        ASSERT_TRUE(project_set_draft(&project, 0, "x=2"));
        ASSERT_TRUE(project_commit_line(&project, 0).ok);
        if (second_line) {
            ASSERT_TRUE(project_set_draft(&project, 1, "x=-2"));
            ASSERT_TRUE(project_commit_line(&project, 1).ok);
        }
        if (invalid_viewport) app_state.viewport.units_per_pixel = 0;
        project.dirty = false;
    }
    draws = refreshes = rows = 0;
    generation_before = render_generation;
    row_before = render_job.job.row_y;
    phase_before = render_phase;
    row_active_before = render_job.active;
    completed_before = render_status.completed;
    discards_before = discards;
    if (poll_count == step_count) {
        project.dirty = false;
        poll_count++;
        return APP_ACTION_QUIT;
    }
    step = &steps[poll_count++];
    if (step->text) {
        ASSERT_TRUE(editor != NULL);
        if (editor) ASSERT_TRUE(editor_set_text(editor, step->text));
    }
    if (step->cursor >= 0 && editor) editor->cursor = (uint16_t)step->cursor;
    if (modal_keys) script_keys(modal_keys, modal_key_count);
    else script_keys(dirty_choice == CE_DIRTY_DISCARD ? discard_dialog : close_dialog,
                     dirty_choice == CE_DIRTY_DISCARD ? 6 : 3);
    return step->action;
}
static void run_script(const LoopStep *script, size_t count) {
    reset_fake_hardware();
    track_discard();
    steps = script; step_count = count; poll_count = 0;
    draws = refreshes = rows = discards = 0;
    ASSERT_EQ_INT(0, calculator_main());
    ASSERT_EQ_INT(count + 1, poll_count);
    ASSERT_EQ_INT(0, draws);
    ASSERT_EQ_INT(0, rows);
}

void run_main_loop_tests(void) {
    static const LoopStep ui_only[] = {
        {APP_ACTION_NONE, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_SHOW_SPLIT, NULL, -1, 0, 0, CE_RENDER_COARSE},
        {APP_ACTION_MOVE_SELECTION_UP, NULL, -1, 0, 0, CE_RENDER_COARSE},
        {APP_ACTION_BEGIN_EDIT, NULL, -1, 0, 1, CE_RENDER_COARSE},
        {APP_ACTION_NONE, NULL, 2, 0, 1, CE_RENDER_COARSE},
        {APP_ACTION_NONE, "x<2", 2, 0, 1, CE_RENDER_COARSE},
        {APP_ACTION_NONE, "x<", 2, 0, 1, CE_RENDER_COARSE},
        {APP_ACTION_COMMIT_EDIT, NULL, -1, 0, 1, CE_RENDER_COARSE},
        {APP_ACTION_CANCEL_EDIT, NULL, -1, 0, 1, CE_RENDER_COARSE},
        {APP_ACTION_MOVE_SELECTION_DOWN, NULL, -1, 0, 1, CE_RENDER_COARSE},
        {APP_ACTION_TOGGLE_VISIBILITY, NULL, -1, 0, 0, CE_RENDER_COARSE},
        {APP_ACTION_CLEAR_EXPRESSION, NULL, -1, 0, 0, CE_RENDER_COARSE},
        {APP_ACTION_COMMIT_EDIT, NULL, -1, 0, 0, CE_RENDER_COARSE},
        {APP_ACTION_PAN_RIGHT, NULL, -1, 0, 0, CE_RENDER_COARSE}
    };
    static const LoopStep changes[] = {
        {APP_ACTION_NONE, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_TOGGLE_VISIBILITY, NULL, -1, 1, 0, CE_RENDER_PREVIEW},
        {APP_ACTION_TOGGLE_VISIBILITY, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_BEGIN_EDIT, NULL, -1, 0, 1, CE_RENDER_COARSE},
        {APP_ACTION_COMMIT_EDIT, "x=3", -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_SHOW_GRAPH, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_PAN_RIGHT, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_ZOOM_IN, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_TOGGLE_GRAPH_MODE, NULL, -1, 1, 0, CE_RENDER_OUT_OF_DATE},
        {APP_ACTION_TOGGLE_GRAPH_MODE, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_SHOW_SPLIT, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_CLEAR_EXPRESSION, NULL, -1, 1, 0, CE_RENDER_PREVIEW}
    };
    static const LoopStep clear_uncommitted[] = {
        {APP_ACTION_NONE, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_MOVE_SELECTION_DOWN, NULL, -1, 0, 1, CE_RENDER_COARSE},
        {APP_ACTION_BEGIN_EDIT, NULL, -1, 0, 1, CE_RENDER_COARSE},
        {APP_ACTION_NONE, "x=3", -1, 0, 1, CE_RENDER_COARSE},
        {APP_ACTION_CLEAR_EXPRESSION, NULL, -1, 0, 1, CE_RENDER_COARSE},
        {APP_ACTION_CLEAR_EXPRESSION, NULL, -1, 0, 0, CE_RENDER_COARSE}
    };
    static const LoopStep canceled[] = {
        {APP_ACTION_NONE, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_CANCEL_RENDER, NULL, -1, 0, 0, CE_RENDER_CANCELED},
        {APP_ACTION_CANCEL_RENDER, NULL, -1, 0, 0, CE_RENDER_CANCELED},
        {APP_ACTION_TOGGLE_VISIBILITY, NULL, -1, 1, 0, CE_RENDER_OUT_OF_DATE},
        {APP_ACTION_SHOW_SPLIT, NULL, -1, 1, 0, CE_RENDER_PREVIEW},
        {APP_ACTION_TOGGLE_VISIBILITY, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_CANCEL_RENDER, NULL, -1, 0, 0, CE_RENDER_CANCELED},
        {APP_ACTION_SHOW_EXPRESSIONS, NULL, -1, 1, 0, CE_RENDER_OUT_OF_DATE},
        {APP_ACTION_SHOW_EXPRESSIONS, NULL, -1, 0, 0, CE_RENDER_OUT_OF_DATE},
        {APP_ACTION_SHOW_GRAPH, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_CANCEL_RENDER, NULL, -1, 0, 0, CE_RENDER_CANCELED},
        {APP_ACTION_PAN_LEFT, NULL, -1, 1, 0, CE_RENDER_OUT_OF_DATE},
        {APP_ACTION_ZOOM_OUT, NULL, -1, 1, 0, CE_RENDER_OUT_OF_DATE},
        {APP_ACTION_SHOW_GRAPH, NULL, -1, 1, 0, CE_RENDER_COARSE}
    };
    static const LoopStep modal[] = {
        {APP_ACTION_NONE, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_OPEN_MENU, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_SAVE_PROJECT, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_CANCEL_RENDER, NULL, -1, 0, 0, CE_RENDER_CANCELED},
        {APP_ACTION_OPEN_MENU, NULL, -1, 1, 0, CE_RENDER_OUT_OF_DATE},
        {APP_ACTION_LOAD_PROJECT, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_CANCEL_RENDER, NULL, -1, 0, 0, CE_RENDER_CANCELED},
        {APP_ACTION_NEW_PROJECT, NULL, -1, 1, 0, CE_RENDER_OUT_OF_DATE},
        {APP_ACTION_CANCEL_RENDER, NULL, -1, 0, 0, CE_RENDER_OUT_OF_DATE},
        {APP_ACTION_SHOW_GRAPH, NULL, -1, 1, 0, CE_RENDER_PREVIEW}
    };
    static const LoopStep declined[] = {
        {APP_ACTION_NONE, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_BEGIN_EDIT, NULL, -1, 0, 1, CE_RENDER_COARSE},
        {APP_ACTION_NONE, "x=3", -1, 0, 1, CE_RENDER_COARSE},
        {APP_ACTION_CANCEL_RENDER, NULL, -1, 0, 0, CE_RENDER_CANCELED},
        {APP_ACTION_NEW_PROJECT, NULL, -1, 1, 0, CE_RENDER_OUT_OF_DATE},
        {APP_ACTION_LOAD_PROJECT, NULL, -1, 1, 0, CE_RENDER_OUT_OF_DATE},
        {APP_ACTION_CANCEL_EDIT, NULL, -1, 0, 1, CE_RENDER_OUT_OF_DATE},
        {APP_ACTION_LOAD_PROJECT, NULL, -1, 1, 0, CE_RENDER_OUT_OF_DATE}
    };
    static const LoopStep canceled_math[] = {
        {APP_ACTION_NONE, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_CANCEL_RENDER, NULL, -1, 0, 0, CE_RENDER_CANCELED},
        {APP_ACTION_BEGIN_EDIT, NULL, -1, 0, 1, CE_RENDER_CANCELED},
        {APP_ACTION_COMMIT_EDIT, "x=5", -1, 1, 0, CE_RENDER_OUT_OF_DATE},
        {APP_ACTION_CLEAR_EXPRESSION, NULL, -1, 1, 0, CE_RENDER_OUT_OF_DATE},
        {APP_ACTION_TOGGLE_GRAPH_MODE, NULL, -1, 1, 0, CE_RENDER_OUT_OF_DATE},
        {APP_ACTION_TOGGLE_GRAPH_MODE, NULL, -1, 1, 0, CE_RENDER_OUT_OF_DATE}
    };
    static const LoopStep dirty_quit[] = {
        {APP_ACTION_NONE, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_BEGIN_EDIT, NULL, -1, 0, 1, CE_RENDER_COARSE},
        {APP_ACTION_NONE, "x=4", -1, 0, 1, CE_RENDER_COARSE},
        {APP_ACTION_QUIT, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_SAVE_PROJECT, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_CANCEL_RENDER, NULL, -1, 0, 0, CE_RENDER_CANCELED},
        {APP_ACTION_QUIT, NULL, -1, 1, 0, CE_RENDER_OUT_OF_DATE}
    };
    static const LoopStep error_reload[] = {
        {APP_ACTION_NONE, NULL, -1, 1, 0, CE_RENDER_ERROR},
        {APP_ACTION_CANCEL_RENDER, NULL, -1, 0, 0, CE_RENDER_ERROR},
        {APP_ACTION_SHOW_SPLIT, NULL, -1, 1, 0, CE_RENDER_ERROR}
    };
    static const LoopStep selection_end[] = {
        {APP_ACTION_NONE, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_MOVE_SELECTION_DOWN, NULL, -1, 0, 0, CE_RENDER_COARSE},
        {APP_ACTION_TOGGLE_VISIBILITY, NULL, -1, 0, 0, CE_RENDER_COARSE}
    };
    LoopStep finish[79];
    static const LoopStep menu_load[] = {
        {APP_ACTION_NONE, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_CANCEL_RENDER, NULL, -1, 0, 0, CE_RENDER_CANCELED},
        {APP_ACTION_OPEN_MENU, NULL, -1, 1, 0, CE_RENDER_COARSE}
    };
    static const LoopStep menu_new[] = {
        {APP_ACTION_NONE, NULL, -1, 1, 0, CE_RENDER_COARSE},
        {APP_ACTION_CANCEL_RENDER, NULL, -1, 0, 0, CE_RENDER_CANCELED},
        {APP_ACTION_OPEN_MENU, NULL, -1, 1, 0, CE_RENDER_OUT_OF_DATE},
        {APP_ACTION_SHOW_GRAPH, NULL, -1, 1, 0, CE_RENDER_PREVIEW}
    };
    static const kb_lkey_t load_keys[] = {0, kb_KeyDown, 0, 0, kb_KeyDown, 0,
                                         0, kb_KeyEnter, 0};
    static const kb_lkey_t new_keys[] = {0, kb_KeyEnter, 0};
    size_t i;
    storage_ok = true; dirty_choice = CE_DIRTY_CANCEL; initial_view = APP_VIEW_SPLIT;
    run_script(ui_only, sizeof(ui_only) / sizeof(ui_only[0]));
    run_script(clear_uncommitted, sizeof(clear_uncommitted) / sizeof(clear_uncommitted[0]));
    run_script(changes, sizeof(changes) / sizeof(changes[0]));
    run_script(canceled, sizeof(canceled) / sizeof(canceled[0]));
    run_script(modal, sizeof(modal) / sizeof(modal[0]));
    run_script(canceled_math, sizeof(canceled_math) / sizeof(canceled_math[0]));
    storage_ok = false;
    run_script(declined, sizeof(declined) / sizeof(declined[0]));
    run_script(dirty_quit, sizeof(dirty_quit) / sizeof(dirty_quit[0]));
    invalid_viewport = true;
    run_script(error_reload, sizeof(error_reload) / sizeof(error_reload[0]));
    invalid_viewport = false;
    initial_selection = PROJECT_MAX_EXPRESSIONS - 1;
    run_script(selection_end, sizeof(selection_end) / sizeof(selection_end[0]));
    initial_selection = 0;
    for (i = 0; i < 79; i++) {
        finish[i] = (LoopStep){APP_ACTION_SHOW_SPLIT, NULL, -1, i == 0, 0,
                     i < 25 ? CE_RENDER_COARSE : i == 25 ? CE_RENDER_PREVIEW :
                     i < 76 ? CE_RENDER_FINE : CE_RENDER_CURRENT};
    }
    /* Same active key during loading, automatic preview, fine, and current. */
    run_script(finish, 79);
    finish[26] = (LoopStep){APP_ACTION_CANCEL_RENDER, NULL, -1, 0, 0, CE_RENDER_PREVIEW};
    finish[27] = (LoopStep){APP_ACTION_SHOW_SPLIT, NULL, -1, 1, 0, CE_RENDER_COARSE};
    run_script(finish, 28);
    second_line = true;
    for (i = 0; i < 35; i++)
        finish[i] = (LoopStep){APP_ACTION_SHOW_SPLIT, NULL, -1, i == 0, 0, CE_RENDER_COARSE};
    finish[28].action = APP_ACTION_BEGIN_EDIT; finish[28].refreshes = 1;
    finish[29].action = APP_ACTION_NONE; finish[29].text = "x=4"; finish[29].refreshes = 1;
    finish[30].action = APP_ACTION_NONE; finish[30].cursor = 1; finish[30].refreshes = 1;
    run_script(finish, 35);
    second_line = false;
    storage_ok = true;
    modal_keys = load_keys; modal_key_count = sizeof(load_keys) / sizeof(load_keys[0]);
    run_script(menu_load, sizeof(menu_load) / sizeof(menu_load[0]));
    modal_keys = new_keys; modal_key_count = sizeof(new_keys) / sizeof(new_keys[0]);
    run_script(menu_new, sizeof(menu_new) / sizeof(menu_new[0]));
    modal_keys = NULL;
}
