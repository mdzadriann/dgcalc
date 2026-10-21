#include "storage/atomic_store.h"
#include "test.h"
#include "project_memory.h"

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
static uint8_t old_bytes[PROJECT_MAX_BYTES];
static size_t old_length;
static uint8_t new_bytes[PROJECT_MAX_BYTES];
static size_t new_length;

static void write_record_u32(uint8_t *bytes, uint32_t value) {
    unsigned index;
    for (index = 0; index < 4; index++) bytes[index] = (uint8_t)(value >> (8 * index));
}

static void repair_record(uint8_t *bytes, size_t length) {
    write_record_u32(bytes + 5, (uint32_t)(length - 13));
    write_record_u32(bytes + 9, project_crc32(bytes + 13, length - 13));
}

static size_t make_atomic_v1(uint8_t *bytes) {
    /* Independently pin v1 metadata at 13 and lines at 17, with an inactive draft. */
    static const uint8_t fixture[] = {
        'D', 'G', 'C', 'E', 1, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 2, 1, 2,
        3, 0, 'a', '=', '9', 3, 0, 'q', '=', '8',
        5, 0, 'b', '=', 'a', '+', '1', 5, 0, 'b', '=', 'a', '+', '1'
    };
    memcpy(bytes, fixture, sizeof(fixture));
    repair_record(bytes, sizeof(fixture));
    return sizeof(fixture);
}

