#include "project/project.h"
#include "test.h"
#include "project_memory.h"

static void project_set_and_commit(Project *project,
                                   uint8_t index,
                                   const char *source) {
    CommitResult result;

    ASSERT_TRUE(project_set_draft(project, index, source));
    result = project_commit_line(project, index);
    ASSERT_TRUE(result.ok);
}

static void test_definitions_evaluate_and_failed_edits_are_transactional(void) {
    Project project;
    CommitResult failed;
    AstView tree;
    AstId root;

    project_init(&project);
    project_set_and_commit(&project, 0, "a=2");
    project_set_and_commit(&project, 1, "b=a+3");
    ASSERT_FLOAT_EQ(5.0f, project_evaluate_line(&project, 1).value);

    ASSERT_TRUE(project_begin_edit(&project, 0));
    ASSERT_TRUE(project_set_draft(&project, 0, "a="));
    failed = project_commit_line(&project, 0);
    ASSERT_FALSE(failed.ok);
    ASSERT_TRUE(project_is_editing(&project));
    ASSERT_EQ_INT(EXPRESSION_INVALID, project_line_status(&project, 0));
    ASSERT_STR_EQ("a=2", project_line_source(&project, 0));
    ASSERT_STR_EQ("a=", project_line_display_text(&project, 0));
    ASSERT_TRUE(project_line_tree(&project, 0, &tree, &root));
    ASSERT_TRUE(expression_validate(tree, root).ok);
    ASSERT_EQ_INT(EXPR_ROLE_DEFINITION, project_line_role(&project, 0));
    ASSERT_EQ_INT(EVAL_OK, project_evaluate_line(&project, 1).error);
    ASSERT_FLOAT_EQ(5.0f, project_evaluate_line(&project, 1).value);
    project_dispose(&project);
}

static void test_cancel_restores_the_line_count_and_original_dirty_flag(void) {
    Project project;
    uint8_t count_before;
    unsigned dirty;

    for (dirty = 0; dirty < 2; dirty++) {
        project_init(&project);
        project_set_and_commit(&project, 0, "a=2");
        project.dirty = dirty != 0;
        count_before = project.count;
        ASSERT_TRUE(project_begin_edit(&project, 0));
        ASSERT_EQ_INT(0, project_editing_index(&project));
        ASSERT_TRUE(project_editor(&project) != NULL);
        ASSERT_TRUE(project_set_draft(&project, 0, "a="));
        ASSERT_FALSE(project_validate_draft(&project, 0).ok);
        project_cancel_edit(&project);
        ASSERT_FALSE(project_is_editing(&project));
        ASSERT_TRUE(project_editor(&project) == NULL);
        ASSERT_STR_EQ("a=2", project_line_source(&project, 0));
        ASSERT_STR_EQ("a=2", project_line_display_text(&project, 0));
        ASSERT_EQ_INT(EXPRESSION_VALID, project_line_status(&project, 0));
        ASSERT_EQ_INT(count_before, project.count);
        ASSERT_EQ_INT(dirty, project.dirty);

        ASSERT_TRUE(project_begin_edit(&project, 15));
        ASSERT_TRUE(project_set_draft(&project, 15, "x"));
        project_cancel_edit(&project);
        ASSERT_EQ_INT(count_before, project.count);
        ASSERT_EQ_INT(dirty, project.dirty);
        ASSERT_STR_EQ("", project_line_display_text(&project, 15));
        project_dispose(&project);
    }
}

