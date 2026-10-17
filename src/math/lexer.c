#include "math/lexer.h"

#include "math/ast.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

static bool is_space(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static bool is_letter(char value) {
    return (value >= 'a' && value <= 'z') ||
           (value >= 'A' && value <= 'Z');
}

static Token make_token(TokenKind kind, uint16_t offset) {
    Token token = {0};
    token.kind = kind;
    token.offset = offset;
    return token;
}

void lexer_init(Lexer *lexer, const char *source) {
    lexer->source = source;
    lexer->position = 0;
}

Token lexer_next(Lexer *lexer) {
    const char *source = lexer->source;
    uint16_t start;
    char value;

    while (is_space(source[lexer->position])) {
        lexer->position++;
    }

    start = lexer->position;
    value = source[lexer->position];
    if (value == '\0') {
        return make_token(TOKEN_END, start);
    }

    if ((value >= '0' && value <= '9') || value == '.') {
        char *end;
        Token token = make_token(TOKEN_NUMBER, start);
        token.number = strtof(&source[start], &end);
        if (end == &source[start]) {
            lexer->position++;
            token.kind = TOKEN_INVALID;
            return token;
        }
        lexer->position = (uint16_t)(end - source);
        return token;
    }

    if (is_letter(value)) {
        Token token = make_token(TOKEN_IDENTIFIER, start);
        size_t length;
        while (is_letter(source[lexer->position])) {
            lexer->position++;
        }
        length = lexer->position - start;
        if (!ast_name_from_text(&source[start], length, &token.name)) {
            token.kind = TOKEN_INVALID;
        }
        return token;
    }

    lexer->position++;
    switch (value) {
        case '(':
            return make_token(TOKEN_LEFT_PAREN, start);
        case ')':
            return make_token(TOKEN_RIGHT_PAREN, start);
        case ',':
            return make_token(TOKEN_COMMA, start);
        case '+':
            return make_token(TOKEN_PLUS, start);
        case '-':
            return make_token(TOKEN_MINUS, start);
        case '*':
            return make_token(TOKEN_STAR, start);
        case '/':
            return make_token(TOKEN_SLASH, start);
        case '^':
            return make_token(TOKEN_CARET, start);
        case '=':
            return make_token(TOKEN_EQUAL, start);
        case '<':
            if (source[lexer->position] == '=') {
                lexer->position++;
                return make_token(TOKEN_LESS_EQUAL, start);
            }
            return make_token(TOKEN_LESS, start);
        case '>':
            if (source[lexer->position] == '=') {
                lexer->position++;
                return make_token(TOKEN_GREATER_EQUAL, start);
            }
            return make_token(TOKEN_GREATER, start);
        default:
            return make_token(TOKEN_INVALID, start);
    }
}
