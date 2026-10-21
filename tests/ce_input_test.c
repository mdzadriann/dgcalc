#include "platform/ce/ce_input.h"
#include "platform/ce/ce_ui.h"
#include "project/project.h"
#include "fake_ce.h"
#include "test.h"

static AppAction press(kb_lkey_t key, EditorBuffer *editor, bool editing,
                       AppView view) {
    kb_lkey_t keys[] = {0, key};
    script_keys(keys, 2);
    kb_Scan();
    (void)ce_input_poll(editor, editing, view);
    kb_Scan();
    return ce_input_poll(editor, editing, view);
}

static void test_math_cycles_complete_relations_at_each_caret_position(void) {
    static const struct { const char *source, *want; int end; } cases[] = {
        {"x=y", "x<y", 2}, {"x<y", "x>y", 2},
        {"x>y", "x<=y", 3}, {"x<=y", "x>=y", 3},
        {"x>=y", "x=y", 2}, {"=", "<", 1}, {">=", "=", 1}
    };
    EditorBuffer editor;
    size_t i, cursor;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        size_t start = cases[i].source[0] == 'x' ? 1 : 0;
        size_t end = strlen(cases[i].source) - start;
        for (cursor = start; cursor <= end; cursor++) {
            reset_fake_hardware();
            ce_input_begin();
            editor_init(&editor);
            ASSERT_TRUE(editor_set_text(&editor, cases[i].source));
            ASSERT_TRUE(editor_move(&editor, (int16_t)cursor - editor.cursor));
            ASSERT_EQ_INT(APP_ACTION_NONE,
                          press(kb_KeyMath, &editor, true, APP_VIEW_EXPRESSIONS));
            ASSERT_STR_EQ(cases[i].want, editor.text);
            ASSERT_EQ_INT(strlen(cases[i].want), editor.length);
            ASSERT_EQ_INT(cases[i].end, editor.cursor);
            ce_input_end();
        }
    }
}

static void test_math_inserts_locally_and_rejects_invalid_clusters(void) {
    static const struct { const char *source; int cursor; const char *want; int end; } cases[] = {
        {"", 0, "=", 1}, {"xy", 1, "x=y", 2},
        {"x=y+z", 5, "x=y+z=", 6}, {"x=y+z", 0, "=x=y+z", 1},
        {"x=y<z", 2, "x<y<z", 2}, {"x=y<z", 3, "x=y>z", 4}
    };
    static const char *invalid[] = {"==", "<<", "<==", ">>", "=>", "><", ">=<", "===="};
    EditorBuffer editor;
    size_t i, cursor;

    reset_fake_hardware();
    ce_input_begin();
    editor_init(&editor);
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ASSERT_TRUE(editor_set_text(&editor, cases[i].source));
        ASSERT_TRUE(editor_move(&editor, cases[i].cursor - editor.cursor));
        (void)press(kb_KeyMath, &editor, true, APP_VIEW_SPLIT);
        ASSERT_STR_EQ(cases[i].want, editor.text);
        ASSERT_EQ_INT(strlen(cases[i].want), editor.length);
        ASSERT_EQ_INT(cases[i].end, editor.cursor);
    }
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        for (cursor = 0; cursor <= strlen(invalid[i]); cursor++) {
            ASSERT_TRUE(editor_set_text(&editor, invalid[i]));
            ASSERT_TRUE(editor_move(&editor, (int16_t)cursor - editor.cursor));
            (void)press(kb_KeyMath, &editor, true, APP_VIEW_EXPRESSIONS);
            ASSERT_STR_EQ(invalid[i], editor.text);
            ASSERT_EQ_INT(strlen(invalid[i]), editor.length);
            ASSERT_EQ_INT(cursor, editor.cursor);
        }
    }
    ce_input_end();
}