static void test_edit_boundary_rejects_invalid_slots_and_clears_stale_views(void) {
    Project project;
    AstView tree;
    AstId root;
    unsigned index;

    project_init(&project);
    project_cancel_edit(&project);
    ASSERT_FALSE(project_is_editing(&project));
    ASSERT_EQ_INT(PROJECT_MAX_EXPRESSIONS, project_editing_index(&project));
    ASSERT_TRUE(project_editor(&project) == NULL);
    ASSERT_EQ_INT(0, project_editor_cursor(&project));
    for (index = 16; index <= 255; index++) {
        ASSERT_FALSE(project_begin_edit(&project, (uint8_t)index));
        ASSERT_STR_EQ("", project_line_source(&project, (uint8_t)index));
        ASSERT_STR_EQ("", project_line_display_text(&project, (uint8_t)index));
        ASSERT_FALSE(project_line_has_committed(&project, (uint8_t)index));
        ASSERT_EQ_INT(EXPRESSION_EMPTY,
                      project_line_status(&project, (uint8_t)index));
        ASSERT_EQ_INT(EXPR_ROLE_INVALID,
                      project_line_role(&project, (uint8_t)index));
    }
    project_set_and_commit(&project, 0, "a=2");
    ASSERT_TRUE(project_line_tree(&project, 0, &tree, &root));
    ASSERT_FALSE(project_line_tree(&project, 16, &tree, &root));
    ASSERT_TRUE(tree.nodes == NULL);
    ASSERT_EQ_INT(0, tree.count);
    ASSERT_EQ_INT(AST_NONE, root);
    ASSERT_FALSE(project_line_tree(&project, 1, &tree, &root));
    ASSERT_TRUE(project_begin_edit(&project, 0));
    ASSERT_FALSE(project_begin_edit(&project, 1));
    ASSERT_EQ_INT(0, project_editing_index(&project));
    ASSERT_EQ_INT(3, project_editor_cursor(&project));
    ASSERT_TRUE(project_set_draft(&project, 0, "a=4"));
    ASSERT_TRUE(project_commit_line(&project, 0).ok);
    ASSERT_FALSE(project_is_editing(&project));
    ASSERT_TRUE(project_editor(&project) == NULL);
    ASSERT_EQ_INT(PROJECT_MAX_EXPRESSIONS, project_editing_index(&project));
    ASSERT_EQ_INT(0, project_editor_cursor(&project));
    ASSERT_FLOAT_EQ(4.0f, project_evaluate_line(&project, 0).value);
    ASSERT_TRUE(project_begin_edit(&project, 0));
    ASSERT_TRUE(project_set_draft(&project, 0, ""));
    ASSERT_TRUE(project_commit_line(&project, 0).ok);
    ASSERT_FALSE(project_is_editing(&project));
    ASSERT_FALSE(project_line_tree(&project, 0, &tree, &root));
    ASSERT_TRUE(tree.nodes == NULL);
    ASSERT_EQ_INT(AST_NONE, root);
    project_dispose(&project);
}

static void test_undefined_names_and_cycles_are_isolated(void) {
    Project project;

    project_init(&project);
    project_set_and_commit(&project, 0, "a=2");
    project_set_and_commit(&project, 1, "b=a+3");
    project_set_and_commit(&project, 2, "c=d+1");
    ASSERT_EQ_INT(EXPRESSION_UNDEFINED, project_line_status(&project, 2));
    ASSERT_FLOAT_EQ(5.0f, project_evaluate_line(&project, 1).value);

    project_set_and_commit(&project, 3, "d=c+1");
    ASSERT_EQ_INT(EXPRESSION_CYCLE, project_line_status(&project, 2));
    ASSERT_EQ_INT(EXPRESSION_CYCLE, project_line_status(&project, 3));
    ASSERT_FLOAT_EQ(5.0f, project_evaluate_line(&project, 1).value);
    project_dispose(&project);
}

static void test_user_functions_bind_their_local_parameter(void) {
    Project project;
    EvalResult result;

    project_init(&project);
    project_set_and_commit(&project, 0, "a=2");
    project_set_and_commit(&project, 1, "f(x)=x^2+a");
    project_set_and_commit(&project, 2, "f(3)");
    result = project_evaluate_line(&project, 2);

    ASSERT_EQ_INT(EVAL_OK, result.error);
    ASSERT_FLOAT_EQ(11.0f, result.value);
    project_dispose(&project);
}

static void test_duplicate_definitions_are_rejected_transactionally(void) {
    Project project;
    CommitResult result;

    project_init(&project);
    project_set_and_commit(&project, 0, "a=2");
    ASSERT_TRUE(project_set_draft(&project, 1, "a=9"));
    result = project_commit_line(&project, 1);
    ASSERT_FALSE(result.ok);
    ASSERT_EQ_INT(VALIDATION_ERR_DEFINITION, result.validation_error);
    ASSERT_FALSE(project_line_has_committed(&project, 1));
    ASSERT_FLOAT_EQ(2.0f, project_evaluate_line(&project, 0).value);

    project_set_and_commit(&project, 2, "f(x)=x+1");
    project_set_and_commit(&project, 3, "g(x)=x+2");
    ASSERT_TRUE(project_set_draft(&project, 3, "f(t)=t+9"));
    result = project_commit_line(&project, 3);
    ASSERT_FALSE(result.ok);
    ASSERT_EQ_INT(VALIDATION_ERR_DEFINITION, result.validation_error);
    ASSERT_STR_EQ("g(x)=x+2", project_line_source(&project, 3));
    project_dispose(&project);
}

