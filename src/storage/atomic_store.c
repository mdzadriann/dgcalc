#include "storage/atomic_store.h"

#include <stddef.h>

#define TEMP_NAME "DGCTMP"
#define BACKUP_NAME "DGCBAK"

static uint8_t work_buffer[PROJECT_MAX_BYTES];

typedef enum {
    RECOVERY_READY,
    RECOVERY_EMPTY,
    RECOVERY_READ_FAILED,
    RECOVERY_INVALID,
    RECOVERY_RENAME_FAILED,
    RECOVERY_ARCHIVE_FAILED,
    RECOVERY_CLEANUP_FAILED
} RecoveryStatus;

static StoreResult store_result(bool ok, StoreError error) {
    StoreResult result = {ok, error};
    return result;
}

static bool backend_ready(const StorageBackend *backend) {
    return backend != NULL && backend->exists != NULL &&
           backend->write_all != NULL && backend->read_all != NULL &&
           backend->delete != NULL && backend->rename != NULL &&
           backend->set_archived != NULL;
}

static bool delete_if_present(StorageBackend *backend, const char *name) {
    return !backend->exists(backend->user, name) ||
           backend->delete(backend->user, name);
}

static void cleanup_temp(StorageBackend *backend) {
    if (backend->exists(backend->user, TEMP_NAME)) {
        (void)backend->delete(backend->user, TEMP_NAME);
    }
}

static RecoveryStatus validate_record(StorageBackend *backend,
                                      const char *name) {
    CodecError codec_error;
    size_t length = 0;

    if (!backend->read_all(backend->user,
                           name,
                           work_buffer,
                           sizeof(work_buffer),
                           &length)) {
        return RECOVERY_READ_FAILED;
    }
    if (length > sizeof(work_buffer) ||
        !project_validate(work_buffer, length, &codec_error)) {
        return RECOVERY_INVALID;
    }
    return RECOVERY_READY;
}

static RecoveryStatus finish_recovery(StorageBackend *backend,
                                      const char *final_name,
                                      bool has_backup) {
    if (!backend->set_archived(backend->user, final_name, true)) {
        return RECOVERY_ARCHIVE_FAILED;
    }
    if (!delete_if_present(backend, TEMP_NAME)) {
        return RECOVERY_CLEANUP_FAILED;
    }
    if (has_backup && !backend->delete(backend->user, BACKUP_NAME)) {
        return RECOVERY_CLEANUP_FAILED;
    }
    return RECOVERY_READY;
}

static RecoveryStatus recover_final(StorageBackend *backend,
                                    const char *final_name) {
    bool has_final = backend->exists(backend->user, final_name);
    bool has_backup = backend->exists(backend->user, BACKUP_NAME);
    RecoveryStatus final_status = RECOVERY_EMPTY;
    RecoveryStatus backup_status;

    if (has_final) {
        final_status = validate_record(backend, final_name);
        if (final_status == RECOVERY_READY) {
            return finish_recovery(backend, final_name, has_backup);
        }
    }
    if (!has_backup) {
        return has_final ? final_status : RECOVERY_EMPTY;
    }

    backup_status = validate_record(backend, BACKUP_NAME);
    if (backup_status != RECOVERY_READY) {
        return backup_status;
    }
    if (has_final && !backend->delete(backend->user, final_name)) {
        return RECOVERY_RENAME_FAILED;
    }
    if (!backend->rename(backend->user, BACKUP_NAME, final_name)) {
        return RECOVERY_RENAME_FAILED;
    }
    return finish_recovery(backend, final_name, false);
}

static StoreError recovery_error(RecoveryStatus status) {
    if (status == RECOVERY_READ_FAILED || status == RECOVERY_EMPTY) {
        return STORE_ERR_READ;
    }
    if (status == RECOVERY_RENAME_FAILED) {
        return STORE_ERR_RENAME;
    }
    if (status == RECOVERY_ARCHIVE_FAILED) {
        return STORE_ERR_ARCHIVE;
    }
    if (status == RECOVERY_CLEANUP_FAILED) {
        return STORE_ERR_RENAME;
    }
    return STORE_ERR_VALIDATE;
}

