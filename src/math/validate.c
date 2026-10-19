#include "math/validate.h"

static bool valid_id(AstView tree, AstId id) {
    return tree.nodes != NULL && id != AST_NONE && id < tree.count;
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

static uint8_t node_variable_mask(AstView tree, AstId id) {
    const AstNode *node;

    if (!valid_id(tree, id)) {
        return 0;
    }
    node = &tree.nodes[id];
    if (node->kind == AST_SYMBOL) {
        return symbol_variable_mask(node->name);
    }
    return (uint8_t)(node_variable_mask(tree, node->left) |
                     node_variable_mask(tree, node->right));
}

uint8_t expression_variable_mask(AstView tree, AstId root) {
    return node_variable_mask(tree, root);
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

ValidationResult expression_validate(AstView tree, AstId root) {
    const AstNode *node;
    const AstNode *left;
    uint8_t variables;

    if (!valid_id(tree, root)) {
        return invalid_result(VALIDATION_ERR_SHAPE, root);
    }

    node = &tree.nodes[root];
    if (node->kind != AST_RELATION) {
        return valid_result(EXPR_ROLE_SCALAR);
    }
    if (!valid_id(tree, node->left) || !valid_id(tree, node->right)) {
        return invalid_result(VALIDATION_ERR_SHAPE, root);
    }

    left = &tree.nodes[node->left];
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
            !valid_id(tree, left->left) ||
            tree.nodes[left->left].kind != AST_SYMBOL ||
            is_reserved_name(tree.nodes[left->left].name)) {
            return invalid_result(VALIDATION_ERR_DEFINITION, node->left);
        }
        return valid_result(EXPR_ROLE_FUNCTION_DEFINITION);
    }

    variables = expression_variable_mask(tree, root);
    if (variables == 0) {
        return invalid_result(VALIDATION_ERR_VARIABLES, root);
    }
    return valid_result((variables & 0x04) != 0 ? EXPR_ROLE_RELATION_3D
                                                : EXPR_ROLE_RELATION_2D);
}