static void test_reserved_names_do_not_alias_single_letter_definitions(void) {
    Project project;

    project_init(&project);
    project_set_and_commit(&project, 0, "s=7");
    project_set_and_commit(&project, 1, "sin");
    project_set_and_commit(&project, 2, "p(x)=x+1");
    project_set_and_commit(&project, 3, "pi(2)");

    ASSERT_EQ_INT(EVAL_ERR_UNDEFINED,
                  project_evaluate_line(&project, 1).error);
    ASSERT_EQ_INT(EVAL_ERR_UNDEFINED,
                  project_evaluate_line(&project, 3).error);
    project_dispose(&project);
}

static void test_clear_line_removes_definition_and_rebuilds_dependents(void) {
    Project project;

    project_init(&project);
    project_set_and_commit(&project, 0, "a=2");
    project_set_and_commit(&project, 1, "b=a+3");
    project.dirty = false;

    ASSERT_TRUE(project_clear_line(&project, 0));
    ASSERT_STR_EQ("", project_line_display_text(&project, 0));
    ASSERT_STR_EQ("", project_line_source(&project, 0));
    ASSERT_FALSE(project_line_has_committed(&project, 0));
    ASSERT_EQ_INT(EXPRESSION_EMPTY, project_line_status(&project, 0));
    ASSERT_EQ_INT(EXPRESSION_UNDEFINED, project_line_status(&project, 1));
    ASSERT_EQ_INT(EVAL_ERR_UNDEFINED,
                  project_evaluate_line(&project, 1).error);
    ASSERT_TRUE(project.dirty);

    project.dirty = false;
    ASSERT_FALSE(project_clear_line(&project, 0));
    ASSERT_FALSE(project.dirty);
    project_dispose(&project);
}

static void test_committing_empty_draft_clears_line(void) {
    Project project;
    CommitResult result;

    project_init(&project);
    project_set_and_commit(&project, 0, "x=2");
    ASSERT_TRUE(project_set_draft(&project, 0, ""));

    result = project_commit_line(&project, 0);

    ASSERT_TRUE(result.ok);
    ASSERT_FALSE(project_line_has_committed(&project, 0));
    ASSERT_EQ_INT(EXPRESSION_EMPTY, project_line_status(&project, 0));
    project_dispose(&project);
}

static EvalError circle_coordinates(void *user, const char *name, float *value) {
    (void)user;
    *value = name[0] == 'x' ? 3.0f : 4.0f;
    return EVAL_OK;
}

static void test_clear_keeps_the_shared_editor_active_without_retaining_payloads(void) {
    Project project;
    unsigned finish;

    for (finish = 0; finish < 3; finish++) {
        EditorBuffer *editor;
        project_init_with_memory(&project, counting_memory());
        project_set_and_commit(&project, 0, "a=2");
        project_set_and_commit(&project, 1, "b=a+3");
        project.dirty = false;
        ASSERT_TRUE(project_begin_edit(&project, 0));
        editor = project_editor(&project);
        ASSERT_TRUE(project_clear_line(&project, 0));
        ASSERT_TRUE(project_is_editing(&project));
        ASSERT_EQ_INT(0, project_editing_index(&project));
        ASSERT_TRUE(editor == project_editor(&project));
        ASSERT_STR_EQ("", project_line_source(&project, 0));
        ASSERT_STR_EQ("", project_line_display_text(&project, 0));
        ASSERT_EQ_INT(0, editor->length);
        ASSERT_EQ_INT(0, editor->cursor);
        ASSERT_EQ_INT(EXPRESSION_EMPTY, project_line_status(&project, 0));
        ASSERT_EQ_INT(EXPRESSION_UNDEFINED, project_line_status(&project, 1));
        ASSERT_EQ_INT(0, project.dependencies.dependencies[1]);
        ASSERT_EQ_INT(1, memory_live);
        ASSERT_EQ_INT(memory_bytes, project_payload_bytes(&project));
        ASSERT_EQ_INT(2, project.count);
        ASSERT_TRUE(project.dirty);

        if (finish == 0) {
            project_cancel_edit(&project);
            ASSERT_FALSE(project_line_has_committed(&project, 0));
            ASSERT_STR_EQ("", project_line_display_text(&project, 0));
            ASSERT_EQ_INT(EXPRESSION_EMPTY, project_line_status(&project, 0));
            ASSERT_EQ_INT(EXPRESSION_UNDEFINED, project_line_status(&project, 1));
            ASSERT_TRUE(project.dirty);
            ASSERT_EQ_INT(1, memory_live);
        } else {
            if (finish == 1 && project_editor(&project) != NULL) {
                ASSERT_TRUE(editor_insert(project_editor(&project), '4'));
            }
            ASSERT_TRUE(project_commit_line(&project, 0).ok);
            ASSERT_STR_EQ(finish == 1 ? "4" : "", project_line_source(&project, 0));
            ASSERT_EQ_INT(finish == 1 ? 2 : 1, memory_live);
        }
        ASSERT_FALSE(project_is_editing(&project));
        ASSERT_TRUE(project_editor(&project) == NULL);
        ASSERT_EQ_INT(2, project.count);
        ASSERT_TRUE(project.dirty);
        project_dispose(&project);
        ASSERT_EQ_INT(0, memory_live);
        ASSERT_EQ_INT(0, memory_bytes);
    }

    project_init_with_memory(&project, counting_memory());
    ASSERT_TRUE(project_begin_edit(&project, 15));
    ASSERT_TRUE(project_set_draft(&project, 15, "x+"));
    ASSERT_TRUE(project_clear_line(&project, 15));
    ASSERT_TRUE(project_is_editing(&project));
    project_cancel_edit(&project);
    ASSERT_EQ_INT(0, project.count);
    ASSERT_FALSE(project.dirty);
    ASSERT_EQ_INT(0, memory_live);
    project_dispose(&project);
    ASSERT_EQ_INT(0, memory_bytes);
}

