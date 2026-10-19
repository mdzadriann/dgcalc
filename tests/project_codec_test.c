#include "storage/project_codec.h"
#include "test.h"
#include "project_memory.h"

#include <stdint.h>
#include <string.h>

static uint8_t encoded[PROJECT_MAX_BYTES];
static uint8_t damaged[PROJECT_MAX_BYTES];
static Project source_project;
static Project destination_project;

static void set_and_commit(Project *project, uint8_t index, const char *source) {
    ASSERT_TRUE(project_set_draft(project, index, source));
    ASSERT_TRUE(project_commit_line(project, index).ok);
}

static void assert_state_equal(const AppState *expected, const AppState *actual) {
    ASSERT_EQ_INT(expected->view, actual->view);
    ASSERT_EQ_INT(expected->mode, actual->mode);
    ASSERT_EQ_INT(expected->selected_expression, actual->selected_expression);
}

static size_t make_encoded_project(AppState *state) {
    CodecError error;
    size_t length;

    app_state_init(state);
    state->view = APP_VIEW_SPLIT;
    state->mode = GRAPH_MODE_3D;
    state->selected_expression = 0;

    project_dispose(&source_project);
    set_and_commit(&source_project, 0, "a=2");
    set_and_commit(&source_project, 1, "b=a+3");
    ASSERT_TRUE(project_set_draft(&source_project, 0, "a="));
    ASSERT_FALSE(project_commit_line(&source_project, 0).ok);

    length = project_encode(state,
                            &source_project,
                            encoded,
                            sizeof(encoded),
                            &error);
    ASSERT_TRUE(length > 0);
    ASSERT_EQ_INT(CODEC_OK, error);
    return length;
}

static void test_project_codec_round_trips_committed_and_draft_sources(void) {
    AppState source_state;
    AppState destination_state;
    CodecError error;
    size_t length = make_encoded_project(&source_state);

    app_state_init(&destination_state);
    project_dispose(&destination_project);
    ASSERT_TRUE(project_decode(&destination_state,
                               &destination_project,
                               encoded,
                               length,
                               &error));
    ASSERT_EQ_INT(CODEC_OK, error);
    assert_state_equal(&source_state, &destination_state);
    ASSERT_EQ_INT(2, destination_project.count);
    ASSERT_STR_EQ("a=2", project_line_source(&destination_project, 0));
    ASSERT_STR_EQ("a=", project_line_display_text(&destination_project, 0));
    ASSERT_EQ_INT(EXPRESSION_INVALID,
                  project_line_status(&destination_project, 0));
    ASSERT_EQ_INT(PARSE_ERR_EXPECTED,
                  project_validate_draft(&destination_project, 0).parse_error);
    ASSERT_STR_EQ("b=a+3", project_line_source(&destination_project, 1));
    ASSERT_FALSE(destination_project.dirty);
    ASSERT_FLOAT_EQ(5.0f,
                    project_evaluate_line(&destination_project, 1).value);
    project_cancel_edit(&destination_project);
    ASSERT_FALSE(destination_project.dirty);
    ASSERT_EQ_INT(2, destination_project.count);
    ASSERT_STR_EQ("a=2", project_line_display_text(&destination_project, 0));
    ASSERT_EQ_INT(EXPRESSION_VALID, project_line_status(&destination_project, 0));
}

static void test_project_codec_round_trips_an_empty_project(void) {
    AppState source_state;
    AppState destination_state;
    CodecError error;
    size_t length;

    app_state_init(&source_state);
    project_dispose(&source_project);
    length = project_encode(&source_state,
                            &source_project,
                            encoded,
                            sizeof(encoded),
                            &error);
    ASSERT_TRUE(length > 0);

    destination_state.view = APP_VIEW_GRAPH;
    project_dispose(&destination_project);
    ASSERT_TRUE(project_decode(&destination_state,
                               &destination_project,
                               encoded,
                               length,
                               &error));
    assert_state_equal(&source_state, &destination_state);
    ASSERT_EQ_INT(0, destination_project.count);
    ASSERT_FALSE(destination_project.dirty);
}

