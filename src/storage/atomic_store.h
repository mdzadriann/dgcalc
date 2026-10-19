#ifndef DGCALC_ATOMIC_STORE_H
#define DGCALC_ATOMIC_STORE_H

#include "storage/project_codec.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void *user;
    bool (*exists)(void *user, const char *name);
    bool (*write_all)(void *user,
                      const char *name,
                      const uint8_t *data,
                      size_t length);
    bool (*read_all)(void *user,
                     const char *name,
                     uint8_t *data,
                     size_t capacity,
                     size_t *length);
    bool (*delete)(void *user, const char *name);
    bool (*rename)(void *user, const char *old_name, const char *new_name);
    bool (*set_archived)(void *user, const char *name, bool archived);
} StorageBackend;

typedef enum {
    STORE_OK,
    STORE_ERR_ENCODE,
    STORE_ERR_WRITE,
    STORE_ERR_VALIDATE,
    STORE_ERR_RENAME,
    STORE_ERR_ARCHIVE,
    STORE_ERR_READ,
    STORE_ERR_MEMORY
} StoreError;

typedef struct {
    bool ok;
    StoreError error;
} StoreResult;

StoreResult project_store_atomic(StorageBackend *backend,
                                 const char *final_name,
                                 const AppState *state,
                                 const Project *project);
StoreResult project_store_load(StorageBackend *backend,
                               const char *name,
                               AppState *state_destination,
                               Project *project_destination);

#endif
