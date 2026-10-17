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
    return expression_eval(&arena, parsed.root, NULL);
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
    return expression_validate(&arena, parsed.root);
}

static uint8_t variable_mask_source(const char *source) {
    AstArena arena;
    ParseResult parsed = parser_parse(source, &arena);

    ASSERT_TRUE(parsed.ok);
    return parsed.ok ? expression_variable_mask(&arena, parsed.root) : 0;
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
    test_validator_classifies_general_relations();
    test_validator_recognizes_definitions_and_rejects_constants();
}
