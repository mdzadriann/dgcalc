#include "math/parser.h"

#include "math/lexer.h"

#include <stdbool.h>
#include <stdint.h>

#define PARSER_MAX_DEPTH 64

typedef struct {
    Lexer lexer;
    Token current;
    AstArena *arena;
    ParseErrorCode error;
    uint16_t error_offset;
    uint8_t depth;
} Parser;

static AstId parse_addition(Parser *parser);
static AstId parse_unary(Parser *parser);

static void set_error(Parser *parser, ParseErrorCode code, uint16_t offset) {
    if (parser->error == PARSE_OK) {
        parser->error = code;
        parser->error_offset = offset;
    }
}

static void advance(Parser *parser) {
    parser->current = lexer_next(&parser->lexer);
    if (parser->current.kind == TOKEN_INVALID) {
        set_error(parser, PARSE_ERR_TOKEN, parser->current.offset);
    }
}

static AstId append_node(Parser *parser, AstNode node) {
    AstId id = ast_arena_append(parser->arena, node);
    if (id == AST_NONE) {
        set_error(parser, PARSE_ERR_NODE_LIMIT, parser->current.offset);
    }
    return id;
}

static AstId append_number(Parser *parser, float number) {
    AstNode node = {0};
    node.kind = AST_NUMBER;
    node.number = number;
    node.left = AST_NONE;
    node.right = AST_NONE;
    return append_node(parser, node);
}

static AstId append_symbol(Parser *parser, uint8_t name) {
    AstNode node = {0};
    node.kind = AST_SYMBOL;
    node.name = name;
    node.left = AST_NONE;
    node.right = AST_NONE;
    return append_node(parser, node);
}

static AstId append_operation(Parser *parser,
                              AstKind kind,
                              AstOp op,
                              AstId left,
                              AstId right) {
    AstNode node = {0};
    node.kind = (uint8_t)kind;
    node.op = (uint8_t)op;
    node.left = left;
    node.right = right;
    return append_node(parser, node);
}

static bool enter_group(Parser *parser, uint16_t offset) {
    if (parser->depth >= PARSER_MAX_DEPTH) {
        set_error(parser, PARSE_ERR_DEPTH_LIMIT, offset);
        return false;
    }
    parser->depth++;
    return true;
}

static bool begins_primary(TokenKind kind) {
    return kind == TOKEN_NUMBER || kind == TOKEN_IDENTIFIER ||
           kind == TOKEN_LEFT_PAREN;
}

static AstId parse_primary(Parser *parser) {
    Token token = parser->current;
    AstId value;

    if (parser->error != PARSE_OK) {
        return AST_NONE;
    }

    if (token.kind == TOKEN_NUMBER) {
        advance(parser);
        return append_number(parser, token.number);
    }

    if (token.kind == TOKEN_IDENTIFIER) {
        advance(parser);
        if (parser->current.kind != TOKEN_LEFT_PAREN) {
            return append_symbol(parser, token.name);
        }

        if (!enter_group(parser, parser->current.offset)) {
            return AST_NONE;
        }
        advance(parser);
        value = parse_addition(parser);
        if (parser->error == PARSE_OK &&
            parser->current.kind != TOKEN_RIGHT_PAREN) {
            set_error(parser, PARSE_ERR_EXPECTED, parser->current.offset);
        }
        if (parser->current.kind == TOKEN_RIGHT_PAREN) {
            advance(parser);
        }
        parser->depth--;
        if (value == AST_NONE || parser->error != PARSE_OK) {
            return AST_NONE;
        }

        {
            AstNode node = {0};
            node.kind = AST_CALL;
            node.name = token.name;
            node.left = value;
            node.right = AST_NONE;
            return append_node(parser, node);
        }
    }

    if (token.kind == TOKEN_LEFT_PAREN) {
        if (!enter_group(parser, token.offset)) {
            return AST_NONE;
        }
        advance(parser);
        value = parse_addition(parser);
        if (parser->error == PARSE_OK &&
            parser->current.kind != TOKEN_RIGHT_PAREN) {
            set_error(parser, PARSE_ERR_EXPECTED, parser->current.offset);
        }
        if (parser->current.kind == TOKEN_RIGHT_PAREN) {
            advance(parser);
        }
        parser->depth--;
        return parser->error == PARSE_OK ? value : AST_NONE;
    }

    set_error(parser, PARSE_ERR_EXPECTED, token.offset);
    return AST_NONE;
}

