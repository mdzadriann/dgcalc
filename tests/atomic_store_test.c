#include "storage/atomic_store.h"
#include "test.h"

#include <stdint.h>
#include <string.h>

#define FAKE_RECORDS 3

typedef struct {
    char name[9];
    uint8_t bytes[PROJECT_MAX_BYTES];
    size_t length;
    bool present;
    bool archived;
} FakeRecord;

typedef struct {
    StorageBackend backend;
    FakeRecord records[FAKE_RECORDS];
    unsigned operation;
    unsigned fail_on_operation;
    bool partial_write_success;
    bool close_failure;
    bool fail_temp_promotion;
    bool fail_archive;
    bool fail_temp_delete;
} FakeStorage;

static FakeStorage fake;
static Project old_project;
static Project new_project;
static Project loaded_project;
static Project project_snapshot;
static uint8_t old_bytes[PROJECT_MAX_BYTES];
static size_t old_length;
static uint8_t new_bytes[PROJECT_MAX_BYTES];
static size_t new_length;

static FakeRecord *fake_find(FakeStorage *storage, const char *name) {
    size_t index;
    for (index = 0; index < FAKE_RECORDS; index++) {
        if (storage->records[index].present &&
            strcmp(storage->records[index].name, name) == 0) {
            return &storage->records[index];
        }
    }
    return NULL;
}

static FakeRecord *fake_create(FakeStorage *storage, const char *name) {
    FakeRecord *record = fake_find(storage, name);
    size_t index;

    if (record != NULL) {
        return record;
    }
    for (index = 0; index < FAKE_RECORDS; index++) {
        if (!storage->records[index].present) {
            record = &storage->records[index];
            memset(record, 0, sizeof(*record));
            memcpy(record->name, name, strlen(name) + 1);
            record->present = true;
            return record;
        }
    }
    return NULL;
}

static bool fake_fails(FakeStorage *storage) {
    storage->operation++;
    return storage->fail_on_operation == storage->operation;
}

static bool fake_exists(void *user, const char *name) {
    return fake_find((FakeStorage *)user, name) != NULL;
}

static bool fake_write_all(void *user,
                           const char *name,
                           const uint8_t *data,
                           size_t length) {
    FakeStorage *storage = (FakeStorage *)user;
    FakeRecord *record;

    if (fake_fails(storage)) {
        return false;
    }
    record = fake_create(storage, name);
    if (record == NULL || length > sizeof(record->bytes)) {
        return false;
    }
    if (storage->partial_write_success) {
        length /= 2;
    }
    memcpy(record->bytes, data, length);
    record->length = length;
    record->archived = false;
    return !storage->close_failure;
}

static bool fake_read_all(void *user,
                          const char *name,
                          uint8_t *data,
                          size_t capacity,
                          size_t *length) {
    FakeStorage *storage = (FakeStorage *)user;
    FakeRecord *record;

    if (fake_fails(storage)) {
        return false;
    }
    record = fake_find(storage, name);
    if (record == NULL || record->length > capacity) {
        return false;
    }
    memcpy(data, record->bytes, record->length);
    *length = record->length;
    return true;
}

static bool fake_delete(void *user, const char *name) {
    FakeStorage *storage = (FakeStorage *)user;
    FakeRecord *record;

    if (fake_fails(storage)) {
        return false;
    }
    if (storage->fail_temp_delete && strcmp(name, "DGCTMP") == 0) {
        return false;
    }
    record = fake_find(storage, name);
    if (record == NULL) {
        return false;
    }
    memset(record, 0, sizeof(*record));
    return true;
}

static bool fake_rename(void *user, const char *old_name, const char *new_name) {
    FakeStorage *storage = (FakeStorage *)user;
    FakeRecord *old_record;
    FakeRecord *new_record;

    if (fake_fails(storage)) {
        return false;
    }
    if (storage->fail_temp_promotion && strcmp(old_name, "DGCTMP") == 0) {
        return false;
    }
    old_record = fake_find(storage, old_name);
    if (old_record == NULL || fake_find(storage, new_name) != NULL) {
        return false;
    }
    new_record = fake_create(storage, new_name);
    if (new_record == NULL) {
        return false;
    }
    memcpy(new_record->bytes, old_record->bytes, old_record->length);
    new_record->length = old_record->length;
    new_record->archived = old_record->archived;
    memset(old_record, 0, sizeof(*old_record));
    return true;
}