static void test_sparse_payloads_allocate_once_and_release_on_clear(void) {
    static const char *const sources[] = {
        "y=x", "x=2", "x=y^2", "x^2+y^2=25", "9x^2+4y^2=36"
    };
    Project project;
    unsigned index;
    size_t nodes = 0;
    size_t source_bytes = 0;
    project_init_with_memory(&project, counting_memory());
    ASSERT_EQ_INT(0, project_payload_bytes(&project));
    ASSERT_EQ_INT(0, memory_live);
    for (index = 0; index < 5; index++) {
        AstView tree;
        AstId root;
        project_set_and_commit(&project, (uint8_t)index, sources[index]);
        ASSERT_TRUE(project_line_tree(&project, (uint8_t)index, &tree, &root));
        nodes += tree.count;
        source_bytes += strlen(project_line_source(&project, (uint8_t)index)) + 1;
        ASSERT_EQ_INT(index + 1, memory_live);
        ASSERT_EQ_INT(memory_live, memory_calls);
        ASSERT_EQ_INT(memory_bytes, project_payload_bytes(&project));
    }
    printf("Sparse memory: sizeof(Project)=%zu blank=0 five=%zu live=%zu\n",
           sizeof(Project), project_payload_bytes(&project), memory_live);
    printf("Five payloads: nodes=%zu source bytes including terminators=%zu\n",
           nodes, source_bytes);
    for (index = 0; index < 5; index++) {
        ASSERT_TRUE(project_clear_line(&project, (uint8_t)index));
    }
    ASSERT_EQ_INT(0, project_payload_bytes(&project));
    ASSERT_EQ_INT(0, memory_live);
    for (index = 0; index < 50; index++) {
        project_set_and_commit(&project, 15, "a=2");
        project_set_and_commit(&project, 15, "a=3");
        ASSERT_TRUE(project_clear_line(&project, 15));
        ASSERT_EQ_INT(0, memory_live);
        ASSERT_EQ_INT(0, memory_bytes);
    }
    for (index = 0; index < PROJECT_MAX_EXPRESSIONS; index++) {
        project_set_and_commit(&project, (uint8_t)index, "y=x");
    }
    ASSERT_EQ_INT(PROJECT_MAX_EXPRESSIONS, memory_live);
    project_dispose(&project);
    project_dispose(&project);
    ASSERT_EQ_INT(0, memory_live);
    ASSERT_EQ_INT(0, memory_bytes);
}