static void restore_backup(StorageBackend *backend,
                           const char *final_name,
                           bool had_final) {
    if (backend->exists(backend->user, final_name)) {
        (void)backend->delete(backend->user, final_name);
    }
    if (had_final && backend->exists(backend->user, BACKUP_NAME)) {
        (void)backend->rename(backend->user, BACKUP_NAME, final_name);
    }
    cleanup_temp(backend);
}

StoreResult project_store_atomic(StorageBackend *backend,
                                 const char *final_name,
                                 const AppState *state,
                                 const Project *project) {
    CodecError codec_error;
    size_t encoded_length;
    size_t read_length = 0;
    uint32_t encoded_crc;
    bool had_final;
    RecoveryStatus recovery;

    if (!backend_ready(backend) || final_name == NULL) {
        return store_result(false, STORE_ERR_WRITE);
    }

    recovery = recover_final(backend, final_name);
    if (recovery != RECOVERY_READY && recovery != RECOVERY_EMPTY) {
        return store_result(false, recovery_error(recovery));
    }

    encoded_length = project_encode(state,
                                    project,
                                    work_buffer,
                                    sizeof(work_buffer),
                                    &codec_error);
    if (encoded_length == 0) {
        return store_result(false, STORE_ERR_ENCODE);
    }
    encoded_crc = project_crc32(work_buffer, encoded_length);

    if (!delete_if_present(backend, TEMP_NAME) ||
        !backend->write_all(backend->user,
                            TEMP_NAME,
                            work_buffer,
                            encoded_length)) {
        cleanup_temp(backend);
        return store_result(false, STORE_ERR_WRITE);
    }
    if (!backend->read_all(backend->user,
                           TEMP_NAME,
                           work_buffer,
                           sizeof(work_buffer),
                           &read_length)) {
        cleanup_temp(backend);
        return store_result(false, STORE_ERR_READ);
    }
    if (read_length != encoded_length ||
        project_crc32(work_buffer, read_length) != encoded_crc ||
        !project_validate(work_buffer, read_length, &codec_error)) {
        cleanup_temp(backend);
        return store_result(false, STORE_ERR_VALIDATE);
    }

    if (!delete_if_present(backend, BACKUP_NAME)) {
        cleanup_temp(backend);
        return store_result(false, STORE_ERR_RENAME);
    }
    had_final = backend->exists(backend->user, final_name);
    if (had_final &&
        !backend->rename(backend->user, final_name, BACKUP_NAME)) {
        cleanup_temp(backend);
        return store_result(false, STORE_ERR_RENAME);
    }
    if (!backend->rename(backend->user, TEMP_NAME, final_name)) {
        restore_backup(backend, final_name, had_final);
        return store_result(false, STORE_ERR_RENAME);
    }
    if (!backend->set_archived(backend->user, final_name, true)) {
        restore_backup(backend, final_name, had_final);
        return store_result(false, STORE_ERR_ARCHIVE);
    }
    if (had_final && !backend->delete(backend->user, BACKUP_NAME)) {
        restore_backup(backend, final_name, true);
        return store_result(false, STORE_ERR_RENAME);
    }

    return store_result(true, STORE_OK);
}

StoreResult project_store_load(StorageBackend *backend,
                               const char *name,
                               AppState *state_destination,
                               Project *project_destination) {
    CodecError codec_error;
    size_t length = 0;
    RecoveryStatus recovery;

    if (!backend_ready(backend) || name == NULL) {
        return store_result(false, STORE_ERR_READ);
    }
    recovery = recover_final(backend, name);
    if (recovery != RECOVERY_READY) {
        return store_result(false, recovery_error(recovery));
    }
    if (!backend->read_all(backend->user,
                           name,
                           work_buffer,
                           sizeof(work_buffer),
                           &length) ||
        length > sizeof(work_buffer)) {
        return store_result(false, STORE_ERR_READ);
    }
    if (!project_decode(state_destination,
                        project_destination,
                        work_buffer,
                        length,
                        &codec_error)) {
        return store_result(false, codec_error == CODEC_ERR_MEMORY
                                       ? STORE_ERR_MEMORY : STORE_ERR_VALIDATE);
    }
    return store_result(true, STORE_OK);
}