static bool fake_set_archived(void *user, const char *name, bool archived) {
    FakeStorage *storage = (FakeStorage *)user;
    FakeRecord *record;

    if (fake_fails(storage)) {
        return false;
    }
    if (storage->fail_archive) {
        return false;
    }
    record = fake_find(storage, name);
    if (record == NULL) {
        return false;
    }
    record->archived = archived;
    return true;
}

static void fake_init(FakeStorage *storage) {
    memset(storage, 0, sizeof(*storage));
    storage->backend.user = storage;
    storage->backend.exists = fake_exists;
    storage->backend.write_all = fake_write_all;
    storage->backend.read_all = fake_read_all;
    storage->backend.delete = fake_delete;
    storage->backend.rename = fake_rename;
    storage->backend.set_archived = fake_set_archived;
}

static void set_and_commit(Project *project, uint8_t index, const char *source) {
    ASSERT_TRUE(project_set_draft(project, index, source));
    ASSERT_TRUE(project_commit_line(project, index).ok);
}

static void prepare_projects(AppState *old_state, AppState *new_state) {
    CodecError error;

    app_state_init(old_state);
    project_init(&old_project);
    set_and_commit(&old_project, 0, "a=2");
    old_length = project_encode(old_state,
                                &old_project,
                                old_bytes,
                                sizeof(old_bytes),
                                &error);
    ASSERT_TRUE(old_length > 0);

    app_state_init(new_state);
    new_state->view = APP_VIEW_GRAPH;
    new_state->mode = GRAPH_MODE_3D;
    project_init(&new_project);
    set_and_commit(&new_project, 0, "a=9");
    set_and_commit(&new_project, 1, "b=a+1");
    new_length = project_encode(new_state,
                                &new_project,
                                new_bytes,
                                sizeof(new_bytes),
                                &error);
    ASSERT_TRUE(new_length > 0);
}

static void install_record(FakeStorage *storage,
                           const char *name,
                           const uint8_t *bytes,
                           size_t length,
                           bool archived) {
    FakeRecord *record = fake_create(storage, name);
    memcpy(record->bytes, bytes, length);
    record->length = length;
    record->archived = archived;
}

static void install_old_final(FakeStorage *storage) {
    install_record(storage, "DGCPROJ", old_bytes, old_length, true);
}

static void assert_old_final_is_intact(FakeStorage *storage) {
    FakeRecord *record = fake_find(storage, "DGCPROJ");
    ASSERT_TRUE(record != NULL);
    if (record != NULL) {
        ASSERT_EQ_INT(old_length, record->length);
        ASSERT_TRUE(memcmp(old_bytes, record->bytes, old_length) == 0);
    }
    ASSERT_FALSE(fake_exists(storage, "DGCTMP"));
}

static void test_every_save_failure_restores_the_old_project(void) {
    AppState old_state;
    AppState new_state;
    StoreResult result;
    unsigned successful_operations;
    unsigned failure;

    prepare_projects(&old_state, &new_state);
    fake_init(&fake);
    install_old_final(&fake);
    result = project_store_atomic(&fake.backend,
                                  "DGCPROJ",
                                  &new_state,
                                  &new_project);
    ASSERT_TRUE(result.ok);
    successful_operations = fake.operation;

    for (failure = 1; failure <= successful_operations; failure++) {
        fake_init(&fake);
        install_old_final(&fake);
        fake.fail_on_operation = failure;
        result = project_store_atomic(&fake.backend,
                                      "DGCPROJ",
                                      &new_state,
                                      &new_project);
        ASSERT_FALSE(result.ok);
        assert_old_final_is_intact(&fake);
    }
}

static void test_partial_and_reported_close_failures_preserve_old_project(void) {
    AppState old_state;
    AppState new_state;
    StoreResult result;

    prepare_projects(&old_state, &new_state);
    fake_init(&fake);
    install_old_final(&fake);
    fake.partial_write_success = true;
    result = project_store_atomic(&fake.backend,
                                  "DGCPROJ",
                                  &new_state,
                                  &new_project);
    ASSERT_FALSE(result.ok);
    assert_old_final_is_intact(&fake);

    fake_init(&fake);
    install_old_final(&fake);
    fake.close_failure = true;
    result = project_store_atomic(&fake.backend,
                                  "DGCPROJ",
                                  &new_state,
                                  &new_project);
    ASSERT_FALSE(result.ok);
    assert_old_final_is_intact(&fake);
}

