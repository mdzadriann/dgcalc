#ifndef DGCALC_AST_H
#define DGCALC_AST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AST_MAX_NODES 128

typedef uint16_t AstId;
#define AST_NONE UINT16_MAX

typedef enum {
    AST_NUMBER,
    AST_SYMBOL,
    AST_UNARY,
    AST_BINARY,
    AST_CALL,
    AST_RELATION
} AstKind;

typedef enum {
    AST_OP_POS,
    AST_OP_NEG,
    AST_OP_ADD,
    AST_OP_SUB,
    AST_OP_MUL,
    AST_OP_DIV,
    AST_OP_POW,
    AST_OP_EQ,
    AST_OP_LT,
    AST_OP_LE,
    AST_OP_GT,
    AST_OP_GE
} AstOp;

typedef enum {
    AST_NAME_PI = 128,
    AST_NAME_SIN,
    AST_NAME_COS,
    AST_NAME_TAN,
    AST_NAME_SQRT,
    AST_NAME_ABS,
    AST_NAME_EXP,
    AST_NAME_LN,
    AST_NAME_LOG,
    AST_NAME_FLOOR,
    AST_NAME_CEIL
} AstName;

typedef struct {
    float number;
    AstId left;
    AstId right;
    uint8_t kind;
    uint8_t op;
    uint8_t name;
    uint8_t reserved;
} AstNode;

typedef struct {
    AstNode nodes[AST_MAX_NODES];
    uint16_t count;
} AstArena;

typedef struct {
    const AstNode *nodes;
    uint16_t count;
} AstView;

void ast_arena_init(AstArena *arena);
AstId ast_arena_append(AstArena *arena, AstNode node);
AstView ast_arena_view(const AstArena *arena);
bool ast_name_from_text(const char *text, size_t length, uint8_t *name);
const char *ast_name_text(uint8_t name);

#endif
