#ifndef DGCALC_TEST_PROJECT_MEMORY_H
#define DGCALC_TEST_PROJECT_MEMORY_H

#include "project/project.h"
#include "test.h"
#include <stdlib.h>

static void *memory_pointers[32];
static size_t memory_sizes[32];
static size_t memory_live;
static size_t memory_bytes;
static unsigned memory_calls;
static unsigned memory_fail_on;

static void *count_allocate(size_t size) {
    size_t index;
    void *pointer;
    memory_calls++;
    if (memory_calls == memory_fail_on) {
        return NULL;
    }
    pointer = malloc(size);
    if (pointer == NULL) {
        return NULL;
    }
    for (index = 0; index < 32; index++) {
        if (memory_pointers[index] == NULL) {
            memory_pointers[index] = pointer;
            memory_sizes[index] = size;
            memory_live++;
            memory_bytes += size;
            return pointer;
        }
    }
    free(pointer);
    ASSERT_TRUE(false);
    return NULL;
}

static void count_release(void *pointer) {
    size_t index;
    for (index = 0; index < 32; index++) {
        if (pointer != NULL && memory_pointers[index] == pointer) {
            memory_live--;
            memory_bytes -= memory_sizes[index];
            memory_pointers[index] = NULL;
            free(pointer);
            return;
        }
    }
    ASSERT_TRUE(false);
}

static ProjectMemory counting_memory(void) {
    ProjectMemory memory = {count_allocate, count_release};
    ASSERT_EQ_INT(0, memory_live);
    ASSERT_EQ_INT(0, memory_bytes);
    memory_calls = 0;
    memory_fail_on = 0;
    return memory;
}

#endif
