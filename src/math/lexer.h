#ifndef DGCALC_LEXER_H
#define DGCALC_LEXER_H

#include <stdint.h>

typedef enum {
    TOKEN_END,
    TOKEN_INVALID,
    TOKEN_NUMBER,
    TOKEN_IDENTIFIER,
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_COMMA,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_CARET,
    TOKEN_EQUAL,
    TOKEN_LESS,
    TOKEN_LESS_EQUAL,
    TOKEN_GREATER,
    TOKEN_GREATER_EQUAL
} TokenKind;

typedef struct {
    TokenKind kind;
    float number;
    uint8_t name;
    uint16_t offset;
} Token;

typedef struct {
    const char *source;
    uint16_t position;
} Lexer;

void lexer_init(Lexer *lexer, const char *source);
Token lexer_next(Lexer *lexer);

#endif
