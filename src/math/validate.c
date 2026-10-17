#include "math/validate.h"

static bool valid_id(const AstArena *arena, AstId id) {
    return id != AST_NONE && id < arena->count;
}

static uint8_t symbol_variable_mask(uint8_t name) {
    if (name == 'x') {
        return 0x01;
    }
    if (name == 'y') {
        return 0x02;
    }
    if (name == 'z') {
        return 0x04;
    }
    return 0;
}

static uint8_t node_variable_mask(const AstArena *arena, AstId id) {
    const AstNode *node;

    if (!valid_id(arena, id)) {
        return 0;
    }
    node = &arena->nodes[id];
    if (node->kind == AST_SYMBOL) {
        return symbol_variable_mask(node->name);
    }
    return (uint8_t)(node_variable_mask(arena, node->left) |
                     node_variable_mask(arena, node->right));
}

uint8_t expression_variable_mask(const AstArena *arena, AstId root) {
    return node_variable_mask(arena, root);
}

static bool is_reserved_name(uint8_t name) {
    return name == 'e' || name >= AST_NAME_PI;
}

static ValidationResult valid_result(ExprRole role) {
    ValidationResult result = {true, role, VALIDATION_OK, AST_NONE};
    return result;
}

static ValidationResult invalid_result(ValidationErrorCode error, AstId node) {
    ValidationResult result = {false, EXPR_ROLE_INVALID, error, node};
    return result;
}

ValidationResult expression_validate(const AstArena *arena, AstId root) {
    const AstNode *node;
    const AstNode *left;
    uint8_t variables;

    if (!valid_id(arena, root)) {
        return invalid_result(VALIDATION_ERR_SHAPE, root);
    }

    node = &arena->nodes[root];
    if (node->kind != AST_RELATION) {
        return valid_result(EXPR_ROLE_SCALAR);
    }
    if (!valid_id(arena, node->left) || !valid_id(arena, node->right)) {
        return invalid_result(VALIDATION_ERR_SHAPE, root);
    }

    left = &arena->nodes[node->left];
    if (node->op == AST_OP_EQ && left->kind == AST_SYMBOL &&
        symbol_variable_mask(left->name) == 0) {
        if (is_reserved_name(left->name)) {
            return invalid_result(VALIDATION_ERR_DEFINITION, node->left);
        }
        return valid_result(EXPR_ROLE_DEFINITION);
    }

    if (node->op == AST_OP_EQ && left->kind == AST_CALL) {
        if (is_reserved_name(left->name) ||
            symbol_variable_mask(left->name) != 0 ||
            !valid_id(arena, left->left) ||
            arena->nodes[left->left].kind != AST_SYMBOL ||
            is_reserved_name(arena->nodes[left->left].name)) {
            return invalid_result(VALIDATION_ERR_DEFINITION, node->left);
        }
        return valid_result(EXPR_ROLE_FUNCTION_DEFINITION);
    }

    variables = expression_variable_mask(arena, root);
    if (variables == 0) {
        return invalid_result(VALIDATION_ERR_VARIABLES, root);
    }
    return valid_result((variables & 0x04) != 0 ? EXPR_ROLE_RELATION_3D
                                                : EXPR_ROLE_RELATION_2D);
}
