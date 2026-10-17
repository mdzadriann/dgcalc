#include "platform/ce/appvar_backend.h"

#include <fileioc.h>
#include <graphx.h>

#include <stddef.h>
#include <stdint.h>

static void before_gc(void) {
    gfx_End();
}

static void after_gc(void) {
    gfx_Begin();
    gfx_SetDrawBuffer();
}

static bool appvar_exists(void *user, const char *name) {
    uint8_t handle;
    (void)user;

    handle = ti_Open(name, "r");
    if (handle == 0) {
        return false;
    }
    (void)ti_Close(handle);
    return true;
}

static bool appvar_write_all(void *user,
                             const char *name,
                             const uint8_t *data,
                             size_t length) {
    uint8_t handle;
    size_t written;
    int closed;
    (void)user;

    handle = ti_Open(name, "w");
    if (handle == 0) {
        return false;
    }
    written = ti_Write(data, 1, length, handle);
    closed = ti_Close(handle);
    return written == length && closed != 0;
}

static bool appvar_read_all(void *user,
                            const char *name,
                            uint8_t *data,
                            size_t capacity,
                            size_t *length) {
    uint8_t handle;
    size_t size;
    size_t read;
    int closed;
    (void)user;

    handle = ti_Open(name, "r");
    if (handle == 0) {
        return false;
    }
    size = ti_GetSize(handle);
    if (size > capacity) {
        (void)ti_Close(handle);
        return false;
    }
    read = ti_Read(data, 1, size, handle);
    closed = ti_Close(handle);
    if (read != size || closed == 0) {
        return false;
    }
    *length = size;
    return true;
}

static bool appvar_delete(void *user, const char *name) {
    (void)user;
    return ti_Delete(name) != 0;
}

static bool appvar_rename(void *user,
                          const char *old_name,
                          const char *new_name) {
    (void)user;
    return ti_Rename(old_name, new_name) == 0;
}

static bool appvar_set_archived(void *user,
                                const char *name,
                                bool archived) {
    uint8_t handle;
    int moved;
    int closed;
    (void)user;

    handle = ti_Open(name, "r");
    if (handle == 0) {
        return false;
    }
    moved = (ti_IsArchived(handle) != 0) == archived
                ? 1
                : ti_SetArchiveStatus(archived, handle);
    closed = ti_Close(handle);
    return moved != 0 && closed != 0;
}

StorageBackend ce_appvar_backend(void) {
    StorageBackend backend = {
        NULL,
        appvar_exists,
        appvar_write_all,
        appvar_read_all,
        appvar_delete,
        appvar_rename,
        appvar_set_archived
    };

    ti_SetGCBehavior(before_gc, after_gc);
    return backend;
}