static void test_math_growth_is_transactional_at_capacity(void) {
    EditorBuffer editor;
    char full[EDITOR_CAPACITY];
    size_t cursor;

    memset(full, 'x', sizeof(full) - 1);
    full[sizeof(full) - 1] = '\0';
    full[1] = '>';
    reset_fake_hardware();
    ce_input_begin();
    editor_init(&editor);
    for (cursor = 1; cursor <= 2; cursor++) {
        ASSERT_TRUE(editor_set_text(&editor, full));
        ASSERT_TRUE(editor_move(&editor, (int16_t)cursor - editor.cursor));
        (void)press(kb_KeyMath, &editor, true, APP_VIEW_EXPRESSIONS);
        ASSERT_STR_EQ(full, editor.text);
        ASSERT_EQ_INT(EDITOR_CAPACITY - 1, editor.length);
        ASSERT_EQ_INT(cursor, editor.cursor);
    }
    ASSERT_TRUE(editor_move(&editor, 1));
    (void)press(kb_KeyMath, &editor, true, APP_VIEW_EXPRESSIONS);
    ASSERT_STR_EQ(full, editor.text);
    ASSERT_EQ_INT(3, editor.cursor);
    full[1] = '=';
    ASSERT_TRUE(editor_set_text(&editor, full));
    ASSERT_TRUE(editor_move(&editor, 2 - editor.cursor));
    (void)press(kb_KeyMath, &editor, true, APP_VIEW_EXPRESSIONS);
    full[1] = '<';
    ASSERT_STR_EQ(full, editor.text);
    ASSERT_EQ_INT(EDITOR_CAPACITY - 1, editor.length);
    ASSERT_EQ_INT(2, editor.cursor);
    ce_input_end();
}

static void test_former_second_relation_shortcuts_are_consumed_without_mutation(void) {
    static const kb_lkey_t keys[] = {kb_KeyMath, kb_KeyApps, kb_KeyPrgm, kb_KeyVars};
    EditorBuffer editor;
    size_t i;

    reset_fake_hardware();
    ce_input_begin();
    editor_init(&editor);
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        ASSERT_TRUE(editor_set_text(&editor, "x=y"));
        ASSERT_TRUE(editor_move(&editor, -1));
        (void)press(kb_Key2nd, &editor, true, APP_VIEW_EXPRESSIONS);
        (void)press(keys[i], &editor, true, APP_VIEW_EXPRESSIONS);
        ASSERT_STR_EQ("x=y", editor.text);
        ASSERT_EQ_INT(3, editor.length);
        ASSERT_EQ_INT(2, editor.cursor);
        (void)press(kb_Key1, &editor, true, APP_VIEW_EXPRESSIONS);
        ASSERT_STR_EQ("x=1y", editor.text);
    }
    (void)press(kb_KeyAlpha, &editor, true, APP_VIEW_EXPRESSIONS);
    (void)press(kb_KeyMath, &editor, true, APP_VIEW_EXPRESSIONS);
    ASSERT_STR_EQ("x=1ay", editor.text);
    ce_input_end();
}

static void hold_keys(kb_lkey_t first, kb_lkey_t second) {
    memset(kb_Data, 0, sizeof(kb_Data));
    kb_Data[first >> 8] |= (uint8_t)first;
    kb_Data[second >> 8] |= (uint8_t)second;
}

static void test_overlapping_edges_are_delivered_once_and_can_be_repressed(void) {
    EditorBuffer editor;

    reset_fake_hardware();
    ce_input_begin();
    editor_init(&editor);
    hold_keys(kb_KeyGraphVar, kb_KeyAdd);
    ASSERT_EQ_INT(APP_ACTION_NONE, ce_input_poll(&editor, true, APP_VIEW_EXPRESSIONS));
    ASSERT_STR_EQ("x", editor.text);
    ASSERT_EQ_INT(APP_ACTION_NONE, ce_input_poll(&editor, true, APP_VIEW_EXPRESSIONS));
    ASSERT_STR_EQ("x+", editor.text);
    ASSERT_EQ_INT(APP_ACTION_NONE, ce_input_poll(&editor, true, APP_VIEW_EXPRESSIONS));
    ASSERT_STR_EQ("x+", editor.text);
    hold_keys(0, kb_KeyAdd);
    (void)ce_input_poll(&editor, true, APP_VIEW_EXPRESSIONS);
    hold_keys(kb_KeyGraphVar, kb_KeyAdd);
    (void)ce_input_poll(&editor, true, APP_VIEW_EXPRESSIONS);
    ASSERT_STR_EQ("x+x", editor.text);
    ce_input_end();
}