static void test_success_leaves_only_an_archived_decodable_final(void) {
    AppState old_state;
    AppState new_state;
    AppState loaded_state;
    FakeRecord *record;
    CodecError error;
    StoreResult result;

    prepare_projects(&old_state, &new_state);
    fake_init(&fake);
    install_old_final(&fake);
    result = project_store_atomic(&fake.backend,
                                  "DGCPROJ",
                                  &new_state,
                                  &new_project);
    ASSERT_TRUE(result.ok);
    record = fake_find(&fake, "DGCPROJ");
    ASSERT_TRUE(record != NULL);
    ASSERT_TRUE(record != NULL && record->archived);
    ASSERT_FALSE(fake_exists(&fake, "DGCTMP"));
    ASSERT_FALSE(fake_exists(&fake, "DGCBAK"));
    project_init(&loaded_project);
    ASSERT_TRUE(record != NULL &&
                project_decode(&loaded_state,
                               &loaded_project,
                               record->bytes,
                               record->length,
                               &error));
    ASSERT_EQ_INT(GRAPH_MODE_3D, loaded_state.mode);
    ASSERT_FLOAT_EQ(10.0f, project_evaluate_line(&loaded_project, 1).value);
}

static void test_corrupt_load_leaves_destinations_unchanged(void) {
    AppState state;
    AppState state_snapshot;
    FakeRecord *record;
    StoreResult result;

    app_state_init(&state);
    state.view = APP_VIEW_SPLIT;
    project_init(&loaded_project);
    set_and_commit(&loaded_project, 0, "q=7");
    loaded_project.dirty = false;
    state_snapshot = state;
    project_snapshot = loaded_project;

    fake_init(&fake);
    record = fake_create(&fake, "DGCPROJ");
    memcpy(record->bytes, "broken", 6);
    record->length = 6;
    result = project_store_load(&fake.backend,
                                "DGCPROJ",
                                &state,
                                &loaded_project);
    ASSERT_FALSE(result.ok);
    ASSERT_EQ_INT(STORE_ERR_VALIDATE, result.error);
    ASSERT_TRUE(memcmp(&state, &state_snapshot, sizeof(state)) == 0);
    ASSERT_TRUE(memcmp(&loaded_project,
                       &project_snapshot,
                       sizeof(loaded_project)) == 0);
}

static void test_load_recovers_a_valid_backup_after_interruption(void) {
    AppState old_state;
    AppState new_state;
    AppState loaded_state;
    StoreResult result;

    prepare_projects(&old_state, &new_state);
    fake_init(&fake);
    install_old_final(&fake);
    ASSERT_TRUE(fake_rename(&fake, "DGCPROJ", "DGCBAK"));
    fake.operation = 0;

    project_init(&loaded_project);
    result = project_store_load(&fake.backend,
                                "DGCPROJ",
                                &loaded_state,
                                &loaded_project);
    ASSERT_TRUE(result.ok);
    ASSERT_FLOAT_EQ(2.0f, project_evaluate_line(&loaded_project, 0).value);
    ASSERT_TRUE(fake_exists(&fake, "DGCPROJ"));
    ASSERT_FALSE(fake_exists(&fake, "DGCBAK"));
}

static void test_load_replaces_a_corrupt_final_with_a_valid_backup(void) {
    AppState old_state;
    AppState new_state;
    AppState loaded_state;
    FakeRecord *record;
    StoreResult result;

    prepare_projects(&old_state, &new_state);
    fake_init(&fake);
    install_old_final(&fake);
    ASSERT_TRUE(fake_rename(&fake, "DGCPROJ", "DGCBAK"));
    record = fake_create(&fake, "DGCPROJ");
    memcpy(record->bytes, "broken", 6);
    record->length = 6;
    fake.operation = 0;

    project_init(&loaded_project);
    result = project_store_load(&fake.backend,
                                "DGCPROJ",
                                &loaded_state,
                                &loaded_project);
    ASSERT_TRUE(result.ok);
    ASSERT_FLOAT_EQ(2.0f, project_evaluate_line(&loaded_project, 0).value);
    ASSERT_FALSE(fake_exists(&fake, "DGCBAK"));
}

