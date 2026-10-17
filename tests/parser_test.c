#include "math/parser.h"
#include "test.h"

#include <stddef.h>

static float eval_literal_node(const AstArena *arena, AstId id) {
    const AstNode *node = &arena->nodes[id];
    float left;
    float right;

    switch ((AstKind)node->kind) {
        case AST_NUMBER:
            return node->number;
        case AST_UNARY:
            left = eval_literal_node(arena, node->left);
            return node->op == AST_OP_NEG ? -left : left;
        case AST_BINARY:
            left = eval_literal_node(arena, node->left);
            right = eval_literal_node(arena, node->right);
            switch ((AstOp)node->op) {
                case AST_OP_ADD:
                    return left + right;
                case AST_OP_SUB:
                    return left - right;
                case AST_OP_MUL:
                    return left * right;
                case AST_OP_DIV:
                    return left / right;
                default:
                    return 0.0f;
            }
        default:
            return 0.0f;
    }
}

static float test_parse_and_eval_literal(const char *source) {
    AstArena arena;
    ParseResult result = parser_parse(source, &arena);

    ASSERT_TRUE(result.ok);
    return result.ok ? eval_literal_node(&arena, result.root) : 0.0f;
}

static AstKind root_kind(const char *source) {
    AstArena arena;
    ParseResult result = parser_parse(source, &arena);

    ASSERT_TRUE(result.ok);
    return result.ok ? (AstKind)arena.nodes[result.root].kind : AST_NUMBER;
}

static AstOp root_op(const char *source) {
    AstArena arena;
    ParseResult result = parser_parse(source, &arena);

    ASSERT_TRUE(result.ok);
    return result.ok ? (AstOp)arena.nodes[result.root].op : AST_OP_POS;
}

static ParseResult parse_nested_parentheses(size_t count) {
    char source[2 * 65 + 2];
    AstArena arena;
    size_t index;

    for (index = 0; index < count; index++) {
        source[index] = '(';
    }
    source[count] = '1';
    for (index = 0; index < count; index++) {
        source[count + 1 + index] = ')';
    }
    source[2 * count + 1] = '\0';
    return parser_parse(source, &arena);
}

static ParseResult parse_repeated_additions(size_t terms) {
    char source[2 * (AST_MAX_NODES + 1)];
    AstArena arena;
    size_t index;

    for (index = 0; index < terms; index++) {
        source[index * 2] = '1';
        if (index + 1 < terms) {
            source[index * 2 + 1] = '+';
        }
    }
    source[terms * 2 - 1] = '\0';
    return parser_parse(source, &arena);
}

static ParseResult parse_repeated_unary(size_t count) {
    char source[67];
    AstArena arena;
    size_t index;

    for (index = 0; index < count; index++) {
        source[index] = '-';
    }
    source[count] = '1';
    source[count + 1] = '\0';
    return parser_parse(source, &arena);
}

static ParseResult parse_power_chain(size_t operators) {
    char source[2 * 65 + 2];
    AstArena arena;
    size_t index;

    for (index = 0; index <= operators; index++) {
        source[index * 2] = '1';
        if (index < operators) {
            source[index * 2 + 1] = '^';
        }
    }
    source[operators * 2 + 1] = '\0';
    return parser_parse(source, &arena);
}

static ParseResult parse_mixed_depth(size_t groups, size_t unary) {
    char source[2 * 64 + 65 + 2];
    AstArena arena;
    size_t offset = 0;
    size_t index;

    for (index = 0; index < groups; index++) {
        source[offset++] = '(';
    }
    for (index = 0; index < unary; index++) {
        source[offset++] = '-';
    }
    source[offset++] = '1';
    for (index = 0; index < groups; index++) {
        source[offset++] = ')';
    }
    source[offset] = '\0';
    return parser_parse(source, &arena);
}

static void test_parser_respects_precedence_and_implicit_multiplication(void) {
    ASSERT_FLOAT_EQ(14.0f, test_parse_and_eval_literal("2+3*4"));
    ASSERT_FLOAT_EQ(20.0f, test_parse_and_eval_literal("2(3+7)"));
}

static void test_parser_accepts_relations_in_either_direction(void) {
    ASSERT_EQ_INT(AST_RELATION, root_kind("25=x^2+y^2"));
    ASSERT_EQ_INT(AST_OP_EQ, root_op("x^2+y^2=25"));
}

static void test_parser_stops_at_fixed_limits(void) {
    ASSERT_EQ_INT(PARSE_ERR_DEPTH_LIMIT, parse_nested_parentheses(65).code);
    ASSERT_EQ_INT(PARSE_ERR_DEPTH_LIMIT, parse_repeated_unary(65).code);
    ASSERT_EQ_INT(PARSE_ERR_DEPTH_LIMIT, parse_power_chain(65).code);
    ASSERT_TRUE(parse_mixed_depth(32, 32).ok);
    ASSERT_EQ_INT(PARSE_ERR_DEPTH_LIMIT, parse_mixed_depth(32, 33).code);
    ASSERT_EQ_INT(PARSE_ERR_NODE_LIMIT,
                  parse_repeated_additions(AST_MAX_NODES + 1).code);
}

void run_parser_tests(void) {
    test_parser_respects_precedence_and_implicit_multiplication();
    test_parser_accepts_relations_in_either_direction();
    test_parser_stops_at_fixed_limits();
}
