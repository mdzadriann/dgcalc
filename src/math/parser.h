#ifndef DGCALC_PARSER_H
#define DGCALC_PARSER_H

#include "math/ast.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PARSE_OK,
    PARSE_ERR_TOKEN,
    PARSE_ERR_EXPECTED,
    PARSE_ERR_TRAILING,
    PARSE_ERR_NODE_LIMIT,
    PARSE_ERR_DEPTH_LIMIT
} ParseErrorCode;

typedef struct {
    bool ok;
    AstId root;
    ParseErrorCode code;
    uint16_t offset;
} ParseResult;

ParseResult parser_parse(const char *source, AstArena *arena);

#endif
