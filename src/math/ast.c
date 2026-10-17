#include "math/ast.h"

#include <string.h>

typedef struct {
    const char *text;
    uint8_t name;
} NamedIdentifier;

static const NamedIdentifier named_identifiers[] = {
    {"pi", AST_NAME_PI},
    {"sin", AST_NAME_SIN},
    {"cos", AST_NAME_COS},
    {"tan", AST_NAME_TAN},
    {"sqrt", AST_NAME_SQRT},
    {"abs", AST_NAME_ABS},
    {"exp", AST_NAME_EXP},
    {"ln", AST_NAME_LN},
    {"log", AST_NAME_LOG},
    {"floor", AST_NAME_FLOOR},
    {"ceil", AST_NAME_CEIL}
};

static const char letter_names[] =
    "a\0b\0c\0d\0e\0f\0g\0h\0i\0j\0k\0l\0m\0"
    "n\0o\0p\0q\0r\0s\0t\0u\0v\0w\0x\0y\0z\0";

void ast_arena_init(AstArena *arena) {
    arena->count = 0;
}

AstId ast_arena_append(AstArena *arena, AstNode node) {
    AstId id;

    if (arena->count >= AST_MAX_NODES) {
        return AST_NONE;
    }

    id = arena->count;
    arena->nodes[id] = node;
    arena->count++;
    return id;
}

bool ast_name_from_text(const char *text, size_t length, uint8_t *name) {
    size_t index;

    if (length == 1) {
        char value = text[0];
        if (value >= 'A' && value <= 'Z') {
            value = (char)(value - 'A' + 'a');
        }
        if (value >= 'a' && value <= 'z') {
            *name = (uint8_t)value;
            return true;
        }
    }

    for (index = 0;
         index < sizeof(named_identifiers) / sizeof(named_identifiers[0]);
         index++) {
        const char *candidate = named_identifiers[index].text;
        if (strlen(candidate) == length && memcmp(candidate, text, length) == 0) {
            *name = named_identifiers[index].name;
            return true;
        }
    }

    return false;
}

const char *ast_name_text(uint8_t name) {
    size_t index;

    if (name >= 'a' && name <= 'z') {
        return &letter_names[(name - 'a') * 2];
    }

    for (index = 0;
         index < sizeof(named_identifiers) / sizeof(named_identifiers[0]);
         index++) {
        if (named_identifiers[index].name == name) {
            return named_identifiers[index].text;
        }
    }

    return "";
}
