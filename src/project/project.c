#include "project/project.h"

#include <stddef.h>
#include <string.h>

static AstArena parse_scratch;

static AstId expression_body(const ExpressionLine *line) {
    const AstNode *root = &line->arena.nodes[line->root];

    if (line->role == EXPR_ROLE_DEFINITION ||
        line->role == EXPR_ROLE_FUNCTION_DEFINITION) {
        return root->right;
    }
    return line->root;
}

static uint8_t definition_name(const ExpressionLine *line) {
    const AstNode *root = &line->arena.nodes[line->root];
    return line->arena.nodes[root->left].name;
}

static bool has_duplicate_definition(const Project *project,
                                     uint8_t edited_index,
                                     const AstArena *arena,
                                     AstId root,
                                     ExprRole role) {
    uint8_t name;
    uint8_t index;

    if (role != EXPR_ROLE_DEFINITION &&
        role != EXPR_ROLE_FUNCTION_DEFINITION) {
        return false;
    }
    name = arena->nodes[arena->nodes[root].left].name;
    for (index = 0; index < project->count; index++) {
        const ExpressionLine *line = &project->lines[index];
        if (index != edited_index && line->has_committed &&
            line->role == role && definition_name(line) == name) {
            return true;
        }
    }
    return false;
}

static int8_t find_definition(const Project *project,
                              uint8_t name,
                              bool function_call) {
    uint8_t index;
    ExprRole role = function_call ? EXPR_ROLE_FUNCTION_DEFINITION
                                  : EXPR_ROLE_DEFINITION;

    for (index = 0; index < project->count; index++) {
        const ExpressionLine *line = &project->lines[index];
        if (line->has_committed && line->role == role &&
            definition_name(line) == name) {
            return (int8_t)index;
        }
    }
    return -1;
}

static int8_t dependency_lookup(void *user,
                                uint8_t name,
                                bool function_call) {
    return find_definition((const Project *)user, name, function_call);
}

static uint8_t function_parameter(const ExpressionLine *line) {
    const AstNode *root = &line->arena.nodes[line->root];
    const AstNode *call = &line->arena.nodes[root->left];
    return line->arena.nodes[call->left].name;
}

static bool line_has_draft_error(const ExpressionLine *line) {
    return line->parse_error != PARSE_OK ||
           line->validation_error != VALIDATION_OK;
}

