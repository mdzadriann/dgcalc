#include "math/deps.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    const AstArena *arena;
    uint8_t local_parameter;
    DependencyLookupFn lookup;
    void *user;
    DependencyScan result;
} ScanState;

static bool is_automatic_symbol(uint8_t name, uint8_t local_parameter) {
    return name == local_parameter || name == 'e' || name == 'x' ||
           name == 'y' || name == 'z' || name >= AST_NAME_PI;
}

static void scan_name(ScanState *state, uint8_t name, bool function_call) {
    int8_t line;

    if (is_automatic_symbol(name, state->local_parameter)) {
        return;
    }
    line = state->lookup(state->user, name, function_call);
    if (line < 0) {
        state->result.undefined = true;
    } else {
        state->result.mask |= UINT32_C(1) << (uint8_t)line;
    }
}

static void scan_node(ScanState *state, AstId id) {
    const AstNode *node;

    if (id == AST_NONE || id >= state->arena->count) {
        return;
    }
    node = &state->arena->nodes[id];
    if (node->kind == AST_SYMBOL) {
        scan_name(state, node->name, false);
        return;
    }
    if (node->kind == AST_CALL) {
        scan_name(state, node->name, true);
        scan_node(state, node->left);
        return;
    }
    scan_node(state, node->left);
    scan_node(state, node->right);
}

DependencyScan dependency_scan(const AstArena *arena,
                               AstId root,
                               uint8_t local_parameter,
                               DependencyLookupFn lookup,
                               void *user) {
    ScanState state = {arena, local_parameter, lookup, user, {0, false}};
    scan_node(&state, root);
    return state.result;
}

void dependency_graph_clear(DependencyGraph *graph) {
    memset(graph, 0, sizeof(*graph));
}

void dependency_graph_set(DependencyGraph *graph,
                          uint8_t expression,
                          uint32_t dependencies) {
    uint8_t index;

    if (expression >= DEPENDENCY_MAX_EXPRESSIONS) {
        return;
    }
    graph->dependencies[expression] = dependencies;
    for (index = 0; index < DEPENDENCY_MAX_EXPRESSIONS; index++) {
        if ((dependencies & (UINT32_C(1) << index)) != 0) {
            graph->reverse_dependencies[index] |= UINT32_C(1) << expression;
        }
    }
}

typedef struct {
    DependencyGraph *graph;
    uint8_t count;
    uint8_t color[DEPENDENCY_MAX_EXPRESSIONS];
    uint8_t stack[DEPENDENCY_MAX_EXPRESSIONS];
    uint8_t position[DEPENDENCY_MAX_EXPRESSIONS];
    uint8_t depth;
} CycleState;

static void visit(CycleState *state, uint8_t node) {
    uint32_t edges = state->graph->dependencies[node];
    uint8_t target;

    state->color[node] = 1;
    state->position[node] = state->depth;
    state->stack[state->depth++] = node;

    for (target = 0; target < state->count; target++) {
        uint8_t index;
        if ((edges & (UINT32_C(1) << target)) == 0) {
            continue;
        }
        if (state->color[target] == 0) {
            visit(state, target);
        } else if (state->color[target] == 1) {
            for (index = state->position[target]; index < state->depth; index++) {
                state->graph->cycles |= UINT32_C(1) << state->stack[index];
            }
        }
    }

    state->depth--;
    state->color[node] = 2;
}

uint32_t dependency_graph_find_cycles(DependencyGraph *graph, uint8_t count) {
    CycleState state;
    uint8_t index;

    memset(&state, 0, sizeof(state));
    state.graph = graph;
    state.count = count > DEPENDENCY_MAX_EXPRESSIONS
                      ? DEPENDENCY_MAX_EXPRESSIONS
                      : count;
    graph->cycles = 0;
    for (index = 0; index < state.count; index++) {
        if (state.color[index] == 0) {
            visit(&state, index);
        }
    }
    return graph->cycles;
}