static void test_same_scan_modifiers_precede_ordinary_keys(void) {
    static const struct { kb_lkey_t modifier, key; const char *text; } cases[] = {
        {kb_Key2nd, kb_KeySquare, "sqrt("},
        {kb_KeyAlpha, kb_KeySquare, "i"}
    };
    EditorBuffer editor;
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        reset_fake_hardware();
        ce_input_begin();
        editor_init(&editor);
        hold_keys(cases[i].modifier, cases[i].key);
        ASSERT_EQ_INT(APP_ACTION_NONE, ce_input_poll(&editor, true, APP_VIEW_EXPRESSIONS));
        ASSERT_STR_EQ("", editor.text);
        (void)ce_input_poll(&editor, true, APP_VIEW_EXPRESSIONS);
        ASSERT_STR_EQ(cases[i].text, editor.text);
        (void)ce_input_poll(&editor, true, APP_VIEW_EXPRESSIONS);
        ASSERT_STR_EQ(cases[i].text, editor.text);
        (void)press(kb_Key1, &editor, true, APP_VIEW_EXPRESSIONS);
        ASSERT_STR_EQ(i == 0 ? "sqrt(1" : "i1", editor.text);
        ce_input_end();
    }
    reset_fake_hardware();
    ce_input_begin();
    hold_keys(kb_Key2nd, kb_KeyMode);
    ASSERT_EQ_INT(APP_ACTION_NONE, ce_input_poll(NULL, false, APP_VIEW_GRAPH));
    ASSERT_EQ_INT(APP_ACTION_QUIT, ce_input_poll(NULL, false, APP_VIEW_GRAPH));
    ASSERT_EQ_INT(APP_ACTION_NONE, ce_input_poll(NULL, false, APP_VIEW_GRAPH));
    ce_input_end();
}

static void test_context_actions_discard_queued_ordinary_edges(void) {
    static const struct { kb_lkey_t key; AppAction action; } cases[] = {
        {kb_KeyYequ, APP_ACTION_SHOW_EXPRESSIONS},
        {kb_KeyWindow, APP_ACTION_SHOW_SPLIT},
        {kb_KeyZoom, APP_ACTION_TOGGLE_GRAPH_MODE},
        {kb_KeyTrace, APP_ACTION_OPEN_MENU},
        {kb_KeyGraph, APP_ACTION_SHOW_GRAPH}
    };
    EditorBuffer editor;
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        reset_fake_hardware();
        ce_input_begin();
        editor_init(&editor);
        hold_keys(cases[i].key, kb_KeyGraphVar);
        kb_Data[kb_KeyAdd >> 8] |= (uint8_t)kb_KeyAdd;
        ASSERT_EQ_INT(cases[i].action, ce_input_poll(&editor, true, APP_VIEW_SPLIT));
        ASSERT_EQ_INT(APP_ACTION_NONE, ce_input_poll(&editor, true, APP_VIEW_EXPRESSIONS));
        ASSERT_EQ_INT(APP_ACTION_NONE, ce_input_poll(&editor, true, APP_VIEW_EXPRESSIONS));
        ASSERT_STR_EQ("", editor.text);
        (void)press(kb_Key1, &editor, true, APP_VIEW_EXPRESSIONS);
        ASSERT_STR_EQ("1", editor.text);
        ce_input_end();
    }
}

static void test_pending_edges_survive_release_and_discard_does_not_requeue_held_keys(void) {
    EditorBuffer editor;

    reset_fake_hardware();
    ce_input_begin();
    editor_init(&editor);
    hold_keys(kb_KeyGraphVar, kb_KeyAdd);
    (void)ce_input_poll(&editor, true, APP_VIEW_EXPRESSIONS);
    hold_keys(0, 0);
    (void)ce_input_poll(&editor, true, APP_VIEW_EXPRESSIONS);
    ASSERT_STR_EQ("x+", editor.text);

    hold_keys(kb_KeyGraphVar, kb_KeyAdd);
    (void)ce_input_poll(&editor, true, APP_VIEW_EXPRESSIONS);
    ce_input_discard_pending();
    (void)ce_input_poll(&editor, true, APP_VIEW_EXPRESSIONS);
    ASSERT_STR_EQ("x+x", editor.text);

    (void)press(kb_KeyAlpha, &editor, true, APP_VIEW_EXPRESSIONS);
    ce_input_discard_pending();
    (void)press(kb_KeyMath, &editor, true, APP_VIEW_EXPRESSIONS);
    ASSERT_STR_EQ("x+xa", editor.text);

    hold_keys(kb_KeyGraphVar, kb_KeyAdd);
    (void)ce_input_poll(&editor, true, APP_VIEW_EXPRESSIONS);
    ce_input_begin();
    (void)ce_input_poll(&editor, true, APP_VIEW_EXPRESSIONS);
    ASSERT_STR_EQ("x+xax", editor.text);
    (void)press(kb_Key1, &editor, true, APP_VIEW_EXPRESSIONS);
    ASSERT_STR_EQ("x+xax1", editor.text);
    ce_input_end();
}