static void rebuild_dependencies(Project *project) {
    bool undefined[PROJECT_MAX_EXPRESSIONS] = {false};
    ExpressionStatus computed[PROJECT_MAX_EXPRESSIONS];
    uint8_t index;
    uint8_t pass;

    dependency_graph_clear(&project->dependencies);
    for (index = 0; index < project->count; index++) {
        ExpressionLine *line = &project->lines[index];
        DependencyScan scan;
        uint8_t local = DEPENDENCY_NO_LOCAL;

        if (!line->has_committed) {
            computed[index] = EXPRESSION_EMPTY;
            continue;
        }
        if (line->role == EXPR_ROLE_FUNCTION_DEFINITION) {
            local = function_parameter(line);
        }
        scan = dependency_scan(&line->arena,
                               expression_body(line),
                               local,
                               dependency_lookup,
                               project);
        dependency_graph_set(&project->dependencies, index, scan.mask);
        undefined[index] = scan.undefined;
        computed[index] = scan.undefined ? EXPRESSION_UNDEFINED
                                         : EXPRESSION_VALID;
    }

    dependency_graph_find_cycles(&project->dependencies, project->count);
    for (index = 0; index < project->count; index++) {
        if ((project->dependencies.cycles & (UINT32_C(1) << index)) != 0) {
            computed[index] = EXPRESSION_CYCLE;
        }
    }

    for (pass = 0; pass < project->count; pass++) {
        bool changed = false;
        for (index = 0; index < project->count; index++) {
            uint32_t mask = project->dependencies.dependencies[index];
            uint8_t target;

            if (!project->lines[index].has_committed ||
                computed[index] == EXPRESSION_CYCLE) {
                continue;
            }
            for (target = 0; target < project->count; target++) {
                if ((mask & (UINT32_C(1) << target)) == 0) {
                    continue;
                }
                if (computed[target] == EXPRESSION_CYCLE) {
                    computed[index] = EXPRESSION_CYCLE;
                    changed = true;
                    break;
                }
                if (computed[target] == EXPRESSION_UNDEFINED &&
                    computed[index] != EXPRESSION_UNDEFINED) {
                    computed[index] = EXPRESSION_UNDEFINED;
                    changed = true;
                }
            }
            if (undefined[index] && computed[index] == EXPRESSION_VALID) {
                computed[index] = EXPRESSION_UNDEFINED;
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }

    for (index = 0; index < project->count; index++) {
        if (!line_has_draft_error(&project->lines[index])) {
            project->lines[index].status = computed[index];
        }
    }
}

void project_init(Project *project) {
    uint8_t index;

    memset(project, 0, sizeof(*project));
    for (index = 0; index < PROJECT_MAX_EXPRESSIONS; index++) {
        editor_init(&project->lines[index].draft);
        ast_arena_init(&project->lines[index].arena);
        project->lines[index].root = AST_NONE;
        project->lines[index].status = EXPRESSION_EMPTY;
    }
    dependency_graph_clear(&project->dependencies);
}

bool project_set_draft(Project *project, uint8_t index, const char *source) {
    if (index >= PROJECT_MAX_EXPRESSIONS ||
        !editor_set_text(&project->lines[index].draft, source)) {
        return false;
    }
    if (index >= project->count) {
        project->count = (uint8_t)(index + 1);
    }
    project->dirty = true;
    return true;
}

bool project_clear_line(Project *project, uint8_t index) {
    ExpressionLine *line;

    if (index >= project->count) {
        return false;
    }
    line = &project->lines[index];
    if (line->draft.length == 0 && !line->has_committed &&
        !line_has_draft_error(line)) {
        return false;
    }

    memset(line, 0, sizeof(*line));
    editor_init(&line->draft);
    ast_arena_init(&line->arena);
    line->root = AST_NONE;
    line->status = EXPRESSION_EMPTY;
    project->dirty = true;
    rebuild_dependencies(project);
    return true;
}

static CommitResult failed_commit(ParseErrorCode parse_error,
                                  ValidationErrorCode validation_error,
                                  uint16_t offset) {
    CommitResult result = {false, parse_error, validation_error, offset};
    return result;
}

static CommitResult validate_draft(Project *project,
                                   uint8_t index,
                                   ParseResult *parsed,
                                   ValidationResult *validated) {
    ExpressionLine *line;

    if (index >= project->count) {
        return failed_commit(PARSE_ERR_EXPECTED, VALIDATION_OK, 0);
    }
    line = &project->lines[index];
    *parsed = parser_parse(line->draft.text, &parse_scratch);
    if (!parsed->ok) {
        line->status = EXPRESSION_INVALID;
        line->parse_error = parsed->code;
        line->validation_error = VALIDATION_OK;
        line->error_offset = parsed->offset;
        return failed_commit(parsed->code, VALIDATION_OK, parsed->offset);
    }

    *validated = expression_validate(&parse_scratch, parsed->root);
    if (!validated->ok) {
        line->status = EXPRESSION_INVALID;
        line->parse_error = PARSE_OK;
        line->validation_error = validated->error;
        line->error_offset = 0;
        return failed_commit(PARSE_OK, validated->error, 0);
    }
    if (has_duplicate_definition(project,
                                 index,
                                 &parse_scratch,
                                 parsed->root,
                                 validated->role)) {
        line->status = EXPRESSION_INVALID;
        line->parse_error = PARSE_OK;
        line->validation_error = VALIDATION_ERR_DEFINITION;
        line->error_offset = 0;
        return failed_commit(PARSE_OK, VALIDATION_ERR_DEFINITION, 0);
    }
    line->parse_error = PARSE_OK;
    line->validation_error = VALIDATION_OK;
    line->error_offset = 0;

    {
        CommitResult result = {true, PARSE_OK, VALIDATION_OK, 0};
        return result;
    }
}

CommitResult project_validate_draft(Project *project, uint8_t index) {
    ParseResult parsed;
    ValidationResult validated;
    return validate_draft(project, index, &parsed, &validated);
}

CommitResult project_commit_line(Project *project, uint8_t index) {
    ExpressionLine *line;
    ParseResult parsed;
    ValidationResult validated;
    CommitResult checked;

    if (index < project->count && project->lines[index].draft.length == 0) {
        CommitResult cleared = {true, PARSE_OK, VALIDATION_OK, 0};
        (void)project_clear_line(project, index);
        return cleared;
    }
    checked = validate_draft(project, index, &parsed, &validated);

    if (!checked.ok) {
        return checked;
    }
    line = &project->lines[index];

    memcpy(line->committed_source,
           line->draft.text,
           (size_t)line->draft.length + 1);
    line->arena = parse_scratch;
    line->root = parsed.root;
    line->role = validated.role;
    line->has_committed = true;
    line->status = EXPRESSION_VALID;
    line->parse_error = PARSE_OK;
    line->validation_error = VALIDATION_OK;
    line->error_offset = 0;
    project->dirty = true;
    project_mark_dependents_dirty(project, index);

    {
        CommitResult result = {true, PARSE_OK, VALIDATION_OK, 0};
        return result;
    }
}

void project_mark_dependents_dirty(Project *project, uint8_t index) {
    if (index >= project->count) {
        return;
    }
    /* shortcut: Milestone 1 caches no values; rebuild statuses when caching lands. */
    rebuild_dependencies(project);
}

typedef struct {
    uint8_t name;
    float value;
} LocalBinding;

typedef struct {
    Project *project;
    uint32_t visiting;
    LocalBinding locals[PROJECT_MAX_EXPRESSIONS];
    uint8_t local_count;
} ProjectEvalState;

static EvalResult evaluate_line(ProjectEvalState *state, uint8_t index);

static EvalError resolve_symbol(void *user, const char *name, float *value) {
    ProjectEvalState *state = (ProjectEvalState *)user;
    uint8_t symbol = (uint8_t)name[0];
    uint8_t local;
    int8_t index;
    EvalResult result;

    for (local = state->local_count; local > 0; local--) {
        if (state->locals[local - 1].name == symbol) {
            *value = state->locals[local - 1].value;
            return EVAL_OK;
        }
    }

    index = find_definition(state->project, symbol, false);
    if (index < 0) {
        return EVAL_ERR_UNDEFINED;
    }
    result = evaluate_line(state, (uint8_t)index);
    if (result.error == EVAL_OK) {
        *value = result.value;
    }
    return result.error;
}

static EvalError resolve_call(void *user,
                              const char *name,
                              float argument,
                              float *value) {
    ProjectEvalState *state = (ProjectEvalState *)user;
    int8_t found = find_definition(state->project, (uint8_t)name[0], true);
    uint8_t index;
    ExpressionLine *line;
    EvalContext context = {resolve_symbol, resolve_call, state};
    EvalResult result;

    if (found < 0 || state->local_count >= PROJECT_MAX_EXPRESSIONS) {
        return EVAL_ERR_UNDEFINED;
    }
    index = (uint8_t)found;
    line = &state->project->lines[index];
    if ((state->visiting & (UINT32_C(1) << index)) != 0 ||
        line->status == EXPRESSION_CYCLE) {
        return EVAL_ERR_CYCLE;
    }
    if (line->status == EXPRESSION_UNDEFINED) {
        return EVAL_ERR_UNDEFINED;
    }

    state->visiting |= UINT32_C(1) << index;
    state->locals[state->local_count].name = function_parameter(line);
    state->locals[state->local_count].value = argument;
    state->local_count++;
    result = expression_eval(&line->arena, expression_body(line), &context);
    state->local_count--;
    state->visiting &= ~(UINT32_C(1) << index);
    if (result.error == EVAL_OK) {
        *value = result.value;
    }
    return result.error;
}

static EvalResult evaluate_line(ProjectEvalState *state, uint8_t index) {
    ExpressionLine *line;
    EvalContext context = {resolve_symbol, resolve_call, state};
    EvalResult result;

    if (index >= state->project->count) {
        result.value = 0.0f;
        result.error = EVAL_ERR_UNDEFINED;
        return result;
    }
    line = &state->project->lines[index];
    if (!line->has_committed ||
        line->role == EXPR_ROLE_FUNCTION_DEFINITION ||
        line->status == EXPRESSION_UNDEFINED) {
        result.value = 0.0f;
        result.error = EVAL_ERR_UNDEFINED;
        return result;
    }
    if (line->status == EXPRESSION_CYCLE ||
        (state->visiting & (UINT32_C(1) << index)) != 0) {
        result.value = 0.0f;
        result.error = EVAL_ERR_CYCLE;
        return result;
    }

    state->visiting |= UINT32_C(1) << index;
    result = expression_eval(&line->arena, expression_body(line), &context);
    state->visiting &= ~(UINT32_C(1) << index);
    return result;
}

EvalResult project_evaluate_line(Project *project, uint8_t index) {
    ProjectEvalState state;

    memset(&state, 0, sizeof(state));
    state.project = project;
    return evaluate_line(&state, index);
}