static void test_allocation_failure_preserves_committed_tree_and_dependencies(void) {
    Project project;
    AstView before;
    AstView after;
    AstId root_before;
    AstId root_after;
    DependencyGraph dependencies;
    ExpressionStatus status;
    size_t bytes;
    CommitResult result;
    EvalContext context = {circle_coordinates, NULL, NULL};

    project_init_with_memory(&project, counting_memory());
    project_set_and_commit(&project, 0, "x^2+y^2=25");
    project_set_and_commit(&project, 1, "a=2");
    project_set_and_commit(&project, 2, "b=a+3");
    dependencies = project.dependencies;
    status = project_line_status(&project, 0);
    bytes = memory_bytes;
    ASSERT_TRUE(project_line_tree(&project, 0, &before, &root_before));
    ASSERT_FLOAT_EQ(0.0f, expression_eval(before, root_before, &context).value);
    ASSERT_TRUE(project_set_draft(&project, 0, "9x^2+4y^2=36"));
    memory_fail_on = memory_calls + 1;
    result = project_commit_line(&project, 0);
    ASSERT_FALSE(result.ok);
    ASSERT_TRUE(result.out_of_memory);
    ASSERT_TRUE(project_is_editing(&project));
    ASSERT_STR_EQ("9x^2+4y^2=36", project_line_display_text(&project, 0));
    ASSERT_STR_EQ("x^2+y^2=25", project_line_source(&project, 0));
    ASSERT_TRUE(project_line_tree(&project, 0, &after, &root_after));
    ASSERT_TRUE(before.nodes == after.nodes);
    ASSERT_EQ_INT(before.count, after.count);
    ASSERT_EQ_INT(root_before, root_after);
    ASSERT_FLOAT_EQ(0.0f, expression_eval(after, root_after, &context).value);
    ASSERT_EQ_INT(status, project_line_status(&project, 0));
    ASSERT_TRUE(memcmp(&dependencies, &project.dependencies, sizeof(dependencies)) == 0);
    ASSERT_EQ_INT(EVAL_OK, project_evaluate_line(&project, 2).error);
    ASSERT_FLOAT_EQ(5.0f, project_evaluate_line(&project, 2).value);
    ASSERT_EQ_INT(3, memory_live);
    ASSERT_EQ_INT(bytes, memory_bytes);
    ASSERT_EQ_INT(bytes, project_payload_bytes(&project));
    project_cancel_edit(&project);
    ASSERT_TRUE(project_set_draft(&project, 15, "y=x"));
    memory_fail_on = memory_calls + 1;
    result = project_commit_line(&project, 15);
    ASSERT_TRUE(result.out_of_memory);
    ASSERT_FALSE(project_line_has_committed(&project, 15));
    ASSERT_EQ_INT(EXPRESSION_EMPTY, project_line_status(&project, 15));
    ASSERT_EQ_INT(3, memory_live);
    project_dispose(&project);
    ASSERT_EQ_INT(0, memory_live);
    ASSERT_EQ_INT(0, memory_bytes);
}

static void test_replacement_moves_ownership_and_disposes_destination_once(void) {
    Project destination;
    Project staging;
    ProjectMemory memory = counting_memory();
    project_init_with_memory(&destination, memory);
    project_init_with_memory(&staging, memory);
    project_set_and_commit(&destination, 0, "a=2");
    project_set_and_commit(&staging, 0, "a=9");
    project_set_and_commit(&staging, 1, "b=a+1");
    ASSERT_EQ_INT(3, memory_live);
    project_replace(&destination, &staging);
    ASSERT_EQ_INT(2, memory_live);
    ASSERT_EQ_INT(0, project_payload_bytes(&staging));
    ASSERT_EQ_INT(0, staging.count);
    project_dispose(&staging);
    ASSERT_FLOAT_EQ(10.0f, project_evaluate_line(&destination, 1).value);
    project_replace(&destination, &destination);
    ASSERT_EQ_INT(2, memory_live);
    project_dispose(&destination);
    ASSERT_EQ_INT(0, memory_live);
    ASSERT_EQ_INT(0, memory_bytes);
}

static void test_2d_equalities_return_signed_residuals(void) {
    static const struct {
        const char *source;
        float x;
        float y;
        float residual;
    } cases[] = {
        {"y=x", 2, 2, 0},
        {"x=2", 2, 9, 0},
        {"x^2+y^2=25", 3, 4, 0},
        {"25=x^2+y^2", 3, 4, 0},
        {"x^2+y^2=25", 0, 4, -9},
        {"25=x^2+y^2", 0, 4, 9},
        {"x=y^2", 9, 3, 0},
        {"9x^2+4y^2=36", 2, 0, 0}
    };
    Project project;
    size_t index;

    project_init(&project);
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        EvalResult result;
        project_set_and_commit(&project, 0, cases[index].source);
        ASSERT_TRUE(project_line_is_drawable_2d(&project, 0));
        result = project_evaluate_2d(&project, 0, cases[index].x, cases[index].y);
        ASSERT_EQ_INT(EVAL_OK, result.error);
        ASSERT_FLOAT_EQ(cases[index].residual, result.value);
    }
    project_dispose(&project);
}

