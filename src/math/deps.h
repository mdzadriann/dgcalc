#ifndef DGCALC_DEPS_H
#define DGCALC_DEPS_H

#include "math/ast.h"

#include <stdbool.h>
#include <stdint.h>

#define DEPENDENCY_MAX_EXPRESSIONS 32
#define DEPENDENCY_NO_LOCAL UINT8_MAX

typedef int8_t (*DependencyLookupFn)(void *user,
                                     uint8_t name,
                                     bool function_call);

typedef struct {
    uint32_t mask;
    bool undefined;
} DependencyScan;

typedef struct {
    uint32_t dependencies[DEPENDENCY_MAX_EXPRESSIONS];
    uint32_t reverse_dependencies[DEPENDENCY_MAX_EXPRESSIONS];
    uint32_t cycles;
} DependencyGraph;

DependencyScan dependency_scan(AstView tree,
                               AstId root,
                               uint8_t local_parameter,
                               DependencyLookupFn lookup,
                               void *user);
void dependency_graph_clear(DependencyGraph *graph);
void dependency_graph_set(DependencyGraph *graph,
                          uint8_t expression,
                          uint32_t dependencies);
uint32_t dependency_graph_find_cycles(DependencyGraph *graph, uint8_t count);

#endif
