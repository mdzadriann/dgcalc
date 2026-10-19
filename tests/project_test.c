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

void run_project_tests(void) {
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
