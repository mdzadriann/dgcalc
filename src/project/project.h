#ifndef DGCALC_PROJECT_H
#define DGCALC_PROJECT_H

#include "editor/editor.h"
#include "math/deps.h"
#include "math/eval.h"
#include "math/parser.h"
#include "math/validate.h"

#include <stdbool.h>
#include <stdint.h>

/* shortcut: M1 keeps 16 cached ASTs to fit CE RAM; pool ASTs before raising it. */
#define PROJECT_MAX_EXPRESSIONS 16

typedef enum {
    EXPRESSION_EMPTY,
    EXPRESSION_VALID,
    EXPRESSION_INVALID,
    EXPRESSION_UNDEFINED,
    EXPRESSION_CYCLE
} ExpressionStatus;

typedef struct {
    char committed_source[EDITOR_CAPACITY];
    EditorBuffer draft;
    AstArena arena;
    AstId root;
    ExpressionStatus status;
    ParseErrorCode parse_error;
    ValidationErrorCode validation_error;
    uint16_t error_offset;
    ExprRole role;
    bool has_committed;
} ExpressionLine;

typedef struct {
    ExpressionLine lines[PROJECT_MAX_EXPRESSIONS];
    DependencyGraph dependencies;
    uint8_t count;
    bool dirty;
} Project;

typedef struct {
    bool ok;
    ParseErrorCode parse_error;
    ValidationErrorCode validation_error;
    uint16_t error_offset;
} CommitResult;

void project_init(Project *project);
bool project_set_draft(Project *project, uint8_t index, const char *source);
CommitResult project_validate_draft(Project *project, uint8_t index);
CommitResult project_commit_line(Project *project, uint8_t index);
EvalResult project_evaluate_line(Project *project, uint8_t index);
void project_mark_dependents_dirty(Project *project, uint8_t index);

#endif