static size_t make_atomic_version(uint8_t *bytes, unsigned version) {
    static const uint8_t fixture[] = {
        'D', 'G', 'C', 'E', 3, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 2, 1, 2,
        0, 0, 0x60, 0x40, 0, 0, 0xC8, 0xC0, 0, 0, 0x80, 0x3E,
        0x01, 0x80,
        3, 0, 'a', '=', '9', 3, 0, 'q', '=', '8',
        5, 0, 'b', '=', 'a', '+', '1', 5, 0, 'b', '=', 'a', '+', '1'
    };
    size_t length = sizeof(fixture);
    size_t start = version == 1 ? 17 : version == 2 ? 29 : 31;
    memcpy(bytes, fixture, length);
    memmove(bytes + start, bytes + 31, length - 31);
    length -= 31 - start;
    bytes[4] = (uint8_t)version;
    repair_record(bytes, length);
    return length;
}

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
    project_dispose(&old_project);
    set_and_commit(&old_project, 0, "a=2");
    project_restore_visibility_mask(&old_project, 0x8001);
    old_length = project_encode(old_state,
                                &old_project,
                                old_bytes,
                                sizeof(old_bytes),
                                &error);
    ASSERT_TRUE(old_length > 0);

    app_state_init(new_state);
    new_state->view = APP_VIEW_GRAPH;
    new_state->mode = GRAPH_MODE_3D;
    project_dispose(&new_project);
    set_and_commit(&new_project, 0, "a=9");
    set_and_commit(&new_project, 1, "b=a+1");
    project_restore_visibility_mask(&new_project, 0x5AA5);
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
    unsigned failure, version;

    prepare_projects(&old_state, &new_state);
    for (version = 1; version <= 3; version++) {
        old_length = make_atomic_version(old_bytes, version);
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
    project_dispose(&loaded_project);
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
    project_dispose(&loaded_project);
    set_and_commit(&loaded_project, 0, "q=7");
    loaded_project.dirty = false;
    state_snapshot = state;

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
    ASSERT_EQ_INT(state_snapshot.view, state.view);
    ASSERT_EQ_INT(state_snapshot.mode, state.mode);
    ASSERT_EQ_INT(state_snapshot.selected_expression, state.selected_expression);
    ASSERT_EQ_INT(1, loaded_project.count);
    ASSERT_FALSE(loaded_project.dirty);
    ASSERT_STR_EQ("q=7", project_line_source(&loaded_project, 0));
    ASSERT_STR_EQ("q=7", project_line_display_text(&loaded_project, 0));
    ASSERT_EQ_INT(EXPRESSION_VALID, project_line_status(&loaded_project, 0));
    ASSERT_EQ_INT(EVAL_OK, project_evaluate_line(&loaded_project, 0).error);
    ASSERT_FLOAT_EQ(7.0f, project_evaluate_line(&loaded_project, 0).value);
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

    project_dispose(&loaded_project);
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

    project_dispose(&loaded_project);
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

    project_dispose(&loaded_project);
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

    project_dispose(&loaded_project);
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

static void test_load_allocation_failure_keeps_destination_and_saved_bytes(void) {
    AppState old_state;
    AppState new_state;
    AppState state;
    Project destination;
    StoreResult result;
    unsigned fail, version;

    prepare_projects(&old_state, &new_state);
    for (version = 1; version <= 3; version++) {
        new_length = make_atomic_version(new_bytes, version);
        for (fail = 1; fail <= 2; fail++) {
            size_t bytes;
            FakeRecord *record;
            AppState before;
            DependencyGraph dependencies;
            EditorBuffer editor;
            fake_init(&fake);
            install_record(&fake, "DGCPROJ", new_bytes, new_length, true);
            project_init_with_memory(&destination, counting_memory());
            set_and_commit(&destination, 0, "q=7");
            set_and_commit(&destination, 1, "r=q+1");
            ASSERT_TRUE(project_set_draft(&destination, 0, "q="));
            ASSERT_FALSE(project_commit_line(&destination, 0).ok);
            ASSERT_TRUE(editor_move(project_editor(&destination), -1));
            project_restore_visibility_mask(&destination, 0x8124);
            destination.dirty = false;
            bytes = memory_bytes;
            app_state_init(&state);
            state.selected_expression = 6;
            state.viewport = (Viewport2D){-5, 6, 2};
            before = state;
            dependencies = destination.dependencies;
            editor = *project_editor(&destination);
            memory_fail_on = memory_calls + fail;
            result = project_store_load(&fake.backend, "DGCPROJ", &state, &destination);
            ASSERT_FALSE(result.ok);
            ASSERT_EQ_INT(STORE_ERR_MEMORY, result.error);
            ASSERT_EQ_INT(APP_VIEW_EXPRESSIONS, state.view);
            ASSERT_EQ_INT(GRAPH_MODE_2D, state.mode);
            ASSERT_EQ_INT(6, state.selected_expression);
            ASSERT_TRUE(memcmp(&before, &state, sizeof(state)) == 0);
            ASSERT_STR_EQ("q=7", project_line_source(&destination, 0));
            ASSERT_STR_EQ("q=", project_line_display_text(&destination, 0));
            ASSERT_EQ_INT(EXPRESSION_INVALID, project_line_status(&destination, 0));
            ASSERT_EQ_INT(0, project_editing_index(&destination));
            ASSERT_EQ_INT(2, destination.count);
            ASSERT_TRUE(memcmp(&editor, project_editor(&destination), sizeof(editor)) == 0);
            ASSERT_TRUE(memcmp(&dependencies, &destination.dependencies, sizeof(dependencies)) == 0);
            ASSERT_FLOAT_EQ(7.0f, project_evaluate_line(&destination, 0).value);
            ASSERT_FLOAT_EQ(8.0f, project_evaluate_line(&destination, 1).value);
            ASSERT_FALSE(destination.dirty);
            ASSERT_EQ_INT(0x8124, project_visibility_mask(&destination));
            ASSERT_EQ_INT(2, memory_live);
            ASSERT_EQ_INT(bytes, memory_bytes);
            record = fake_find(&fake, "DGCPROJ");
            ASSERT_TRUE(record != NULL && record->archived);
            ASSERT_EQ_INT(new_length, record->length);
            ASSERT_TRUE(memcmp(new_bytes, record->bytes, new_length) == 0);
            ASSERT_FALSE(fake_exists(&fake, "DGCTMP"));
            ASSERT_FALSE(fake_exists(&fake, "DGCBAK"));
            project_dispose(&destination);
            ASSERT_EQ_INT(0, memory_live);
            ASSERT_EQ_INT(0, memory_bytes);
        }
    }
}

static void test_legacy_load_ignores_inactive_drafts_and_moves_the_allocator(void) {
    AppState old_state;
    AppState new_state;
    AppState state;
    Project destination;
    FakeRecord *record;

    prepare_projects(&old_state, &new_state);
    new_length = make_atomic_v1(new_bytes);
    fake_init(&fake);
    install_record(&fake, "DGCPROJ", new_bytes, new_length, true);
    project_init_with_memory(&destination, counting_memory());
    set_and_commit(&destination, 0, "q=7");
    ASSERT_TRUE(project_store_load(&fake.backend, "DGCPROJ", &state, &destination).ok);
    ASSERT_STR_EQ("a=9", project_line_source(&destination, 0));
    ASSERT_STR_EQ("a=9", project_line_display_text(&destination, 0));
    ASSERT_EQ_INT(EXPRESSION_VALID, project_line_status(&destination, 0));
    ASSERT_STR_EQ("b=a+1", project_line_source(&destination, 1));
    ASSERT_FLOAT_EQ(10.0f, project_evaluate_line(&destination, 1).value);
    ASSERT_EQ_INT(2, memory_live);
    ASSERT_EQ_INT(memory_bytes, project_payload_bytes(&destination));
    ASSERT_FALSE(project_is_editing(&destination));
    ASSERT_FALSE(destination.dirty);
    ASSERT_EQ_INT(1, state.selected_expression);
    ASSERT_EQ_INT(GRAPH_MODE_3D, state.mode);
    ASSERT_EQ_INT(APP_VIEW_GRAPH, state.view);
    ASSERT_FLOAT_EQ(0, state.viewport.center_x);
    ASSERT_FLOAT_EQ(0, state.viewport.center_y);
    ASSERT_FLOAT_EQ(0.0625f, state.viewport.units_per_pixel);
    record = fake_find(&fake, "DGCPROJ");
    ASSERT_TRUE(memcmp(new_bytes, record->bytes, new_length) == 0);
    project_dispose(&destination);
    ASSERT_EQ_INT(0, memory_live);
    ASSERT_EQ_INT(0, memory_bytes);
}

static void test_explicit_save_persists_a_clean_viewport_change(void) {
    AppState old_state;
    AppState state;
    AppState loaded;
    FakeRecord *record;
    prepare_projects(&old_state, &state);
    new_project.dirty = false;
    state.viewport = (Viewport2D){3.5f, -6.25f, 0.25f};
    ASSERT_TRUE(app_handle_action(&state, APP_ACTION_PAN_RIGHT));
    ASSERT_FALSE(new_project.dirty);
    fake_init(&fake);
    install_old_final(&fake);
    ASSERT_TRUE(project_store_atomic(&fake.backend, "DGCPROJ", &state, &new_project).ok);
    ASSERT_FALSE(new_project.dirty);
    record = fake_find(&fake, "DGCPROJ");
    ASSERT_TRUE(record != NULL);
    ASSERT_EQ_INT(3, record->bytes[4]);
    ASSERT_TRUE(project_store_load(&fake.backend, "DGCPROJ", &loaded, &loaded_project).ok);
    ASSERT_FLOAT_EQ(11.5f, loaded.viewport.center_x);
    ASSERT_FLOAT_EQ(-6.25f, loaded.viewport.center_y);
    ASSERT_FLOAT_EQ(0.25f, loaded.viewport.units_per_pixel);
    ASSERT_FALSE(loaded_project.dirty);
    ASSERT_EQ_INT(0x5AA5, project_visibility_mask(&loaded_project));
}

static void test_invalid_viewports_and_legacy_records_preserve_load_destinations(void) {
    static const struct { size_t offset; uint32_t bits; } invalid[] = {
        {17, 0x7FC00000}, {17, 0x7F800000}, {17, 0xFF800000},
        {21, 0x7FC00000}, {21, 0x7F800000}, {21, 0xFF800000},
        {25, 0}, {25, 0x80000000}, {25, 0xBF800000},
        {25, 0x7FC00000}, {25, 0x7F800000}, {25, 0xFF800000},
        {25, 0x38D1B716}, {25, 0x461C4001}
    };
    AppState state;
    size_t index;
    unsigned version;
    for (version = 1; version <= 3; version++) {
        for (index = 0; index < sizeof(invalid) / sizeof(invalid[0]) + 5; index++) {
            AppState before;
            DependencyGraph dependencies;
            EditorBuffer editor;
            FakeRecord *record;
            StoreResult result;
            if (version == 1 && index < sizeof(invalid) / sizeof(invalid[0])) continue;
            new_length = make_atomic_version(new_bytes, version);
            if (index < sizeof(invalid) / sizeof(invalid[0])) {
                write_record_u32(new_bytes + invalid[index].offset, invalid[index].bits);
                repair_record(new_bytes, new_length);
            } else {
                size_t fault = index - sizeof(invalid) / sizeof(invalid[0]);
                if (fault == 0) new_length--;
                if (fault == 1) new_bytes[5]++;
                if (fault == 2) new_bytes[9] ^= 1;
                if (fault == 3) new_bytes[4] = 4;
                if (fault == 4) {
                    new_length = version == 1 ? 16 : version == 2 ? 28 : 30;
                    repair_record(new_bytes, new_length);
                }
            }
            app_state_init(&state);
            state.viewport = (Viewport2D){-5, 6, 2};
            before = state;
            project_dispose(&loaded_project);
            set_and_commit(&loaded_project, 0, "q=7");
            set_and_commit(&loaded_project, 1, "r=q+1");
            ASSERT_TRUE(project_set_draft(&loaded_project, 0, "q="));
            ASSERT_FALSE(project_commit_line(&loaded_project, 0).ok);
            ASSERT_TRUE(editor_move(project_editor(&loaded_project), -1));
            project_restore_visibility_mask(&loaded_project, 0x8124);
            loaded_project.dirty = false;
            dependencies = loaded_project.dependencies;
            editor = *project_editor(&loaded_project);
            fake_init(&fake);
            install_record(&fake, "DGCPROJ", new_bytes, new_length, true);
            result = project_store_load(&fake.backend, "DGCPROJ", &state, &loaded_project);
            ASSERT_FALSE(result.ok);
            ASSERT_EQ_INT(STORE_ERR_VALIDATE, result.error);
            ASSERT_TRUE(memcmp(&before, &state, sizeof(state)) == 0);
            ASSERT_STR_EQ("q=7", project_line_source(&loaded_project, 0));
            ASSERT_STR_EQ("q=", project_line_display_text(&loaded_project, 0));
            ASSERT_EQ_INT(EXPRESSION_INVALID, project_line_status(&loaded_project, 0));
            ASSERT_EQ_INT(2, loaded_project.count);
            ASSERT_FALSE(loaded_project.dirty);
            ASSERT_EQ_INT(0x8124, project_visibility_mask(&loaded_project));
            ASSERT_EQ_INT(0, project_editing_index(&loaded_project));
            ASSERT_TRUE(memcmp(&editor, project_editor(&loaded_project), sizeof(editor)) == 0);
            ASSERT_TRUE(memcmp(&dependencies, &loaded_project.dependencies, sizeof(dependencies)) == 0);
            ASSERT_FLOAT_EQ(8, project_evaluate_line(&loaded_project, 1).value);
            record = fake_find(&fake, "DGCPROJ");
            ASSERT_EQ_INT(new_length, record->length);
            ASSERT_TRUE(memcmp(new_bytes, record->bytes, new_length) == 0);
        }
    }
}

static void test_all_versions_recover_and_resave_as_v3(void) {
    unsigned scenario, version;
    for (version = 1; version <= 3; version++) {
        for (scenario = 0; scenario < 3; scenario++) {
            AppState state;
            FakeRecord *record;
            new_length = make_atomic_version(new_bytes, version);
            fake_init(&fake);
            install_record(&fake, scenario == 0 ? "DGCPROJ" : "DGCBAK",
                           new_bytes, new_length, true);
            if (scenario == 2) {
                static const uint8_t broken[] = {'b', 'a', 'd'};
                install_record(&fake, "DGCPROJ", broken, sizeof(broken), false);
            }
            StoreResult result = project_store_load(&fake.backend, "DGCPROJ", &state, &loaded_project);
            ASSERT_TRUE(result.ok);
            if (!result.ok) continue;
            ASSERT_FLOAT_EQ(10, project_evaluate_line(&loaded_project, 1).value);
            ASSERT_FLOAT_EQ(version == 1 ? 0 : 3.5f, state.viewport.center_x);
            ASSERT_FLOAT_EQ(version == 1 ? 0 : -6.25f, state.viewport.center_y);
            ASSERT_FLOAT_EQ(version == 1 ? 0.0625f : 0.25f, state.viewport.units_per_pixel);
            ASSERT_EQ_INT(version == 3 ? 0x8001 : 0xFFFF, project_visibility_mask(&loaded_project));
            ASSERT_FALSE(fake_exists(&fake, "DGCBAK"));
            record = fake_find(&fake, "DGCPROJ");
            ASSERT_TRUE(memcmp(new_bytes, record->bytes, new_length) == 0);
            ASSERT_TRUE(project_store_atomic(&fake.backend, "DGCPROJ", &state, &loaded_project).ok);
            record = fake_find(&fake, "DGCPROJ");
            ASSERT_EQ_INT(3, record->bytes[4]);
            ASSERT_TRUE(record->archived);
            ASSERT_TRUE(project_store_load(&fake.backend, "DGCPROJ", &state, &loaded_project).ok);
            ASSERT_FLOAT_EQ(10, project_evaluate_line(&loaded_project, 1).value);
            ASSERT_EQ_INT(version == 3 ? 0x8001 : 0xFFFF, project_visibility_mask(&loaded_project));
            ASSERT_FLOAT_EQ(version == 1 ? 0 : 3.5f, state.viewport.center_x);
            ASSERT_FLOAT_EQ(version == 1 ? 0 : -6.25f, state.viewport.center_y);
            ASSERT_FLOAT_EQ(version == 1 ? 0.0625f : 0.25f, state.viewport.units_per_pixel);
        }
    }
}

static void test_atomic_store_handles_the_maximum_v3_document(void) {
    AppState state, loaded;
    char text[EDITOR_CAPACITY];
    FakeRecord *record;
    unsigned index;
    project_dispose(&new_project);
    app_state_init(&state);
    memset(text, ' ', sizeof(text) - 1);
    memcpy(text, "x=0", 3);
    text[sizeof(text) - 1] = '\0';
    for (index = 0; index < 16; index++) set_and_commit(&new_project, (uint8_t)index, text);
    project_restore_visibility_mask(&new_project, 0x8001);
    fake_init(&fake);
    ASSERT_TRUE(project_store_atomic(&fake.backend, "DGCPROJ", &state, &new_project).ok);
    record = fake_find(&fake, "DGCPROJ");
    ASSERT_TRUE(record != NULL);
    if (record == NULL) return;
    ASSERT_EQ_INT(8255, record->length);
    ASSERT_EQ_INT(3, record->bytes[4]);
    ASSERT_TRUE(record->archived);
    ASSERT_TRUE(project_store_load(&fake.backend, "DGCPROJ", &loaded, &loaded_project).ok);
    ASSERT_EQ_INT(0x8001, project_visibility_mask(&loaded_project));
    ASSERT_EQ_INT(16, loaded_project.count);
    ASSERT_STR_EQ(text, project_line_source(&loaded_project, 15));
}

void run_atomic_store_tests(void) {
    project_init(&old_project);
    project_init(&new_project);
    project_init(&loaded_project);
    test_explicit_save_persists_a_clean_viewport_change();
    test_atomic_store_handles_the_maximum_v3_document();
    test_invalid_viewports_and_legacy_records_preserve_load_destinations();
    test_all_versions_recover_and_resave_as_v3();
    test_load_allocation_failure_keeps_destination_and_saved_bytes();
    test_legacy_load_ignores_inactive_drafts_and_moves_the_allocator();
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
    project_dispose(&old_project);
    project_dispose(&new_project);
    project_dispose(&loaded_project);
}