static void test_2d_resolution_uses_locals_then_coordinates_then_definitions(void) {
    Project project;

    project_init(&project);
    project_set_and_commit(&project, 0, "a=2");
    project_set_and_commit(&project, 1, "f(t)=t^2+a");
    project_set_and_commit(&project, 2, "y=f(x)");
    project_set_and_commit(&project, 3, "g(x)=x^2+a");
    project_set_and_commit(&project, 4, "y=g(4)");
    project_set_and_commit(&project, 5, "h(a)=a+x");
    project_set_and_commit(&project, 6, "y=h(7)");
    project_set_and_commit(&project, 7, "b=x+2*y+a");
    project_set_and_commit(&project, 8, "y=b");
    project_set_and_commit(&project, 9, "f(3)");
    project_set_and_commit(&project, 10, "g(4)");
    project_set_and_commit(&project, 11, "2+3");

    ASSERT_EQ_INT(EVAL_OK, project_evaluate_2d(&project, 2, 3, 11).error);
    ASSERT_FLOAT_EQ(0, project_evaluate_2d(&project, 2, 3, 11).value);
    ASSERT_EQ_INT(EVAL_OK, project_evaluate_2d(&project, 4, 3, 18).error);
    ASSERT_FLOAT_EQ(0, project_evaluate_2d(&project, 4, 3, 18).value);
    ASSERT_EQ_INT(EVAL_OK, project_evaluate_2d(&project, 6, 3, 10).error);
    ASSERT_FLOAT_EQ(0, project_evaluate_2d(&project, 6, 3, 10).value);
    ASSERT_EQ_INT(EVAL_OK, project_evaluate_2d(&project, 8, 3, 4).error);
    ASSERT_FLOAT_EQ(-9, project_evaluate_2d(&project, 8, 3, 4).value);
    ASSERT_FLOAT_EQ(2, project_evaluate_line(&project, 0).value);
    ASSERT_FLOAT_EQ(11, project_evaluate_line(&project, 9).value);
    ASSERT_FLOAT_EQ(18, project_evaluate_line(&project, 10).value);
    ASSERT_FLOAT_EQ(5, project_evaluate_line(&project, 11).value);
    ASSERT_EQ_INT(EVAL_ERR_UNDEFINED, project_evaluate_line(&project, 7).error);
    project_dispose(&project);
}

static void test_2d_boundary_preserves_committed_graphs_during_invalid_drafts(void) {
    static const char *const skipped[] = {
        "2+3", "a=2", "f(t)=t^2", "y<x", "y<=x", "y>x", "y>=x",
        "x+y+z=1", "y=q", "b=c", "c=b", "y=b"
    };
    Project project;
    unsigned index;

    project_init(&project);
    for (index = 0; index < sizeof(skipped) / sizeof(skipped[0]); index++) {
        project_set_and_commit(&project, (uint8_t)index, skipped[index]);
    }
    ASSERT_EQ_INT(EXPRESSION_VALID, project_line_status(&project, 7));
    ASSERT_EQ_INT(EXPR_ROLE_RELATION_3D, project_line_role(&project, 7));
    ASSERT_EQ_INT(EXPRESSION_UNDEFINED, project_line_status(&project, 8));
    ASSERT_EQ_INT(EXPRESSION_CYCLE, project_line_status(&project, 11));
    ASSERT_EQ_INT(EVAL_ERR_CYCLE, project_evaluate_line(&project, 9).error);
    ASSERT_EQ_INT(EVAL_ERR_CYCLE, project_evaluate_line(&project, 11).error);
    for (index = 0; index < PROJECT_MAX_EXPRESSIONS; index++) {
        ASSERT_FALSE(project_line_is_drawable_2d(&project, (uint8_t)index));
        ASSERT_EQ_INT(EVAL_ERR_UNDEFINED,
                      project_evaluate_2d(&project, (uint8_t)index, 2, 2).error);
    }
    for (index = PROJECT_MAX_EXPRESSIONS; index <= UINT8_MAX; index++) {
        ASSERT_FALSE(project_line_is_drawable_2d(&project, (uint8_t)index));
        ASSERT_EQ_INT(EVAL_ERR_UNDEFINED,
                      project_evaluate_2d(&project, (uint8_t)index, 2, 2).error);
    }
    ASSERT_FALSE(project_line_is_drawable_2d(NULL, 0));
    ASSERT_EQ_INT(EVAL_ERR_UNDEFINED, project_evaluate_2d(NULL, 0, 2, 2).error);

    for (index = 0; index <= sizeof(skipped) / sizeof(skipped[0]); index++) {
        ASSERT_TRUE(project_set_draft(&project, (uint8_t)index, "y="));
        ASSERT_FALSE(project_commit_line(&project, (uint8_t)index).ok);
        ASSERT_FALSE(project_line_is_drawable_2d(&project, (uint8_t)index));
        ASSERT_EQ_INT(EVAL_ERR_UNDEFINED,
                      project_evaluate_2d(&project, (uint8_t)index, 2, 2).error);
        project_cancel_edit(&project);
    }

    project_set_and_commit(&project, 15, "y=x");
    ASSERT_TRUE(project_set_draft(&project, 15, "y="));
    ASSERT_FALSE(project_validate_draft(&project, 15).ok);
    ASSERT_FALSE(project_commit_line(&project, 15).ok);
    ASSERT_TRUE(project_line_has_committed(&project, 15));
    ASSERT_TRUE(project_is_editing(&project));
    ASSERT_EQ_INT(EXPRESSION_INVALID, project_line_status(&project, 15));
    ASSERT_STR_EQ("y=", project_line_display_text(&project, 15));
    ASSERT_TRUE(project_line_is_drawable_2d(&project, 15));
    ASSERT_EQ_INT(EVAL_OK, project_evaluate_2d(&project, 15, 2, 2).error);
    ASSERT_FLOAT_EQ(0, project_evaluate_2d(&project, 15, 2, 2).value);
    project_cancel_edit(&project);
    ASSERT_TRUE(project_line_is_drawable_2d(&project, 15));
    ASSERT_TRUE(project_set_draft(&project, 15, "1=1"));
    ASSERT_FALSE(project_commit_line(&project, 15).ok);
    ASSERT_TRUE(project_is_editing(&project));
    ASSERT_EQ_INT(EXPRESSION_INVALID, project_line_status(&project, 15));
    ASSERT_STR_EQ("1=1", project_line_display_text(&project, 15));
    ASSERT_TRUE(project_line_is_drawable_2d(&project, 15));
    ASSERT_EQ_INT(EVAL_OK, project_evaluate_2d(&project, 15, 2, 2).error);
    ASSERT_FLOAT_EQ(0, project_evaluate_2d(&project, 15, 2, 2).value);
    project_dispose(&project);
}