static AstId parse_power(Parser *parser) {
    AstId left = parse_primary(parser);

    if (left != AST_NONE && parser->current.kind == TOKEN_CARET) {
        AstId right;
        uint16_t offset = parser->current.offset;

        if (!enter_group(parser, offset)) {
            return AST_NONE;
        }
        advance(parser);
        right = parse_unary(parser);
        parser->depth--;
        if (right == AST_NONE) {
            return AST_NONE;
        }
        return append_operation(parser, AST_BINARY, AST_OP_POW, left, right);
    }

    return left;
}

static AstId parse_unary(Parser *parser) {
    TokenKind kind = parser->current.kind;

    if (kind == TOKEN_PLUS || kind == TOKEN_MINUS) {
        AstId operand;
        AstOp op = kind == TOKEN_MINUS ? AST_OP_NEG : AST_OP_POS;
        uint16_t offset = parser->current.offset;

        if (!enter_group(parser, offset)) {
            return AST_NONE;
        }
        advance(parser);
        operand = parse_unary(parser);
        parser->depth--;
        if (operand == AST_NONE) {
            return AST_NONE;
        }
        return append_operation(parser, AST_UNARY, op, operand, AST_NONE);
    }

    return parse_power(parser);
}

static AstId parse_multiplication(Parser *parser) {
    AstId left = parse_unary(parser);

    while (left != AST_NONE && parser->error == PARSE_OK) {
        AstOp op;
        AstId right;

        if (parser->current.kind == TOKEN_STAR) {
            op = AST_OP_MUL;
            advance(parser);
        } else if (parser->current.kind == TOKEN_SLASH) {
            op = AST_OP_DIV;
            advance(parser);
        } else if (begins_primary(parser->current.kind)) {
            op = AST_OP_MUL;
        } else {
            break;
        }

        right = parse_unary(parser);
        if (right == AST_NONE) {
            return AST_NONE;
        }
        left = append_operation(parser, AST_BINARY, op, left, right);
    }

    return left;
}

static AstId parse_addition(Parser *parser) {
    AstId left = parse_multiplication(parser);

    while (left != AST_NONE && parser->error == PARSE_OK &&
           (parser->current.kind == TOKEN_PLUS ||
            parser->current.kind == TOKEN_MINUS)) {
        AstOp op = parser->current.kind == TOKEN_PLUS ? AST_OP_ADD : AST_OP_SUB;
        AstId right;
        advance(parser);
        right = parse_multiplication(parser);
        if (right == AST_NONE) {
            return AST_NONE;
        }
        left = append_operation(parser, AST_BINARY, op, left, right);
    }

    return left;
}

static bool relation_operator(TokenKind kind, AstOp *op) {
    switch (kind) {
        case TOKEN_EQUAL:
            *op = AST_OP_EQ;
            return true;
        case TOKEN_LESS:
            *op = AST_OP_LT;
            return true;
        case TOKEN_LESS_EQUAL:
            *op = AST_OP_LE;
            return true;
        case TOKEN_GREATER:
            *op = AST_OP_GT;
            return true;
        case TOKEN_GREATER_EQUAL:
            *op = AST_OP_GE;
            return true;
        default:
            return false;
    }
}

ParseResult parser_parse(const char *source, AstArena *arena) {
    Parser parser = {0};
    ParseResult result = {0};
    AstId root;
    AstOp op;

    ast_arena_init(arena);
    lexer_init(&parser.lexer, source);
    parser.arena = arena;
    parser.error = PARSE_OK;
    advance(&parser);

    root = parse_addition(&parser);
    if (root != AST_NONE && relation_operator(parser.current.kind, &op)) {
        AstId right;
        advance(&parser);
        right = parse_addition(&parser);
        if (right != AST_NONE) {
            root = append_operation(&parser, AST_RELATION, op, root, right);
        } else {
            root = AST_NONE;
        }
    }

    if (parser.error == PARSE_OK && parser.current.kind != TOKEN_END) {
        set_error(&parser, PARSE_ERR_TRAILING, parser.current.offset);
    }

    result.ok = parser.error == PARSE_OK && root != AST_NONE;
    result.root = result.ok ? root : AST_NONE;
    result.code = parser.error;
    result.offset = parser.error_offset;
    return result;
}