static void test_project_codec_round_trips_a_cleared_definition(void) {
    AppState source_state;
    AppState destination_state;
    CodecError error;
    size_t length;

    app_state_init(&source_state);
    project_dispose(&source_project);
    set_and_commit(&source_project, 0, "a=2");
    set_and_commit(&source_project, 1, "b=a+3");
    ASSERT_TRUE(project_clear_line(&source_project, 0));
    length = project_encode(&source_state,
                            &source_project,
                            encoded,
                            sizeof(encoded),
                            &error);
    ASSERT_TRUE(length > 0);

    project_dispose(&destination_project);
    ASSERT_TRUE(project_decode(&destination_state,
                               &destination_project,
                               encoded,
                               length,
                               &error));
    ASSERT_STR_EQ("", project_line_display_text(&destination_project, 0));
    ASSERT_STR_EQ("", project_line_source(&destination_project, 0));
    ASSERT_FALSE(project_line_has_committed(&destination_project, 0));
    ASSERT_EQ_INT(EXPRESSION_EMPTY,
                  project_line_status(&destination_project, 0));
    ASSERT_EQ_INT(EXPRESSION_UNDEFINED,
                  project_line_status(&destination_project, 1));
    ASSERT_EQ_INT(EVAL_ERR_UNDEFINED,
                  project_evaluate_line(&destination_project, 1).error);
    ASSERT_FALSE(destination_project.dirty);
}

static void test_loaded_uncommitted_draft_cancel_preserves_saved_slot_count(void) {
    AppState state;
    CodecError error;
    size_t length;
    app_state_init(&state);
    state.selected_expression = 15;
    project_dispose(&source_project);
    ASSERT_TRUE(project_set_draft(&source_project, 15, "x+"));
    ASSERT_FALSE(project_commit_line(&source_project, 15).ok);
    length = project_encode(&state, &source_project, encoded, sizeof(encoded), &error);
    ASSERT_TRUE(length > 0);
    ASSERT_TRUE(project_decode(&state, &destination_project, encoded, length, &error));
    ASSERT_EQ_INT(16, destination_project.count);
    ASSERT_TRUE(project_is_editing(&destination_project));
    ASSERT_STR_EQ("x+", project_line_display_text(&destination_project, 15));
    project_cancel_edit(&destination_project);
    ASSERT_EQ_INT(16, destination_project.count);
    ASSERT_FALSE(destination_project.dirty);
    ASSERT_STR_EQ("", project_line_display_text(&destination_project, 15));
}

static void assert_rejected_without_mutation(const uint8_t *input,
                                             size_t length,
                                             CodecError expected_error) {
    AppState destination_state;
    AppState state_before;
    CodecError error;

    app_state_init(&destination_state);
    destination_state.view = APP_VIEW_GRAPH;
    destination_state.mode = GRAPH_MODE_3D;
    destination_state.selected_expression = 7;
    project_dispose(&destination_project);
    set_and_commit(&destination_project, 0, "q=9");
    destination_project.dirty = false;
    state_before = destination_state;

    ASSERT_FALSE(project_decode(&destination_state,
                                &destination_project,
                                input,
                                length,
                                &error));
    ASSERT_EQ_INT(expected_error, error);
    assert_state_equal(&state_before, &destination_state);
    ASSERT_EQ_INT(1, destination_project.count);
    ASSERT_FALSE(destination_project.dirty);
    ASSERT_STR_EQ("q=9", project_line_source(&destination_project, 0));
    ASSERT_STR_EQ("q=9", project_line_display_text(&destination_project, 0));
    ASSERT_EQ_INT(EXPRESSION_VALID,
                  project_line_status(&destination_project, 0));
    ASSERT_EQ_INT(EVAL_OK, project_evaluate_line(&destination_project, 0).error);
    ASSERT_FLOAT_EQ(9.0f, project_evaluate_line(&destination_project, 0).value);
}

static void test_project_codec_rejects_corrupt_inputs_transactionally(void) {
    AppState source_state;
    size_t length = make_encoded_project(&source_state);

    assert_rejected_without_mutation(encoded, length - 1, CODEC_ERR_LENGTH);

    memcpy(damaged, encoded, length);
    damaged[9] ^= 0x01;
    assert_rejected_without_mutation(damaged, length, CODEC_ERR_CRC);

    memcpy(damaged, encoded, length);
    damaged[4] = PROJECT_FORMAT_VERSION + 1;
    assert_rejected_without_mutation(damaged, length, CODEC_ERR_VERSION);

    memcpy(damaged, encoded, length);
    damaged[5] = 0xFF;
    damaged[6] = 0xFF;
    damaged[7] = 0xFF;
    damaged[8] = 0x7F;
    assert_rejected_without_mutation(damaged, length, CODEC_ERR_LENGTH);
}

