#ifndef DGCALC_PROJECT_H
#define DGCALC_PROJECT_H

#include "editor/editor.h"
#include "math/deps.h"
#include "math/eval.h"
#include "math/parser.h"
#include "math/validate.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROJECT_MAX_EXPRESSIONS 16

typedef enum {
    EXPRESSION_EMPTY,
    EXPRESSION_VALID,
    EXPRESSION_INVALID,
    EXPRESSION_UNDEFINED,
    EXPRESSION_CYCLE
} ExpressionStatus;

typedef struct {
    void *(*allocate)(size_t);
    void (*release)(void *);
} ProjectMemory;

typedef struct ExpressionPayload ExpressionPayload;

typedef struct {
    ExpressionPayload *payload;
    AstId root;
    ExpressionStatus status;
    ParseErrorCode parse_error;
    ValidationErrorCode validation_error;
    uint16_t error_offset;
    ExprRole role;
} ExpressionLine;

typedef struct {
    ExpressionLine lines[PROJECT_MAX_EXPRESSIONS];
    DependencyGraph dependencies;
    ProjectMemory memory;
    EditorBuffer editor;
    ExpressionStatus status_before_edit;
    ParseErrorCode parse_error_before_edit;
    ValidationErrorCode validation_error_before_edit;
    uint16_t error_offset_before_edit;
    uint8_t count;
    bool dirty;
    uint8_t count_before_edit;
    bool dirty_before_edit;
    bool editing;
    uint8_t editing_index;
} Project;

typedef struct {
    bool ok;
    ParseErrorCode parse_error;
    ValidationErrorCode validation_error;
    uint16_t error_offset;
    bool out_of_memory;
} CommitResult;

/* Initialize fresh storage only; dispose before reinitializing a live project. */
void project_init(Project *project);
/* Both callbacks are required; allocate must provide ordinary malloc alignment. */
void project_init_with_memory(Project *project, ProjectMemory memory);
/* Idempotent on initialized projects; leaves a blank project with its allocator. */
void project_dispose(Project *project);
size_t project_payload_bytes(const Project *project);
/* Disposes destination and moves staging; both must be initialized. */
void project_replace(Project *destination, Project *staging);
bool project_begin_edit(Project *project, uint8_t index);
void project_cancel_edit(Project *project);
bool project_is_editing(const Project *project);
uint8_t project_editing_index(const Project *project);
EditorBuffer *project_editor(Project *project);
uint16_t project_editor_cursor(const Project *project);
const char *project_line_source(const Project *project, uint8_t index);
const char *project_line_display_text(const Project *project, uint8_t index);
bool project_line_has_committed(const Project *project, uint8_t index);
ExpressionStatus project_line_status(const Project *project, uint8_t index);
ExprRole project_line_role(const Project *project, uint8_t index);
bool project_line_tree(const Project *project, uint8_t index,
                      AstView *tree, AstId *root);
/* Switching slots cancels the previous draft; only one edit can be retained. */
bool project_set_draft(Project *project, uint8_t index, const char *source);
bool project_clear_line(Project *project, uint8_t index);
CommitResult project_validate_draft(Project *project, uint8_t index);
CommitResult project_commit_line(Project *project, uint8_t index);
EvalResult project_evaluate_line(Project *project, uint8_t index);
void project_mark_dependents_dirty(Project *project, uint8_t index);

#endif
