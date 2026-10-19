#include "storage/project_codec.h"
#include "test.h"

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
    state->selected_expression = 1;

    project_init(&source_project);
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
    project_init(&destination_project);
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
}

static void test_project_codec_round_trips_an_empty_project(void) {
    AppState source_state;
    AppState destination_state;
    CodecError error;
    size_t length;

    app_state_init(&source_state);
    project_init(&source_project);
    length = project_encode(&source_state,
                            &source_project,
                            encoded,
                            sizeof(encoded),
                            &error);
    ASSERT_TRUE(length > 0);

    destination_state.view = APP_VIEW_GRAPH;
    project_init(&destination_project);
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
    project_init(&source_project);
    set_and_commit(&source_project, 0, "a=2");
    set_and_commit(&source_project, 1, "b=a+3");
    ASSERT_TRUE(project_clear_line(&source_project, 0));
    length = project_encode(&source_state,
                            &source_project,
                            encoded,
                            sizeof(encoded),
                            &error);
    ASSERT_TRUE(length > 0);

    project_init(&destination_project);
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
    project_init(&destination_project);
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
    project_init(&source_project);
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

static void test_project_codec_restores_duplicate_draft_errors(void) {
    AppState source_state;
    AppState destination_state;
    CodecError error;
    size_t length;

    app_state_init(&source_state);
    project_init(&source_project);
    set_and_commit(&source_project, 0, "g=3");
    set_and_commit(&source_project, 1, "a=2");
    set_and_commit(&source_project, 2, "h=4");
    set_and_commit(&source_project, 3, "q(x)=x+1");
    set_and_commit(&source_project, 4, "f(x)=x^2");
    ASSERT_TRUE(project_set_draft(&source_project, 0, "a=9"));
    ASSERT_FALSE(project_commit_line(&source_project, 0).ok);
    ASSERT_TRUE(project_set_draft(&source_project, 2, "a=8"));
    ASSERT_FALSE(project_commit_line(&source_project, 2).ok);
    ASSERT_TRUE(project_set_draft(&source_project, 3, "f(t)=t+9"));
    ASSERT_FALSE(project_commit_line(&source_project, 3).ok);
    length = project_encode(&source_state,
                            &source_project,
                            encoded,
                            sizeof(encoded),
                            &error);
    ASSERT_TRUE(length > 0);

    project_init(&destination_project);
    ASSERT_TRUE(project_decode(&destination_state,
                               &destination_project,
                               encoded,
                               length,
                               &error));
    ASSERT_STR_EQ("g=3", project_line_source(&destination_project, 0));
    ASSERT_STR_EQ("a=9", project_line_display_text(&destination_project, 0));
    ASSERT_EQ_INT(EXPRESSION_INVALID,
                  project_line_status(&destination_project, 0));
    ASSERT_EQ_INT(VALIDATION_ERR_DEFINITION,
                  project_validate_draft(&destination_project, 0)
                      .validation_error);
    ASSERT_STR_EQ("h=4", project_line_source(&destination_project, 2));
    ASSERT_STR_EQ("a=8", project_line_display_text(&destination_project, 2));
    ASSERT_EQ_INT(EXPRESSION_INVALID,
                  project_line_status(&destination_project, 2));
    ASSERT_EQ_INT(VALIDATION_ERR_DEFINITION,
                  project_validate_draft(&destination_project, 2)
                      .validation_error);
    ASSERT_STR_EQ("q(x)=x+1",
                  project_line_source(&destination_project, 3));
    ASSERT_STR_EQ("f(t)=t+9", project_line_display_text(&destination_project, 3));
    ASSERT_EQ_INT(EXPRESSION_INVALID,
                  project_line_status(&destination_project, 3));
    ASSERT_EQ_INT(VALIDATION_ERR_DEFINITION,
                  project_validate_draft(&destination_project, 3)
                      .validation_error);
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
        project_init(&destination_project);
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

void run_project_codec_tests(void) {
    test_project_codec_round_trips_committed_and_draft_sources();
    test_project_codec_round_trips_an_empty_project();
    test_project_codec_round_trips_a_cleared_definition();
    test_project_codec_rejects_corrupt_inputs_transactionally();
    test_project_codec_rejects_duplicate_saved_definitions();
    test_project_codec_restores_duplicate_draft_errors();
    test_decoder_fuzz_keeps_destinations_transactional();
}