static void test_project_codec_rejects_duplicate_saved_definitions(void) {
    AppState source_state;
    CodecError error;
    size_t length;

    app_state_init(&source_state);
    project_dispose(&source_project);
    set_and_commit(&source_project, 0, "a=2");
    set_and_commit(&source_project, 1, "b=2");
    length = project_encode(&source_state,
                            &source_project,
                            encoded,
                            sizeof(encoded),
                            &error);
    ASSERT_TRUE(length > 0);
    /* Corrupt the second committed definition, then repair the payload CRC. */
    encoded[29] = 'a';
    {
        uint32_t crc = project_crc32(&encoded[13], length - 13);
        encoded[9] = (uint8_t)crc;
        encoded[10] = (uint8_t)(crc >> 8);
        encoded[11] = (uint8_t)(crc >> 16);
        encoded[12] = (uint8_t)(crc >> 24);
    }
    assert_rejected_without_mutation(encoded, length, CODEC_ERR_CONTENT);
}

static void test_project_codec_restores_only_the_active_duplicate_draft(void) {
    AppState source_state;
    AppState destination_state;
    CodecError error;
    size_t length;
    size_t offset;
    uint8_t index;
    static const uint8_t selected[] = {0, 2, 3};
    static const char *const drafts[] = {"a=9", "a=8", "f(t)=t+9"};
    unsigned active;

    app_state_init(&source_state);
    project_dispose(&source_project);
    set_and_commit(&source_project, 0, "g=3");
    set_and_commit(&source_project, 1, "a=2");
    set_and_commit(&source_project, 2, "h=4");
    set_and_commit(&source_project, 3, "q(x)=x+1");
    set_and_commit(&source_project, 4, "f(x)=x^2");
    length = project_encode(&source_state,
                            &source_project,
                            encoded,
                            sizeof(encoded),
                            &error);
    ASSERT_TRUE(length > 0);

    /* Legacy v1 retained differing drafts on multiple inactive lines. */
    offset = 17;
    for (index = 0; index < source_project.count; index++) {
        uint16_t committed = (uint16_t)(encoded[offset] | encoded[offset + 1] << 8);
        uint16_t draft;
        offset += 2 + committed;
        draft = (uint16_t)(encoded[offset] | encoded[offset + 1] << 8);
        offset += 2;
        for (active = 0; active < 3; active++) {
            if (index == selected[active]) {
                ASSERT_EQ_INT(strlen(drafts[active]), draft);
                memcpy(&encoded[offset], drafts[active], draft);
            }
        }
        offset += draft;
    }
    for (active = 0; active < 3; active++) {
        uint32_t crc;
        unsigned line;
        encoded[15] = selected[active];
        crc = project_crc32(&encoded[13], length - 13);
        for (line = 0; line < 4; line++) {
            encoded[9 + line] = (uint8_t)(crc >> (8 * line));
        }
        project_dispose(&destination_project);
        ASSERT_TRUE(project_decode(&destination_state, &destination_project,
                                   encoded, length, &error));
        ASSERT_EQ_INT(5, destination_project.count);
        ASSERT_STR_EQ("g=3", project_line_source(&destination_project, 0));
        ASSERT_STR_EQ("h=4", project_line_source(&destination_project, 2));
        ASSERT_STR_EQ("q(x)=x+1", project_line_source(&destination_project, 3));
        ASSERT_EQ_INT(selected[active], project_editing_index(&destination_project));
        for (line = 0; line < 3; line++) {
            uint8_t slot = selected[line];
            ASSERT_STR_EQ(line == active ? drafts[line] :
                          project_line_source(&destination_project, slot),
                          project_line_display_text(&destination_project, slot));
            ASSERT_EQ_INT(line == active ? EXPRESSION_INVALID : EXPRESSION_VALID,
                          project_line_status(&destination_project, slot));
        }
        ASSERT_EQ_INT(VALIDATION_ERR_DEFINITION,
                      project_validate_draft(&destination_project, selected[active])
                          .validation_error);
        ASSERT_FLOAT_EQ(3.0f, project_evaluate_line(&destination_project, 0).value);
        ASSERT_FLOAT_EQ(4.0f, project_evaluate_line(&destination_project, 2).value);
    }
}

