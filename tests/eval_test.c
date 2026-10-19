#include "math/deps.h"
#include "math/eval.h"
#include "math/parser.h"
#include "math/validate.h"
#include "test.h"

static EvalResult eval_source(const char *source) {
    AstArena arena;
    ParseResult parsed = parser_parse(source, &arena);

    ASSERT_TRUE(parsed.ok);
    if (!parsed.ok) {
        EvalResult failed = {0.0f, EVAL_ERR_UNDEFINED};
        return failed;
    }
    return expression_eval(ast_arena_view(&arena), parsed.root, NULL);
}

static ValidationResult validate_source(const char *source) {
    AstArena arena;
    ParseResult parsed = parser_parse(source, &arena);

    ASSERT_TRUE(parsed.ok);
    if (!parsed.ok) {
        ValidationResult failed = {
            false, EXPR_ROLE_INVALID, VALIDATION_ERR_SHAPE, AST_NONE
        };
        return failed;
    }
    return expression_validate(ast_arena_view(&arena), parsed.root);
}

static uint8_t variable_mask_source(const char *source) {
    AstArena arena;
    ParseResult parsed = parser_parse(source, &arena);

    ASSERT_TRUE(parsed.ok);
    return parsed.ok
               ? expression_variable_mask(ast_arena_view(&arena), parsed.root)
               : 0;
}

static void test_evaluator_handles_arithmetic_and_builtins(void) {
    EvalResult result = eval_source("sin(pi/2)");

    ASSERT_EQ_INT(EVAL_OK, result.error);
    ASSERT_FLOAT_EQ(1.0f, result.value);
    ASSERT_FLOAT_EQ(9.0f, eval_source("sqrt(16)+abs(-5)").value);
}

static void test_evaluator_reports_math_errors(void) {
    ASSERT_EQ_INT(EVAL_ERR_DIV_ZERO, eval_source("1/0").error);
    ASSERT_EQ_INT(EVAL_ERR_DOMAIN, eval_source("sqrt(-1)").error);
    ASSERT_EQ_INT(EVAL_ERR_DOMAIN, eval_source("ln(0)").error);
    ASSERT_EQ_INT(EVAL_ERR_UNDEFINED, eval_source("a+1").error);
}

static EvalError resolve_coordinates(void *user, const char *name, float *value) {
    (void)user;
    if (strcmp(name, "x") == 0) {
        *value = 3.0f;
    } else if (strcmp(name, "y") == 0) {
        *value = 4.0f;
    } else {
        return EVAL_ERR_UNDEFINED;
    }
    return EVAL_OK;
}

static void test_evaluator_accepts_a_compact_ast_view(void) {
    AstArena arena;
    ParseResult parsed = parser_parse("x^2+y^2=25", &arena);
    AstNode nodes[9];
    AstView tree = {nodes, arena.count};
    EvalContext context = {resolve_coordinates, NULL, NULL};
    EvalResult result;
    ValidationResult validated;

    ASSERT_TRUE(parsed.ok);
    ASSERT_EQ_INT(9, arena.count);
    if (!parsed.ok || arena.count != 9) {
        return;
    }
    memcpy(nodes, arena.nodes, arena.count * sizeof(nodes[0]));
    result = expression_eval(tree, parsed.root, &context);
    validated = expression_validate(tree, parsed.root);

    ASSERT_EQ_INT(EVAL_OK, result.error);
    ASSERT_FLOAT_EQ(0.0f, result.value);
    ASSERT_TRUE(validated.ok);
    ASSERT_EQ_INT(EXPR_ROLE_RELATION_2D, validated.role);
    ASSERT_EQ_INT(0x03, expression_variable_mask(tree, parsed.root));
}

static void test_tree_walkers_reject_invalid_views(void) {
    const AstNode node = {1.0f, AST_NONE, AST_NONE, AST_NUMBER, 0, 0, 0};
    const AstView trees[] = {{NULL, 1}, {&node, 0}, {&node, 1}};
    const AstId roots[] = {0, 0, 1};
    size_t index;

    for (index = 0; index < sizeof(trees) / sizeof(trees[0]); index++) {
        ValidationResult validated = expression_validate(trees[index],
                                                           roots[index]);
        DependencyScan scan = dependency_scan(trees[index], roots[index],
                                              DEPENDENCY_NO_LOCAL, NULL, NULL);

        ASSERT_EQ_INT(EVAL_ERR_UNDEFINED,
                      expression_eval(trees[index], roots[index], NULL).error);
        ASSERT_FALSE(validated.ok);
        ASSERT_EQ_INT(VALIDATION_ERR_SHAPE, validated.error);
        ASSERT_EQ_INT(roots[index], validated.error_node);
        ASSERT_EQ_INT(0, expression_variable_mask(trees[index], roots[index]));
        ASSERT_EQ_INT(0, scan.mask);
        ASSERT_FALSE(scan.undefined);
    }
}

static void test_validator_classifies_general_relations(void) {
    ASSERT_EQ_INT(EXPR_ROLE_RELATION_2D, validate_source("y=x^2").role);
    ASSERT_EQ_INT(EXPR_ROLE_RELATION_2D, validate_source("x=2").role);
    ASSERT_EQ_INT(EXPR_ROLE_RELATION_2D,
                  validate_source("25=x^2+y^2").role);
    ASSERT_EQ_INT(EXPR_ROLE_RELATION_3D,
                  validate_source("x^2+y^2+z^2=16").role);
    ASSERT_EQ_INT(0x03, variable_mask_source("x^2+y^2=25"));
}

static void test_validator_recognizes_definitions_and_rejects_constants(void) {
    ValidationResult invalid_relation = validate_source("1=1");

    ASSERT_EQ_INT(EXPR_ROLE_DEFINITION, validate_source("a=2").role);
    ASSERT_EQ_INT(EXPR_ROLE_FUNCTION_DEFINITION,
                  validate_source("f(x)=x^2+a").role);
    ASSERT_FALSE(invalid_relation.ok);
    ASSERT_EQ_INT(EXPR_ROLE_INVALID, invalid_relation.role);
    ASSERT_EQ_INT(VALIDATION_ERR_VARIABLES, invalid_relation.error);
    ASSERT_EQ_INT(VALIDATION_ERR_DEFINITION,
                  validate_source("pi=3").error);
    ASSERT_EQ_INT(VALIDATION_ERR_DEFINITION,
                  validate_source("sin(x)=x").error);
    ASSERT_EQ_INT(VALIDATION_ERR_DEFINITION,
                  validate_source("x(t)=t").error);
}

void run_eval_tests(void) {
    test_evaluator_handles_arithmetic_and_builtins();
    test_evaluator_reports_math_errors();
    test_evaluator_accepts_a_compact_ast_view();
    test_tree_walkers_reject_invalid_views();
    test_validator_classifies_general_relations();
    test_validator_recognizes_definitions_and_rejects_constants();
}