static void test_navigation_matrix_and_hidden_editor_safety(void) {
    static const struct { kb_lkey_t key; AppAction action; } graph[] = {
        {kb_KeyLeft, APP_ACTION_PAN_LEFT},
        {kb_KeyRight, APP_ACTION_PAN_RIGHT},
        {kb_KeyUp, APP_ACTION_PAN_UP},
        {kb_KeyDown, APP_ACTION_PAN_DOWN},
        {kb_KeyAdd, APP_ACTION_ZOOM_IN},
        {kb_KeySub, APP_ACTION_ZOOM_OUT},
        {kb_KeyClear, APP_ACTION_NONE}
    };
    EditorBuffer editor;
    size_t i;
    int editing;
    AppView view;

    reset_fake_hardware();
    ce_input_begin();
    editor_init(&editor);
    ASSERT_TRUE(editor_set_text(&editor, "x+1"));
    for (view = APP_VIEW_EXPRESSIONS; view <= APP_VIEW_SPLIT; view++) {
        ASSERT_EQ_INT(APP_ACTION_MOVE_SELECTION_UP,
                      press(kb_KeyUp, &editor, false, view));
        ASSERT_EQ_INT(APP_ACTION_MOVE_SELECTION_DOWN,
                      press(kb_KeyDown, &editor, false, view));
    }
    for (editing = 0; editing <= 1; editing++) {
        for (i = 0; i < sizeof(graph) / sizeof(graph[0]); i++) {
            ASSERT_EQ_INT(graph[i].action,
                          press(graph[i].key, &editor, editing, APP_VIEW_GRAPH));
            ASSERT_STR_EQ("x+1", editor.text);
            ASSERT_EQ_INT(3, editor.cursor);
        }
        ASSERT_EQ_INT(APP_ACTION_NONE,
                      press(kb_KeyDel, &editor, editing, APP_VIEW_GRAPH));
        ASSERT_EQ_INT(APP_ACTION_NONE,
                      press(kb_Key1, &editor, editing, APP_VIEW_GRAPH));
        ASSERT_EQ_INT(APP_ACTION_NONE,
                      press(kb_KeyEnter, &editor, editing, APP_VIEW_GRAPH));
        ASSERT_STR_EQ("x+1", editor.text);
    }
    ce_input_end();
}

static void test_expression_and_split_editing_still_work(void) {
    EditorBuffer editor;
    AppView view;

    for (view = APP_VIEW_EXPRESSIONS; view <= APP_VIEW_SPLIT; view++) {
        reset_fake_hardware();
        ce_input_begin();
        editor_init(&editor);
        ASSERT_EQ_INT(APP_ACTION_BEGIN_EDIT, press(kb_KeyEnter, NULL, false, view));
        ASSERT_EQ_INT(APP_ACTION_CLEAR_EXPRESSION,
                      press(kb_KeyClear, NULL, false, view));
        (void)press(kb_KeyGraphVar, &editor, true, view);
        (void)press(kb_KeyAdd, &editor, true, view);
        (void)press(kb_Key1, &editor, true, view);
        ASSERT_STR_EQ("x+1", editor.text);
        (void)press(kb_KeyLeft, &editor, true, view);
        ASSERT_EQ_INT(2, editor.cursor);
        (void)press(kb_KeyDel, &editor, true, view);
        ASSERT_STR_EQ("x1", editor.text);
        (void)press(kb_KeyRight, &editor, true, view);
        ASSERT_EQ_INT(2, editor.cursor);
        ASSERT_EQ_INT(APP_ACTION_COMMIT_EDIT,
                      press(kb_KeyEnter, &editor, true, view));
        ASSERT_EQ_INT(APP_ACTION_CLEAR_EXPRESSION,
                      press(kb_KeyClear, &editor, true, view));
        (void)press(kb_KeyAlpha, &editor, true, view);
        (void)press(kb_KeyMath, &editor, true, view);
        ASSERT_STR_EQ("x1a", editor.text);
        (void)press(kb_Key2nd, &editor, true, view);
        (void)press(kb_KeySquare, &editor, true, view);
        ASSERT_STR_EQ("x1asqrt(", editor.text);
        ce_input_end();
    }
}