static void test_decoder_fuzz_keeps_destinations_transactional(void) {
    AppState destination_state;
    AppState state_before;
    CodecError error;
    uint8_t bytes[256];
    uint32_t random = UINT32_C(0xC0DEC0DE);
    size_t iteration;
    size_t index;

    app_state_init(&state_before);
    state_before.view = APP_VIEW_SPLIT;
    state_before.selected_expression = 3;

    for (iteration = 0; iteration < 1000; iteration++) {
        size_t length = iteration % sizeof(bytes) + 1;
        for (index = 0; index < length; index++) {
            random = random * UINT32_C(1664525) + UINT32_C(1013904223);
            bytes[index] = (uint8_t)(random >> 24);
        }
        destination_state = state_before;
        project_dispose(&destination_project);
        set_and_commit(&destination_project, 0, "a=4");
        destination_project.dirty = false;
        if (!project_decode(&destination_state,
                            &destination_project,
                            bytes,
                            length,
                            &error)) {
            assert_state_equal(&state_before, &destination_state);
            ASSERT_EQ_INT(1, destination_project.count);
            ASSERT_FALSE(destination_project.dirty);
            ASSERT_STR_EQ("a=4", project_line_source(&destination_project, 0));
            ASSERT_STR_EQ("a=4",
                          project_line_display_text(&destination_project, 0));
            ASSERT_EQ_INT(EXPRESSION_VALID,
                          project_line_status(&destination_project, 0));
            ASSERT_EQ_INT(EVAL_OK,
                          project_evaluate_line(&destination_project, 0).error);
            ASSERT_FLOAT_EQ(4.0f,
                            project_evaluate_line(&destination_project, 0).value);
        }
    }
}

static void test_decode_allocation_failures_preserve_destination_and_release_staging(void) {
    AppState source_state;
    AppState destination_state;
    AppState before;
    Project destination;
    CodecError error;
    size_t length = make_encoded_project(&source_state);
    unsigned fail;

    for (fail = 1; fail <= 2; fail++) {
        size_t bytes;
        project_init_with_memory(&destination, counting_memory());
        set_and_commit(&destination, 0, "q=9");
        destination.dirty = false;
        app_state_init(&destination_state);
        destination_state.selected_expression = 7;
        before = destination_state;
        bytes = memory_bytes;
        memory_fail_on = memory_calls + fail;
        ASSERT_FALSE(project_decode(&destination_state, &destination,
                                    encoded, length, &error));
        ASSERT_EQ_INT(CODEC_ERR_MEMORY, error);
        assert_state_equal(&before, &destination_state);
        ASSERT_STR_EQ("q=9", project_line_source(&destination, 0));
        ASSERT_EQ_INT(1, destination.count);
        ASSERT_FALSE(destination.dirty);
        ASSERT_FLOAT_EQ(9.0f, project_evaluate_line(&destination, 0).value);
        ASSERT_EQ_INT(1, memory_live);
        ASSERT_EQ_INT(bytes, memory_bytes);
        project_dispose(&destination);
        ASSERT_EQ_INT(0, memory_live);
        ASSERT_EQ_INT(0, memory_bytes);
    }
}

void run_project_codec_tests(void) {
    project_init(&source_project);
    project_init(&destination_project);
    test_decode_allocation_failures_preserve_destination_and_release_staging();
    test_project_codec_round_trips_committed_and_draft_sources();
    test_project_codec_round_trips_an_empty_project();
    test_project_codec_round_trips_a_cleared_definition();
    test_loaded_uncommitted_draft_cancel_preserves_saved_slot_count();
    test_project_codec_rejects_corrupt_inputs_transactionally();
    test_project_codec_rejects_duplicate_saved_definitions();
    test_project_codec_restores_only_the_active_duplicate_draft();
    test_decoder_fuzz_keeps_destinations_transactional();
    project_dispose(&source_project);
    project_dispose(&destination_project);
}