static void test_2d_evaluation_propagates_sample_errors(void) {
    static const struct {
        const char *source;
        float x;
        float y;
        EvalError error;
    } cases[] = {
        {"y=1/x", 0, 1, EVAL_ERR_DIV_ZERO},
        {"y=sqrt(x)", -1, 0, EVAL_ERR_DOMAIN},
        {"y=exp(x)", 1000, 0, EVAL_ERR_NONFINITE},
        {"y=x", INFINITY, 0, EVAL_ERR_NONFINITE},
        {"y=x", 0, NAN, EVAL_ERR_NONFINITE},
        {"x=2", 2, INFINITY, EVAL_ERR_NONFINITE},
        {"y=2", NAN, 2, EVAL_ERR_NONFINITE},
        {"y=z(1)", 0, 0, EVAL_ERR_UNDEFINED}
    };
    Project project;
    size_t index;

    project_init(&project);
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        project_set_and_commit(&project, 0, cases[index].source);
        ASSERT_TRUE(project_line_is_drawable_2d(&project, 0));
        ASSERT_EQ_INT(cases[index].error,
                      project_evaluate_2d(&project, 0, cases[index].x, cases[index].y).error);
    }
    project_dispose(&project);
}

static void test_2d_sampling_does_not_allocate_or_mutate_project(void) {
    Project project;
    const Project *readonly = &project;
    DependencyGraph dependencies;
    AstView before;
    AstView after;
    AstId root_before;
    AstId root_after;
    AstNode nodes[AST_MAX_NODES];
    unsigned calls;
    unsigned index;
    size_t bytes;

    project_init_with_memory(&project, counting_memory());
    for (index = 0; index < PROJECT_MAX_EXPRESSIONS; index++) {
        project_set_and_commit(&project, (uint8_t)index, "y=x");
    }
    ASSERT_TRUE(project_set_draft(&project, 0, "y=x+1"));
    project.dirty = false;
    dependencies = project.dependencies;
    calls = memory_calls;
    bytes = project_payload_bytes(&project);
    memory_fail_on = calls + 1;
    ASSERT_TRUE(project_line_tree(&project, 0, &before, &root_before));
    memcpy(nodes, before.nodes, before.count * sizeof(nodes[0]));
    for (index = 0; index < PROJECT_MAX_EXPRESSIONS; index++) {
        ASSERT_TRUE(project_line_is_drawable_2d(readonly, (uint8_t)index));
        ASSERT_EQ_INT(EVAL_OK, project_evaluate_2d(readonly, (uint8_t)index, 2, 2).error);
        ASSERT_FLOAT_EQ(0, project_evaluate_2d(readonly, (uint8_t)index, 2, 2).value);
        ASSERT_STR_EQ("y=x", project_line_source(readonly, (uint8_t)index));
        ASSERT_EQ_INT(EXPRESSION_VALID, project_line_status(readonly, (uint8_t)index));
    }
    ASSERT_EQ_INT(calls, memory_calls);
    ASSERT_EQ_INT(PROJECT_MAX_EXPRESSIONS, memory_live);
    ASSERT_EQ_INT(bytes, project_payload_bytes(readonly));
    ASSERT_FALSE(project.dirty);
    ASSERT_EQ_INT(PROJECT_MAX_EXPRESSIONS, project.count);
    ASSERT_TRUE(project_is_editing(readonly));
    ASSERT_EQ_INT(0, project_editing_index(readonly));
    ASSERT_EQ_INT(5, project_editor_cursor(readonly));
    ASSERT_STR_EQ("y=x+1", project_line_display_text(readonly, 0));
    ASSERT_TRUE(memcmp(&dependencies, &project.dependencies, sizeof(dependencies)) == 0);
    ASSERT_TRUE(project_line_tree(readonly, 0, &after, &root_after));
    ASSERT_TRUE(before.nodes == after.nodes);
    ASSERT_EQ_INT(before.count, after.count);
    ASSERT_EQ_INT(root_before, root_after);
    ASSERT_TRUE(memcmp(nodes, after.nodes, after.count * sizeof(nodes[0])) == 0);
    project_dispose(&project);
    ASSERT_EQ_INT(0, memory_live);
    ASSERT_EQ_INT(0, memory_bytes);
}