static void test_top_keys_and_quit_in_every_view_and_edit_state(void) {
    static const struct { kb_lkey_t key; AppAction action; } top[] = {
        {kb_KeyYequ, APP_ACTION_SHOW_EXPRESSIONS},
        {kb_KeyWindow, APP_ACTION_SHOW_SPLIT},
        {kb_KeyZoom, APP_ACTION_TOGGLE_GRAPH_MODE},
        {kb_KeyTrace, APP_ACTION_OPEN_MENU},
        {kb_KeyGraph, APP_ACTION_SHOW_GRAPH}
    };
    EditorBuffer editor;
    AppView view;
    int editing;
    size_t i;

    for (view = APP_VIEW_EXPRESSIONS; view <= APP_VIEW_GRAPH; view++) {
        for (editing = 0; editing <= 1; editing++) {
            reset_fake_hardware();
            ce_input_begin();
            editor_init(&editor);
            ASSERT_TRUE(editor_set_text(&editor, "x+1"));
            for (i = 0; i < sizeof(top) / sizeof(top[0]); i++) {
                ASSERT_EQ_INT(top[i].action, press(top[i].key, &editor, editing, view));
                ASSERT_STR_EQ("x+1", editor.text);
            }
            ASSERT_EQ_INT(APP_ACTION_NONE, press(kb_Key2nd, &editor, editing, view));
            ASSERT_EQ_INT(APP_ACTION_QUIT, press(kb_KeyMode, &editor, editing, view));
            ce_input_end();
        }
    }
}

static void test_navigation_active_reads_held_state_without_side_effects(void) {
    static const kb_lkey_t keys[] = {
        kb_KeyLeft, kb_KeyRight, kb_KeyUp, kb_KeyDown, kb_KeyAdd, kb_KeySub,
        kb_KeyClear, kb_KeyYequ, kb_KeyWindow, kb_KeyZoom, kb_KeyTrace,
        kb_KeyGraph, kb_Key2nd, kb_KeyMode
    };
    size_t i;

    reset_fake_hardware();
    ce_input_begin();
    ASSERT_FALSE(ce_input_graph_navigation_active());
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        kb_lkey_t script[] = {0, keys[i]};
        script_keys(script, 2);
        kb_Scan();
        (void)ce_input_poll(NULL, false, APP_VIEW_GRAPH);
        kb_Scan();
        kb_SetMode(MODE_3_CONTINUOUS);
        ASSERT_EQ_INT(i < 6, ce_input_graph_navigation_active());
        ASSERT_EQ_INT(i < 6, ce_input_graph_navigation_active());
        ASSERT_EQ_INT(2, scan_key_index);
        ASSERT_EQ_INT(MODE_3_CONTINUOUS, kb_GetMode());
        if (i == 0) {
            ASSERT_EQ_INT(APP_ACTION_PAN_LEFT, ce_input_poll(NULL, false, APP_VIEW_GRAPH));
            ASSERT_EQ_INT(APP_ACTION_NONE, ce_input_poll(NULL, false, APP_VIEW_GRAPH));
        }
    }
    ce_input_end();
    ASSERT_EQ_INT(MODE_0_IDLE, kb_GetMode());
    ASSERT_FALSE(ce_input_graph_navigation_active());
    ce_input_begin();
    ASSERT_EQ_INT(MODE_3_CONTINUOUS, kb_GetMode());
    ASSERT_EQ_INT(APP_ACTION_NONE, ce_input_poll(NULL, false, APP_VIEW_GRAPH));
    ASSERT_EQ_INT(APP_ACTION_NONE, press(kb_Key2nd, NULL, false, APP_VIEW_GRAPH));
    ASSERT_FALSE(ce_input_graph_navigation_active());
    ASSERT_EQ_INT(APP_ACTION_QUIT, press(kb_KeyMode, NULL, false, APP_VIEW_GRAPH));
    ce_input_end();
}

