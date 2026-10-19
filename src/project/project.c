#include "project/project.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static AstArena parse_scratch;

struct ExpressionPayload {
    size_t bytes;
    uint16_t node_count;
    AstNode nodes[];
};

static AstView line_tree(const ExpressionLine *line) {
    AstView view = {line->payload->nodes, line->payload->node_count};
    return view;
}

static char *payload_source(ExpressionPayload *payload) {
    return (char *)(payload->nodes + payload->node_count);
}

static AstId expression_body(const ExpressionLine *line) {
    const AstNode *root = &line->payload->nodes[line->root];

    if (line->role == EXPR_ROLE_DEFINITION ||
        line->role == EXPR_ROLE_FUNCTION_DEFINITION) {
        return root->right;
    }
    return line->root;
}

static uint8_t definition_name(const ExpressionLine *line) {
    const AstNode *root = &line->payload->nodes[line->root];
    return line->payload->nodes[root->left].name;
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
        if (index != edited_index && line->payload != NULL &&
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
        if (line->payload != NULL && line->role == role &&
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
    const AstNode *root = &line->payload->nodes[line->root];
    const AstNode *call = &line->payload->nodes[root->left];
    return line->payload->nodes[call->left].name;
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

        if (line->payload == NULL) {
            computed[index] = EXPRESSION_EMPTY;
            continue;
        }
        if (line->role == EXPR_ROLE_FUNCTION_DEFINITION) {
            local = function_parameter(line);
        }
        scan = dependency_scan(line_tree(line),
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

            if (project->lines[index].payload == NULL ||
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
    ProjectMemory memory = {malloc, free};
    project_init_with_memory(project, memory);
}

void project_init_with_memory(Project *project, ProjectMemory memory) {
    uint8_t index;

    memset(project, 0, sizeof(*project));
    for (index = 0; index < PROJECT_MAX_EXPRESSIONS; index++) {
        project->lines[index].root = AST_NONE;
        project->lines[index].status = EXPRESSION_EMPTY;
    }
    dependency_graph_clear(&project->dependencies);
    project->memory = memory;
    editor_init(&project->editor);
    project->editing_index = PROJECT_MAX_EXPRESSIONS;
}

void project_dispose(Project *project) {
    uint8_t index;
    ProjectMemory memory = project->memory;
    for (index = 0; index < PROJECT_MAX_EXPRESSIONS; index++) {
        if (project->lines[index].payload != NULL) {
            memory.release(project->lines[index].payload);
        }
    }
    project_init_with_memory(project, memory);
}

size_t project_payload_bytes(const Project *project) {
    size_t bytes = 0;
    uint8_t index;
    for (index = 0; index < PROJECT_MAX_EXPRESSIONS; index++) {
        if (project->lines[index].payload != NULL) {
            bytes += project->lines[index].payload->bytes;
        }
    }
    return bytes;
}

void project_replace(Project *destination, Project *staging) {
    if (destination == staging) {
        return;
    }
    project_dispose(destination);
    /* Transfer the whole owner, then empty staging before it can be disposed. */
    *destination = *staging;
    project_init_with_memory(staging, destination->memory);
}

bool project_begin_edit(Project *project, uint8_t index) {
    const ExpressionLine *line;
    if (index >= PROJECT_MAX_EXPRESSIONS || project->editing) {
        return false;
    }
    line = &project->lines[index];
    project->status_before_edit = line->status;
    project->parse_error_before_edit = line->parse_error;
    project->validation_error_before_edit = line->validation_error;
    project->error_offset_before_edit = line->error_offset;
    (void)editor_set_text(&project->editor, project_line_source(project, index));
    project->count_before_edit = project->count;
    project->dirty_before_edit = project->dirty;
    project->editing_index = index;
    project->editing = true;
    if (index >= project->count) {
        project->count = (uint8_t)(index + 1);
    }
    return true;
}

bool project_is_editing(const Project *project) {
    return project->editing && project->editing_index < PROJECT_MAX_EXPRESSIONS;
}

uint8_t project_editing_index(const Project *project) {
    return project_is_editing(project) ? project->editing_index
                                       : PROJECT_MAX_EXPRESSIONS;
}

EditorBuffer *project_editor(Project *project) {
    return project_is_editing(project)
               ? &project->editor : NULL;
}

uint16_t project_editor_cursor(const Project *project) {
    return project_is_editing(project)
               ? project->editor.cursor : 0;
}

void project_cancel_edit(Project *project) {
    ExpressionLine *line;
    if (!project_is_editing(project)) {
        return;
    }
    line = &project->lines[project->editing_index];
    line->status = project->status_before_edit;
    line->parse_error = project->parse_error_before_edit;
    line->validation_error = project->validation_error_before_edit;
    line->error_offset = project->error_offset_before_edit;
    project->count = project->count_before_edit;
    project->dirty = project->dirty_before_edit;
    project->editing = false;
    project->editing_index = PROJECT_MAX_EXPRESSIONS;
    editor_init(&project->editor);
    rebuild_dependencies(project);
}

const char *project_line_source(const Project *project, uint8_t index) {
    return project_line_has_committed(project, index)
               ? payload_source(project->lines[index].payload) : "";
}

const char *project_line_display_text(const Project *project, uint8_t index) {
    return project_is_editing(project) && project->editing_index == index
               ? project->editor.text : project_line_source(project, index);
}

bool project_line_has_committed(const Project *project, uint8_t index) {
    return index < project->count && index < PROJECT_MAX_EXPRESSIONS &&
           project->lines[index].payload != NULL;
}

ExpressionStatus project_line_status(const Project *project, uint8_t index) {
    return index < project->count && index < PROJECT_MAX_EXPRESSIONS
               ? project->lines[index].status : EXPRESSION_EMPTY;
}

ExprRole project_line_role(const Project *project, uint8_t index) {
    return project_line_has_committed(project, index)
               ? project->lines[index].role : EXPR_ROLE_INVALID;
}

bool project_line_tree(const Project *project, uint8_t index,
                      AstView *tree, AstId *root) {
    if (tree != NULL) {
        tree->nodes = NULL;
        tree->count = 0;
    }
    if (root != NULL) {
        *root = AST_NONE;
    }
    if (!project_line_has_committed(project, index) ||
        tree == NULL || root == NULL) {
        return false;
    }
    *tree = line_tree(&project->lines[index]);
    *root = project->lines[index].root;
    return true;
}

bool project_set_draft(Project *project, uint8_t index, const char *source) {
    size_t length = 0;
    if (index >= PROJECT_MAX_EXPRESSIONS || source == NULL) {
        return false;
    }
    while (length < EDITOR_CAPACITY && source[length] != '\0') {
        length++;
    }
    if (length == EDITOR_CAPACITY) {
        return false;
    }
    if (project_editing_index(project) != index) {
        project_cancel_edit(project);
        (void)project_begin_edit(project, index);
    }
    (void)editor_set_text(&project->editor, source);
    project->dirty = true;
    return true;
}

bool project_clear_line(Project *project, uint8_t index) {
    ExpressionLine *line;

    if (index >= project->count) {
        return false;
    }
    line = &project->lines[index];
    if (project_line_display_text(project, index)[0] == '\0' &&
        line->payload == NULL &&
        !line_has_draft_error(line)) {
        return false;
    }

    if (line->payload != NULL) {
        project->memory.release(line->payload);
    }
    memset(line, 0, sizeof(*line));
    if (project_editing_index(project) == index) {
        project->editing = false;
        project->editing_index = PROJECT_MAX_EXPRESSIONS;
        editor_init(&project->editor);
    }
    line->root = AST_NONE;
    line->status = EXPRESSION_EMPTY;
    project->dirty = true;
    rebuild_dependencies(project);
    return true;
}

static CommitResult failed_commit(ParseErrorCode parse_error,
                                  ValidationErrorCode validation_error,
                                  uint16_t offset) {
    CommitResult result = {false, parse_error, validation_error, offset, false};
    return result;
}

static CommitResult validate_draft(Project *project,
                                   uint8_t index,
                                   ParseResult *parsed,
                                   ValidationResult *validated) {
    ExpressionLine *line;

    if (index >= project->count || project_editing_index(project) != index) {
        return failed_commit(PARSE_ERR_EXPECTED, VALIDATION_OK, 0);
    }
    line = &project->lines[index];
    *parsed = parser_parse(project->editor.text, &parse_scratch);
    if (!parsed->ok) {
        line->status = EXPRESSION_INVALID;
        line->parse_error = parsed->code;
        line->validation_error = VALIDATION_OK;
        line->error_offset = parsed->offset;
        return failed_commit(parsed->code, VALIDATION_OK, parsed->offset);
    }

    *validated = expression_validate(ast_arena_view(&parse_scratch), parsed->root);
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
        CommitResult result = {true, PARSE_OK, VALIDATION_OK, 0, false};
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
    ExpressionPayload *replacement;
    ExpressionPayload *previous;
    size_t bytes;
    size_t text_bytes;
    ExpressionStatus previous_status;
    ParseErrorCode previous_parse_error;
    ValidationErrorCode previous_validation_error;
    uint16_t previous_error_offset;
    ParseResult parsed;
    ValidationResult validated;
    CommitResult checked;

    if (index >= project->count || project_editing_index(project) != index) {
        return failed_commit(PARSE_ERR_EXPECTED, VALIDATION_OK, 0);
    }
    if (project->editor.length == 0) {
        CommitResult cleared = {true, PARSE_OK, VALIDATION_OK, 0, false};
        (void)project_clear_line(project, index);
        if (project_editing_index(project) == index) {
            project->editing = false;
            project->editing_index = PROJECT_MAX_EXPRESSIONS;
        }
        return cleared;
    }
    line = &project->lines[index];
    previous_status = line->status;
    previous_parse_error = line->parse_error;
    previous_validation_error = line->validation_error;
    previous_error_offset = line->error_offset;
    checked = validate_draft(project, index, &parsed, &validated);

    if (!checked.ok) {
        return checked;
    }
    text_bytes = (size_t)project->editor.length + 1;
    bytes = offsetof(ExpressionPayload, nodes);
    if (text_bytes > SIZE_MAX - bytes ||
        parse_scratch.count > (SIZE_MAX - bytes - text_bytes) / sizeof(AstNode)) {
        replacement = NULL;
    } else {
        bytes += (size_t)parse_scratch.count * sizeof(AstNode) + text_bytes;
        replacement = project->memory.allocate(bytes);
    }
    if (replacement == NULL) {
        line->status = previous_status;
        line->parse_error = previous_parse_error;
        line->validation_error = previous_validation_error;
        line->error_offset = previous_error_offset;
        checked.ok = false;
        checked.out_of_memory = true;
        return checked;
    }
    replacement->bytes = bytes;
    replacement->node_count = parse_scratch.count;
    memcpy(replacement->nodes, parse_scratch.nodes,
           (size_t)parse_scratch.count * sizeof(AstNode));
    memcpy(payload_source(replacement), project->editor.text, text_bytes);
    previous = line->payload;
    line->payload = replacement;
    line->root = parsed.root;
    line->role = validated.role;
    line->status = EXPRESSION_VALID;
    line->parse_error = PARSE_OK;
    line->validation_error = VALIDATION_OK;
    line->error_offset = 0;
    project->dirty = true;
    project_mark_dependents_dirty(project, index);
    if (previous != NULL) {
        project->memory.release(previous);
    }
    if (project_editing_index(project) == index) {
        project->editing = false;
        project->editing_index = PROJECT_MAX_EXPRESSIONS;
        editor_init(&project->editor);
    }

    {
        CommitResult result = {true, PARSE_OK, VALIDATION_OK, 0, false};
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
    result = expression_eval(line_tree(line),
                             expression_body(line), &context);
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
    if (line->payload == NULL ||
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
    result = expression_eval(line_tree(line),
                             expression_body(line), &context);
    state->visiting &= ~(UINT32_C(1) << index);
    return result;
}

EvalResult project_evaluate_line(Project *project, uint8_t index) {
    ProjectEvalState state;

    memset(&state, 0, sizeof(state));
    state.project = project;
    return evaluate_line(&state, index);
}