static void test_save_recovers_backup_before_replacing_it(void) {
    AppState old_state;
    AppState new_state;
    StoreResult result;

    prepare_projects(&old_state, &new_state);
    fake_init(&fake);
    install_old_final(&fake);
    ASSERT_TRUE(fake_rename(&fake, "DGCPROJ", "DGCBAK"));
    fake.operation = 0;
    fake.fail_temp_promotion = true;

    result = project_store_atomic(&fake.backend,
                                  "DGCPROJ",
                                  &new_state,
                                  &new_project);
    ASSERT_FALSE(result.ok);
    assert_old_final_is_intact(&fake);
}

static void test_load_discards_temp_when_final_was_never_rotated(void) {
    AppState old_state;
    AppState new_state;
    AppState loaded_state;
    StoreResult result;

    prepare_projects(&old_state, &new_state);
    fake_init(&fake);
    install_old_final(&fake);
    install_record(&fake, "DGCTMP", new_bytes, new_length, false);

    project_init(&loaded_project);
    result = project_store_load(&fake.backend,
                                "DGCPROJ",
                                &loaded_state,
                                &loaded_project);
    ASSERT_TRUE(result.ok);
    ASSERT_FLOAT_EQ(2.0f, project_evaluate_line(&loaded_project, 0).value);
    ASSERT_FALSE(fake_exists(&fake, "DGCTMP"));
}

static void test_load_finishes_promoted_final_recovery(void) {
    AppState old_state;
    AppState new_state;
    AppState loaded_state;
    FakeRecord *record;
    StoreResult result;

    prepare_projects(&old_state, &new_state);
    fake_init(&fake);
    install_record(&fake, "DGCPROJ", new_bytes, new_length, false);
    install_record(&fake, "DGCBAK", old_bytes, old_length, true);

    project_init(&loaded_project);
    result = project_store_load(&fake.backend,
                                "DGCPROJ",
                                &loaded_state,
                                &loaded_project);
    ASSERT_TRUE(result.ok);
    ASSERT_FLOAT_EQ(10.0f, project_evaluate_line(&loaded_project, 1).value);
    record = fake_find(&fake, "DGCPROJ");
    ASSERT_TRUE(record != NULL && record->archived);
    ASSERT_FALSE(fake_exists(&fake, "DGCBAK"));
}

static void test_recovery_failures_keep_the_backup(void) {
    AppState old_state;
    AppState new_state;
    AppState loaded_state;
    StoreResult result;

    prepare_projects(&old_state, &new_state);
    fake_init(&fake);
    install_record(&fake, "DGCPROJ", new_bytes, new_length, false);
    install_record(&fake, "DGCBAK", old_bytes, old_length, true);
    fake.fail_archive = true;
    result = project_store_load(&fake.backend,
                                "DGCPROJ",
                                &loaded_state,
                                &loaded_project);
    ASSERT_FALSE(result.ok);
    ASSERT_EQ_INT(STORE_ERR_ARCHIVE, result.error);
    ASSERT_TRUE(fake_exists(&fake, "DGCBAK"));

    fake_init(&fake);
    install_record(&fake, "DGCPROJ", new_bytes, new_length, false);
    install_record(&fake, "DGCBAK", old_bytes, old_length, true);
    install_record(&fake, "DGCTMP", new_bytes, new_length, false);
    fake.fail_temp_delete = true;
    result = project_store_load(&fake.backend,
                                "DGCPROJ",
                                &loaded_state,
                                &loaded_project);
    ASSERT_FALSE(result.ok);
    ASSERT_EQ_INT(STORE_ERR_RENAME, result.error);
    ASSERT_TRUE(fake_exists(&fake, "DGCBAK"));
}

void run_atomic_store_tests(void) {
    test_every_save_failure_restores_the_old_project();
    test_partial_and_reported_close_failures_preserve_old_project();
    test_success_leaves_only_an_archived_decodable_final();
    test_corrupt_load_leaves_destinations_unchanged();
    test_load_recovers_a_valid_backup_after_interruption();
    test_load_replaces_a_corrupt_final_with_a_valid_backup();
    test_save_recovers_backup_before_replacing_it();
    test_load_discards_temp_when_final_was_never_rotated();
    test_load_finishes_promoted_final_recovery();
    test_recovery_failures_keep_the_backup();
}