static void test_visiting_graph_retains_the_active_project_draft(void) {
    AppState state;
    Project project;
    EditorBuffer *editor;

    reset_fake_hardware();
    ce_input_begin();
    app_state_init(&state);
    project_init(&project);
    ASSERT_TRUE(project_set_draft(&project, 0, "x+1"));
    ASSERT_TRUE(project_commit_line(&project, 0).ok);
    ASSERT_TRUE(project_begin_edit(&project, 0));
    editor = project_editor(&project);
    ASSERT_TRUE(editor_set_text(editor, "x+2"));
    project.dirty = false;
    app_handle_action(&state, press(kb_KeyGraph, editor, true, state.view));
    ASSERT_EQ_INT(APP_VIEW_GRAPH, state.view);
    app_handle_action(&state, press(kb_KeyUp, editor, true, state.view));
    ASSERT_FLOAT_EQ(1.225f, state.viewport.center_y);
    ASSERT_EQ_INT(APP_ACTION_NONE, press(kb_KeyClear, editor, true, state.view));
    app_handle_action(&state, press(kb_KeyYequ, editor, true, state.view));
    ASSERT_TRUE(project_is_editing(&project));
    ASSERT_STR_EQ("x+2", project_line_display_text(&project, 0));
    ASSERT_STR_EQ("x+1", project_line_source(&project, 0));
    ASSERT_EQ_INT(3, editor->cursor);
    ASSERT_FALSE(project.dirty);
    project_dispose(&project);
    ce_input_end();
}

static void test_modal_menu_consumes_navigation_without_moving_background(void) {
    static const kb_lkey_t keys[] = {0, kb_KeyRight, 0, 0, kb_KeyDown, 0,
                                      0, kb_KeyClear, 0};
    AppState state;
    EditorBuffer editor;

    reset_fake_hardware();
    ce_input_begin();
    app_state_init(&state);
    state.view = APP_VIEW_GRAPH;
    editor_init(&editor);
    ASSERT_TRUE(editor_set_text(&editor, "x+1"));
    script_keys(keys, sizeof(keys) / sizeof(keys[0]));
    ASSERT_EQ_INT(APP_ACTION_NONE, ce_ui_show_menu());
    ASSERT_FLOAT_EQ(0, state.viewport.center_x);
    ASSERT_FLOAT_EQ(0, state.viewport.center_y);
    ASSERT_FLOAT_EQ(0.0625f, state.viewport.units_per_pixel);
    ASSERT_EQ_INT(APP_ACTION_NONE, ce_input_poll(&editor, true, APP_VIEW_GRAPH));
    ASSERT_STR_EQ("x+1", editor.text);
    ASSERT_EQ_INT(MODE_3_CONTINUOUS, kb_GetMode());
    ce_input_end();
}

static void test_second_trig_keys_insert_inverse_names_and_consume_modifier(void) {
    static const struct { kb_lkey_t key; const char *text, *temporary, *normal; } cases[] = {
        {kb_KeySin, "arcsin(", "abs(", "sin("},
        {kb_KeyCos, "arccos(", "exp(", "cos("},
        {kb_KeyTan, "arctan(", "floor(", "tan("}
    };
    EditorBuffer editor;
    AppView view;
    size_t index;

    for (view = APP_VIEW_EXPRESSIONS; view <= APP_VIEW_SPLIT; view++) {
        reset_fake_hardware();
        ce_input_begin();
        editor_init(&editor);
        for (index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
            ASSERT_TRUE(editor_set_text(&editor, ""));
            (void)press(kb_Key2nd, &editor, true, view);
            (void)press(cases[index].key, &editor, true, view);
            ASSERT_STR_EQ(cases[index].text, editor.text);
            ASSERT_TRUE(strstr(editor.text, cases[index].temporary) == NULL);
            ASSERT_EQ_INT(7, editor.cursor);
            ASSERT_TRUE(editor_set_text(&editor, ""));
            (void)press(cases[index].key, &editor, true, view);
            ASSERT_STR_EQ(cases[index].normal, editor.text);
        }
        ce_input_end();
    }
}

void run_ce_input_tests(void) {
    test_second_trig_keys_insert_inverse_names_and_consume_modifier();
    test_math_cycles_complete_relations_at_each_caret_position();
    test_math_inserts_locally_and_rejects_invalid_clusters();
    test_math_growth_is_transactional_at_capacity();
    test_former_second_relation_shortcuts_are_consumed_without_mutation();
    test_overlapping_edges_are_delivered_once_and_can_be_repressed();
    test_same_scan_modifiers_precede_ordinary_keys();
    test_context_actions_discard_queued_ordinary_edges();
    test_pending_edges_survive_release_and_discard_does_not_requeue_held_keys();
    test_navigation_matrix_and_hidden_editor_safety();
    test_expression_and_split_editing_still_work();
    test_top_keys_and_quit_in_every_view_and_edit_state();
    test_visiting_graph_retains_the_active_project_draft();
    test_navigation_active_reads_held_state_without_side_effects();
    test_modal_menu_consumes_navigation_without_moving_background();
}
