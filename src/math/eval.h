#ifndef DGCALC_EVAL_H
#define DGCALC_EVAL_H

#include "math/ast.h"

typedef enum {
    EVAL_OK,
    EVAL_ERR_UNDEFINED,
    EVAL_ERR_CYCLE,
    EVAL_ERR_DOMAIN,
    EVAL_ERR_DIV_ZERO,
    EVAL_ERR_NONFINITE
} EvalError;

typedef EvalError (*ResolveSymbolFn)(void *user,
                                     const char *name,
                                     float *value);
typedef EvalError (*ResolveCallFn)(void *user,
                                   const char *name,
                                   float argument,
                                   float *value);

typedef struct {
    ResolveSymbolFn resolve_symbol;
    ResolveCallFn resolve_call;
    void *user;
} EvalContext;

typedef struct {
    float value;
    EvalError error;
} EvalResult;

EvalResult expression_eval(AstView tree,
                           AstId root,
                           const EvalContext *context);

#endif
