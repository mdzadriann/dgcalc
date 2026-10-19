#ifndef DGCALC_VALIDATE_H
#define DGCALC_VALIDATE_H

#include "math/ast.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    EXPR_ROLE_INVALID,
    EXPR_ROLE_SCALAR,
    EXPR_ROLE_DEFINITION,
    EXPR_ROLE_FUNCTION_DEFINITION,
    EXPR_ROLE_RELATION_2D,
    EXPR_ROLE_RELATION_3D
} ExprRole;

typedef enum {
    VALIDATION_OK,
    VALIDATION_ERR_SHAPE,
    VALIDATION_ERR_VARIABLES,
    VALIDATION_ERR_DEFINITION
} ValidationErrorCode;

typedef struct {
    bool ok;
    ExprRole role;
    ValidationErrorCode error;
    AstId error_node;
} ValidationResult;

ValidationResult expression_validate(AstView tree, AstId root);
uint8_t expression_variable_mask(AstView tree, AstId root);

#endif
