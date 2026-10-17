#include "project/project.h"
#include "test.h"

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

    project_init(&project);
    project_set_and_commit(&project, 0, "a=2");
    project_set_and_commit(&project, 1, "b=a+3");
    ASSERT_FLOAT_EQ(5.0f, project_evaluate_line(&project, 1).value);

    ASSERT_TRUE(project_set_draft(&project, 0, "a="));
    failed = project_commit_line(&project, 0);
    ASSERT_FALSE(failed.ok);
    ASSERT_EQ_INT(EXPRESSION_INVALID, project.lines[0].status);
    ASSERT_STR_EQ("a=2", project.lines[0].committed_source);
    ASSERT_STR_EQ("a=", project.lines[0].draft.text);
    ASSERT_FLOAT_EQ(5.0f, project_evaluate_line(&project, 1).value);
}

static void test_undefined_names_and_cycles_are_isolated(void) {
    Project project;

    project_init(&project);
    project_set_and_commit(&project, 0, "a=2");
    project_set_and_commit(&project, 1, "b=a+3");
    project_set_and_commit(&project, 2, "c=d+1");
    ASSERT_EQ_INT(EXPRESSION_UNDEFINED, project.lines[2].status);
    ASSERT_FLOAT_EQ(5.0f, project_evaluate_line(&project, 1).value);

    project_set_and_commit(&project, 3, "d=c+1");
    ASSERT_EQ_INT(EXPRESSION_CYCLE, project.lines[2].status);
    ASSERT_EQ_INT(EXPRESSION_CYCLE, project.lines[3].status);
    ASSERT_FLOAT_EQ(5.0f, project_evaluate_line(&project, 1).value);
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
    ASSERT_FALSE(project.lines[1].has_committed);
    ASSERT_FLOAT_EQ(2.0f, project_evaluate_line(&project, 0).value);

    project_set_and_commit(&project, 2, "f(x)=x+1");
    project_set_and_commit(&project, 3, "g(x)=x+2");
    ASSERT_TRUE(project_set_draft(&project, 3, "f(t)=t+9"));
    result = project_commit_line(&project, 3);
    ASSERT_FALSE(result.ok);
    ASSERT_EQ_INT(VALIDATION_ERR_DEFINITION, result.validation_error);
    ASSERT_STR_EQ("g(x)=x+2", project.lines[3].committed_source);
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
}

void run_project_tests(void) {
    test_definitions_evaluate_and_failed_edits_are_transactional();
    test_undefined_names_and_cycles_are_isolated();
    test_user_functions_bind_their_local_parameter();
    test_duplicate_definitions_are_rejected_transactionally();
    test_reserved_names_do_not_alias_single_letter_definitions();
}
