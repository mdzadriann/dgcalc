#include "math/eval.h"

#include <math.h>
#include <stddef.h>

static EvalResult make_result(float value, EvalError error) {
    EvalResult result = {value, error};
    return result;
}

static EvalResult finite_result(float value) {
    return isfinite(value) ? make_result(value, EVAL_OK)
                           : make_result(0.0f, EVAL_ERR_NONFINITE);
}

static EvalResult eval_node(const AstArena *arena,
                            AstId id,
                            const EvalContext *context);

static EvalResult eval_symbol(uint8_t name, const EvalContext *context) {
    float value = 0.0f;
    EvalError error;

    if (name == AST_NAME_PI) {
        return make_result(3.14159265358979323846f, EVAL_OK);
    }
    if (name == 'e') {
        return make_result(2.71828182845904523536f, EVAL_OK);
    }
    if (name >= AST_NAME_SIN) {
        return make_result(0.0f, EVAL_ERR_UNDEFINED);
    }
    if (context == NULL || context->resolve_symbol == NULL) {
        return make_result(0.0f, EVAL_ERR_UNDEFINED);
    }

    error = context->resolve_symbol(context->user, ast_name_text(name), &value);
    return error == EVAL_OK ? finite_result(value) : make_result(0.0f, error);
}

static EvalResult eval_call(uint8_t name,
                            float argument,
                            const EvalContext *context) {
    float value;
    EvalError error;

    switch (name) {
        case AST_NAME_SIN:
            value = sinf(argument);
            break;
        case AST_NAME_COS:
            value = cosf(argument);
            break;
        case AST_NAME_TAN:
            value = tanf(argument);
            break;
        case AST_NAME_SQRT:
            if (argument < 0.0f) {
                return make_result(0.0f, EVAL_ERR_DOMAIN);
            }
            value = sqrtf(argument);
            break;
        case AST_NAME_ABS:
            value = fabsf(argument);
            break;
        case AST_NAME_EXP:
            value = expf(argument);
            break;
        case AST_NAME_LN:
            if (argument <= 0.0f) {
                return make_result(0.0f, EVAL_ERR_DOMAIN);
            }
            value = logf(argument);
            break;
        case AST_NAME_LOG:
            if (argument <= 0.0f) {
                return make_result(0.0f, EVAL_ERR_DOMAIN);
            }
            value = log10f(argument);
            break;
        case AST_NAME_FLOOR:
            value = floorf(argument);
            break;
        case AST_NAME_CEIL:
            value = ceilf(argument);
            break;
        default:
            if (name >= AST_NAME_PI) {
                return make_result(0.0f, EVAL_ERR_UNDEFINED);
            }
            if (context == NULL || context->resolve_call == NULL) {
                return make_result(0.0f, EVAL_ERR_UNDEFINED);
            }
            error = context->resolve_call(context->user,
                                          ast_name_text(name),
                                          argument,
                                          &value);
            return error == EVAL_OK ? finite_result(value)
                                    : make_result(0.0f, error);
    }

    return finite_result(value);
}

static EvalResult eval_binary(const AstArena *arena,
                              const AstNode *node,
                              const EvalContext *context) {
    EvalResult left = eval_node(arena, node->left, context);
    EvalResult right;
    float value;

    if (left.error != EVAL_OK) {
        return left;
    }
    right = eval_node(arena, node->right, context);
    if (right.error != EVAL_OK) {
        return right;
    }

    switch ((AstOp)node->op) {
        case AST_OP_ADD:
            value = left.value + right.value;
            break;
        case AST_OP_SUB:
            value = left.value - right.value;
            break;
        case AST_OP_MUL:
            value = left.value * right.value;
            break;
        case AST_OP_DIV:
            if (right.value == 0.0f) {
                return make_result(0.0f, EVAL_ERR_DIV_ZERO);
            }
            value = left.value / right.value;
            break;
        case AST_OP_POW:
            value = powf(left.value, right.value);
            if (isnan(value)) {
                return make_result(0.0f, EVAL_ERR_DOMAIN);
            }
            break;
        default:
            return make_result(0.0f, EVAL_ERR_DOMAIN);
    }
    return finite_result(value);
}

static EvalResult eval_node(const AstArena *arena,
                            AstId id,
                            const EvalContext *context) {
    const AstNode *node;
    EvalResult value;

    if (id == AST_NONE || id >= arena->count) {
        return make_result(0.0f, EVAL_ERR_UNDEFINED);
    }
    node = &arena->nodes[id];
    switch ((AstKind)node->kind) {
        case AST_NUMBER:
            return finite_result(node->number);
        case AST_SYMBOL:
            return eval_symbol(node->name, context);
        case AST_UNARY:
            value = eval_node(arena, node->left, context);
            if (value.error == EVAL_OK && node->op == AST_OP_NEG) {
                value.value = -value.value;
            }
            return value;
        case AST_BINARY:
            return eval_binary(arena, node, context);
        case AST_CALL:
            value = eval_node(arena, node->left, context);
            return value.error == EVAL_OK
                       ? eval_call(node->name, value.value, context)
                       : value;
        case AST_RELATION: {
            EvalResult left = eval_node(arena, node->left, context);
            EvalResult right;
            if (left.error != EVAL_OK) {
                return left;
            }
            right = eval_node(arena, node->right, context);
            return right.error == EVAL_OK
                       ? finite_result(left.value - right.value)
                       : right;
        }
        default:
            return make_result(0.0f, EVAL_ERR_UNDEFINED);
    }
}

EvalResult expression_eval(const AstArena *arena,
                           AstId root,
                           const EvalContext *context) {
    return eval_node(arena, root, context);
}