static void test_inverse_trig_definitions_dependencies_and_coordinates(void) {
    Project project;
    EvalResult result;

    project_init(&project);
    project_set_and_commit(&project, 0, "b=0");
    project_set_and_commit(&project, 1, "a=arcsin(b)+arccos(b)+arctan(b)");
    project_set_and_commit(&project, 2, "f(t)=arcsin(t)+arccos(t)+arctan(t)+a");
    project_set_and_commit(&project, 3, "y=f(x)");
    ASSERT_EQ_INT(1, project.dependencies.dependencies[1]);
    ASSERT_EQ_INT(2, project.dependencies.dependencies[2]);
    ASSERT_EQ_INT(4, project.dependencies.dependencies[3]);
    ASSERT_EQ_INT(EXPRESSION_VALID, project_line_status(&project, 3));
    result = project_evaluate_line(&project, 1);
    ASSERT_EQ_INT(EVAL_OK, result.error);
    ASSERT_FLOAT_EQ(1.5707963f, result.value);
    ASSERT_TRUE(project_line_is_drawable_2d(&project, 3));
    result = project_evaluate_2d(&project, 3, 0.0f, 3.1415927f);
    ASSERT_EQ_INT(EVAL_OK, result.error);
    ASSERT_FLOAT_EQ(0.0f, result.value);
    project_set_and_commit(&project, 0, "b=1");
    ASSERT_FLOAT_EQ(2.3561945f, project_evaluate_line(&project, 1).value);
    result = project_evaluate_2d(&project, 3, 0.0f, 3.9269908f);
    ASSERT_EQ_INT(EVAL_OK, result.error);
    ASSERT_FLOAT_EQ(0.0f, result.value);
    ASSERT_EQ_INT(EVAL_ERR_DOMAIN, project_evaluate_2d(&project, 3, 2, 0).error);
    ASSERT_EQ_INT(EVAL_ERR_NONFINITE,
                  project_evaluate_2d(&project, 3, INFINITY, 0).error);
    project_dispose(&project);
}

void run_project_tests(void) {
    test_inverse_trig_definitions_dependencies_and_coordinates();
    test_2d_equalities_return_signed_residuals();
    test_2d_resolution_uses_locals_then_coordinates_then_definitions();
    test_2d_boundary_preserves_committed_graphs_during_invalid_drafts();
    test_2d_evaluation_propagates_sample_errors();
    test_2d_sampling_does_not_allocate_or_mutate_project();
    test_clear_keeps_the_shared_editor_active_without_retaining_payloads();
    test_sparse_payloads_allocate_once_and_release_on_clear();
    test_allocation_failure_preserves_committed_tree_and_dependencies();
    test_replacement_moves_ownership_and_disposes_destination_once();
    test_cancel_restores_the_line_count_and_original_dirty_flag();
    test_edit_boundary_rejects_invalid_slots_and_clears_stale_views();
    test_definitions_evaluate_and_failed_edits_are_transactional();
    test_undefined_names_and_cycles_are_isolated();
    test_user_functions_bind_their_local_parameter();
    test_duplicate_definitions_are_rejected_transactionally();
    test_reserved_names_do_not_alias_single_letter_definitions();
    test_clear_line_removes_definition_and_rebuilds_dependents();
    test_committing_empty_draft_clears_line();
}
