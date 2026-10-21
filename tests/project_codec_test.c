#include "storage/project_codec.h"
#include "test.h"
#include "project_memory.h"

#include <stdint.h>
#include <string.h>

static uint8_t encoded[PROJECT_MAX_BYTES];
static uint8_t damaged[PROJECT_MAX_BYTES];
static Project source_project;
static Project destination_project;

static void write_test_u32(uint8_t *output, uint32_t value) {
    unsigned byte;
    for (byte = 0; byte < 4; byte++) output[byte] = (uint8_t)(value >> (8 * byte));
}

/* Independent, bit-at-a-time fixture CRC; never use the encoder to make v1. */
static uint32_t fixture_crc(const uint8_t *input, size_t length) {
    uint32_t crc = UINT32_MAX;
    size_t index;
    for (index = 0; index < length; index++) {
        unsigned bit;
        crc ^= input[index];
        for (bit = 0; bit < 8; bit++) {
            if (crc & 1) crc = (crc >> 1) ^ UINT32_C(0xEDB88320);
            else crc >>= 1;
        }
    }
    return ~crc;
}

static void repair_fixture(uint8_t *bytes, size_t length) {
    write_test_u32(bytes + 5, (uint32_t)(length - 13));
    write_test_u32(bytes + 9, fixture_crc(bytes + 13, length - 13));
}

static size_t make_v1_fixture(uint8_t *output, uint8_t selected, uint8_t count,
                              const char *const committed[],
                              const char *const drafts[]) {
    size_t offset = 17; /* v1 has exactly four metadata bytes after header 13. */
    unsigned index;
    memcpy(output, "DGCE", 4);
    output[4] = 1;
    output[13] = 1;
    output[14] = 1;
    output[15] = selected;
    output[16] = count;
    for (index = 0; index < count; index++) {
        const char *texts[] = {committed[index], drafts[index]};
        unsigned field;
        for (field = 0; field < 2; field++) {
            size_t length = strlen(texts[field]);
            output[offset++] = (uint8_t)length;
            output[offset++] = (uint8_t)(length >> 8);
            memcpy(output + offset, texts[field], length);
            offset += length;
        }
    }
    repair_fixture(output, offset);
    return offset;
}

static size_t make_legacy_project(void) {
    static const char *const committed[] = {"a=2", "b=a+3"};
    static const char *const drafts[] = {"a=", "b=a+9"};
    return make_v1_fixture(encoded, 0, 2, committed, drafts);
}

static void set_and_commit(Project *project, uint8_t index, const char *source) {
    ASSERT_TRUE(project_set_draft(project, index, source));
    ASSERT_TRUE(project_commit_line(project, index).ok);
}

static void assert_state_equal(const AppState *expected, const AppState *actual) {
    ASSERT_EQ_INT(expected->view, actual->view);
    ASSERT_EQ_INT(expected->mode, actual->mode);
    ASSERT_EQ_INT(expected->selected_expression, actual->selected_expression);
    ASSERT_FLOAT_EQ(expected->viewport.center_x, actual->viewport.center_x);
    ASSERT_FLOAT_EQ(expected->viewport.center_y, actual->viewport.center_y);
    ASSERT_FLOAT_EQ(expected->viewport.units_per_pixel, actual->viewport.units_per_pixel);
}

static size_t make_encoded_project(AppState *state) {
    CodecError error;
    size_t length;

    app_state_init(state);
    state->view = APP_VIEW_SPLIT;
    state->mode = GRAPH_MODE_3D;
    state->selected_expression = 0;
    state->viewport = (Viewport2D){1.5f, -2.25f, 0.25f};

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

static void test_v1_fixture_migrates_with_default_viewport(void) {
    AppState state;
    Viewport2D defaults;
    CodecError error;
    size_t length = make_legacy_project();
    ASSERT_EQ_INT(40, length);
    ASSERT_EQ_INT(3, encoded[17]);
    ASSERT_EQ_INT('a', encoded[19]);
    app_state_init(&state);
    state.viewport = (Viewport2D){8, 9, 2};
    viewport2d_init(&defaults);
    ASSERT_TRUE(project_validate(encoded, length, &error));
    ASSERT_TRUE(project_decode(&state, &destination_project, encoded, length, &error));
    ASSERT_EQ_INT(GRAPH_MODE_3D, state.mode);
    ASSERT_EQ_INT(APP_VIEW_SPLIT, state.view);
    ASSERT_EQ_INT(0, state.selected_expression);
    ASSERT_FLOAT_EQ(defaults.center_x, state.viewport.center_x);
    ASSERT_FLOAT_EQ(defaults.center_y, state.viewport.center_y);
    ASSERT_FLOAT_EQ(defaults.units_per_pixel, state.viewport.units_per_pixel);
    ASSERT_STR_EQ("a=2", project_line_source(&destination_project, 0));
    ASSERT_STR_EQ("a=", project_line_display_text(&destination_project, 0));
    ASSERT_STR_EQ("b=a+3", project_line_display_text(&destination_project, 1));
    ASSERT_EQ_INT(0, project_editing_index(&destination_project));
    ASSERT_FLOAT_EQ(5, project_evaluate_line(&destination_project, 1).value);
    ASSERT_FALSE(destination_project.dirty);
}

static void test_v2_exact_bytes_and_clean_viewport_save(void) {
    static const uint8_t payload[] = {
        1, 1, 0, 2,
        0, 0, 0xC0, 0x3F, 0, 0, 0x10, 0xC0, 0, 0, 0x80, 0x3E,
        3, 0, 'a', '=', '2', 2, 0, 'a', '=',
        5, 0, 'b', '=', 'a', '+', '3', 5, 0, 'b', '=', 'a', '+', '3'
    };
    AppState state;
    CodecError error;
    size_t length = make_encoded_project(&state);
    ASSERT_EQ_INT(2, encoded[4]);
    ASSERT_EQ_INT(13 + sizeof(payload), length);
    ASSERT_TRUE(memcmp(encoded + 13, payload, sizeof(payload)) == 0);
    ASSERT_EQ_INT(sizeof(payload), encoded[5]);
    ASSERT_EQ_INT(0, encoded[6] | encoded[7] | encoded[8]);
    write_test_u32(damaged, fixture_crc(payload, sizeof(payload)));
    ASSERT_TRUE(memcmp(encoded + 9, damaged, 4) == 0);
    source_project.dirty = false;
    state.viewport = (Viewport2D){-12.5f, 30, 0.5f};
    length = project_encode(&state, &source_project, encoded, sizeof(encoded), &error);
    ASSERT_TRUE(length > 0);
    ASSERT_FALSE(source_project.dirty);
    {
        AppState loaded;
        ASSERT_TRUE(project_decode(&loaded, &destination_project, encoded, length, &error));
        assert_state_equal(&state, &loaded);
        ASSERT_FALSE(destination_project.dirty);
    }
}

static void test_v2_omits_a_draft_when_editing_an_unselected_line(void) {
    AppState state;
    AppState loaded;
    CodecError error;
    size_t length = make_encoded_project(&state);
    (void)length;
    state.selected_expression = 1;
    length = project_encode(&state, &source_project, encoded, sizeof(encoded), &error);
    ASSERT_TRUE(project_decode(&loaded, &destination_project, encoded, length, &error));
    ASSERT_STR_EQ("a=2", project_line_display_text(&destination_project, 0));
    ASSERT_FALSE(project_is_editing(&destination_project));
    ASSERT_EQ_INT(3, encoded[34]); /* First draft length, absolute v2 offset. */
    ASSERT_TRUE(memcmp(encoded + 36, "a=2", 3) == 0);
}

static void test_invalid_viewport_encode_keeps_output_and_project_intact(void) {
    static const Viewport2D invalid[] = {
        {NAN, 0, 1}, {INFINITY, 0, 1}, {-INFINITY, 0, 1},
        {0, NAN, 1}, {0, INFINITY, 1}, {0, -INFINITY, 1},
        {0, 0, 0}, {0, 0, -1}, {0, 0, NAN}, {0, 0, INFINITY},
        {0, 0, 0.00001f}, {0, 0, 10001}
    };
    AppState state;
    CodecError error;
    size_t index;
    (void)make_encoded_project(&state);
    source_project.dirty = false;
    memset(encoded, 0xA5, sizeof(encoded));
    for (index = 0; index < sizeof(invalid) / sizeof(invalid[0]); index++) {
        state.viewport = invalid[index];
        memcpy(damaged, encoded, sizeof(encoded));
        ASSERT_EQ_INT(0, project_encode(&state, &source_project, damaged, sizeof(damaged), &error));
        ASSERT_EQ_INT(CODEC_ERR_CONTENT, error);
        ASSERT_TRUE(memcmp(encoded, damaged, sizeof(encoded)) == 0);
        ASSERT_FALSE(source_project.dirty);
        ASSERT_STR_EQ("a=2", project_line_source(&source_project, 0));
        ASSERT_STR_EQ("a=", project_line_display_text(&source_project, 0));
    }
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
    DependencyGraph dependencies;
    EditorBuffer editor;
    uint8_t before[PROJECT_MAX_BYTES];
    uint8_t after[PROJECT_MAX_BYTES];
    size_t before_length;
    size_t after_length;
    bool accepted;

    app_state_init(&destination_state);
    destination_state.view = APP_VIEW_GRAPH;
    destination_state.mode = GRAPH_MODE_3D;
    destination_state.selected_expression = 7;
    destination_state.viewport = (Viewport2D){-7, 8, 3};
    project_dispose(&destination_project);
    set_and_commit(&destination_project, 0, "q=9");
    set_and_commit(&destination_project, 1, "r=q+1");
    ASSERT_TRUE(project_set_draft(&destination_project, 0, "q="));
    ASSERT_FALSE(project_commit_line(&destination_project, 0).ok);
    ASSERT_TRUE(editor_move(project_editor(&destination_project), -1));
    destination_project.dirty = false;
    state_before = destination_state;
    dependencies = destination_project.dependencies;
    editor = *project_editor(&destination_project);
    before_length = project_encode(&destination_state, &destination_project,
                                   before, sizeof(before), &error);

    accepted = project_decode(&destination_state, &destination_project, input, length, &error);
    ASSERT_FALSE(accepted);
    if (accepted) return;
    ASSERT_EQ_INT(expected_error, error);
    assert_state_equal(&state_before, &destination_state);
    ASSERT_TRUE(memcmp(&state_before, &destination_state, sizeof(state_before)) == 0);
    ASSERT_EQ_INT(2, destination_project.count);
    ASSERT_FALSE(destination_project.dirty);
    ASSERT_STR_EQ("q=9", project_line_source(&destination_project, 0));
    ASSERT_STR_EQ("q=", project_line_display_text(&destination_project, 0));
    ASSERT_EQ_INT(EXPRESSION_INVALID,
                  project_line_status(&destination_project, 0));
    ASSERT_EQ_INT(0, project_editing_index(&destination_project));
    ASSERT_TRUE(memcmp(&editor, project_editor(&destination_project), sizeof(editor)) == 0);
    ASSERT_TRUE(memcmp(&dependencies, &destination_project.dependencies, sizeof(dependencies)) == 0);
    ASSERT_EQ_INT(EVAL_OK, project_evaluate_line(&destination_project, 0).error);
    ASSERT_FLOAT_EQ(9.0f, project_evaluate_line(&destination_project, 0).value);
    ASSERT_FLOAT_EQ(10.0f, project_evaluate_line(&destination_project, 1).value);
    after_length = project_encode(&destination_state, &destination_project,
                                  after, sizeof(after), &error);
    ASSERT_EQ_INT(before_length, after_length);
    ASSERT_TRUE(memcmp(before, after, before_length) == 0);
}

static void test_v1_truncation_length_crc_and_version_fail_transactionally(void) {
    size_t length = make_legacy_project();
    static const size_t truncated[] = {13, 14, 15, 16, 17, 18, 20, 23, 25, 39};
    static const uint8_t versions[] = {0, 3, 255};
    size_t index;
    for (index = 0; index < sizeof(truncated) / sizeof(truncated[0]); index++) {
        memcpy(damaged, encoded, length);
        repair_fixture(damaged, truncated[index]);
        assert_rejected_without_mutation(damaged, truncated[index], CODEC_ERR_LENGTH);
    }
    memcpy(damaged, encoded, length);
    damaged[5]++;
    assert_rejected_without_mutation(damaged, length, CODEC_ERR_LENGTH);
    memcpy(damaged, encoded, length);
    damaged[19] ^= 1;
    assert_rejected_without_mutation(damaged, length, CODEC_ERR_CRC);
    for (index = 0; index < sizeof(versions); index++) {
        memcpy(damaged, encoded, length);
        damaged[4] = versions[index];
        assert_rejected_without_mutation(damaged, length, CODEC_ERR_VERSION);
    }
}

static void test_v2_viewport_corruption_is_rejected_before_allocation(void) {
    static const struct { size_t offset; uint32_t bits; } invalid[] = {
        {17, 0x7FC00000}, {17, 0x7F800000}, {17, 0xFF800000},
        {21, 0x7FC00000}, {21, 0x7F800000}, {21, 0xFF800000},
        {25, 0}, {25, 0x80000000}, {25, 0xBF800000},
        {25, 0x7FC00000}, {25, 0x7F800000}, {25, 0xFF800000},
        {25, 0x38D1B716}, /* one binary32 step below 0.0001f */
        {25, 0x461C4001}  /* one binary32 step above 10000.0f */
    };
    AppState state;
    size_t length = make_encoded_project(&state);
    size_t index;
    for (index = 0; index < sizeof(invalid) / sizeof(invalid[0]); index++) {
        CodecError error;
        memcpy(damaged, encoded, length);
        damaged[4] = 2;
        write_test_u32(damaged + invalid[index].offset, invalid[index].bits);
        repair_fixture(damaged, length);
        assert_rejected_without_mutation(damaged, length, CODEC_ERR_CONTENT);
        project_dispose(&destination_project);
        project_init_with_memory(&destination_project, counting_memory());
        ASSERT_FALSE(project_decode(&state, &destination_project, damaged, length, &error));
        ASSERT_EQ_INT(CODEC_ERR_CONTENT, error);
        ASSERT_EQ_INT(0, memory_calls);
        ASSERT_EQ_INT(0, memory_live);
        project_dispose(&destination_project);
        project_init(&destination_project);
    }
}

static void test_v2_accepts_finite_centers_and_inclusive_scale_clamps(void) {
    static const uint32_t scales[] = {0x38D1B717, 0x461C4000};
    AppState state;
    CodecError error;
    size_t length = make_encoded_project(&state);
    unsigned index;
    for (index = 0; index < 2; index++) {
        memcpy(damaged, encoded, length);
        damaged[4] = 2;
        write_test_u32(damaged + 17, 0x7F7FFFFF);
        write_test_u32(damaged + 21, 0xFF7FFFFF);
        write_test_u32(damaged + 25, scales[index]);
        repair_fixture(damaged, length);
        ASSERT_TRUE(project_decode(&state, &destination_project, damaged, length, &error));
        ASSERT_TRUE(isfinite(state.viewport.center_x));
        ASSERT_TRUE(state.viewport.center_x > 1e30f);
        ASSERT_TRUE(state.viewport.center_y < -1e30f);
        ASSERT_TRUE(state.viewport.units_per_pixel == (index == 0 ? 0.0001f : 10000.0f));
    }
}

static void test_project_codec_rejects_corrupt_inputs_transactionally(void) {
    AppState source_state;
    size_t length = make_encoded_project(&source_state);

    assert_rejected_without_mutation(encoded, length - 1, CODEC_ERR_LENGTH);

    memcpy(damaged, encoded, length);
    damaged[0] = 'X';
    assert_rejected_without_mutation(damaged, length, CODEC_ERR_MAGIC);

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
    encoded[41] = 'a'; /* v2 line 1 committed text: 29 + 10 + 2. */
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
    {
        static const char *const committed[] = {"g=3", "a=2", "h=4", "q(x)=x+1", "f(x)=x^2"};
        length = make_v1_fixture(encoded, 0, 5, committed, committed);
    }

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

    ASSERT_EQ_INT(2, encoded[4]);

    for (fail = 1; fail <= 2; fail++) {
        size_t bytes;
        DependencyGraph dependencies;
        EditorBuffer editor;
        project_init_with_memory(&destination, counting_memory());
        set_and_commit(&destination, 0, "q=9");
        set_and_commit(&destination, 1, "r=q+1");
        ASSERT_TRUE(project_set_draft(&destination, 0, "q="));
        ASSERT_FALSE(project_commit_line(&destination, 0).ok);
        ASSERT_TRUE(editor_move(project_editor(&destination), -1));
        destination.dirty = false;
        app_state_init(&destination_state);
        destination_state.selected_expression = 7;
        destination_state.viewport = (Viewport2D){7, -8, 2};
        before = destination_state;
        dependencies = destination.dependencies;
        editor = *project_editor(&destination);
        bytes = memory_bytes;
        memory_fail_on = memory_calls + fail;
        ASSERT_FALSE(project_decode(&destination_state, &destination,
                                    encoded, length, &error));
        ASSERT_EQ_INT(CODEC_ERR_MEMORY, error);
        assert_state_equal(&before, &destination_state);
        ASSERT_TRUE(memcmp(&before, &destination_state, sizeof(before)) == 0);
        ASSERT_STR_EQ("q=9", project_line_source(&destination, 0));
        ASSERT_STR_EQ("q=", project_line_display_text(&destination, 0));
        ASSERT_EQ_INT(EXPRESSION_INVALID, project_line_status(&destination, 0));
        ASSERT_EQ_INT(0, project_editing_index(&destination));
        ASSERT_TRUE(memcmp(&editor, project_editor(&destination), sizeof(editor)) == 0);
        ASSERT_TRUE(memcmp(&dependencies, &destination.dependencies, sizeof(dependencies)) == 0);
        ASSERT_EQ_INT(2, destination.count);
        ASSERT_FALSE(destination.dirty);
        ASSERT_FLOAT_EQ(9.0f, project_evaluate_line(&destination, 0).value);
        ASSERT_FLOAT_EQ(10.0f, project_evaluate_line(&destination, 1).value);
        ASSERT_EQ_INT(2, memory_live);
        ASSERT_EQ_INT(bytes, memory_bytes);
        project_dispose(&destination);
        ASSERT_EQ_INT(0, memory_live);
        ASSERT_EQ_INT(0, memory_bytes);
    }
}

static void test_both_versions_truncate_and_fuzz_valid_documents(void) {
    AppState source_state;
    uint8_t seed[PROJECT_MAX_BYTES];
    uint32_t random = 0xBAADF00D;
    unsigned version;
    for (version = 1; version <= 2; version++) {
        size_t length = version == 1 ? make_legacy_project() : make_encoded_project(&source_state);
        size_t index;
        memcpy(seed, encoded, length);
        for (index = 0; index < length; index++) {
            assert_rejected_without_mutation(seed, index, CODEC_ERR_LENGTH);
        }
        for (index = 0; index < 200; index++) {
            AppState state;
            AppState before;
            CodecError error;
            DependencyGraph dependencies;
            EditorBuffer editor;
            size_t byte;
            random = random * UINT32_C(1664525) + UINT32_C(1013904223);
            byte = 13 + random % (length - 13);
            memcpy(damaged, seed, length);
            damaged[byte] ^= (uint8_t)(1u << (index % 8));
            repair_fixture(damaged, length);
            app_state_init(&state);
            state.viewport = (Viewport2D){3, -4, 2};
            before = state;
            project_dispose(&destination_project);
            set_and_commit(&destination_project, 0, "q=9");
            ASSERT_TRUE(project_set_draft(&destination_project, 0, "q="));
            dependencies = destination_project.dependencies;
            editor = *project_editor(&destination_project);
            if (!project_decode(&state, &destination_project, damaged, length, &error)) {
                ASSERT_TRUE(memcmp(&before, &state, sizeof(state)) == 0);
                ASSERT_STR_EQ("q=9", project_line_source(&destination_project, 0));
                ASSERT_STR_EQ("q=", project_line_display_text(&destination_project, 0));
                ASSERT_TRUE(destination_project.dirty);
                ASSERT_EQ_INT(1, destination_project.count);
                ASSERT_TRUE(memcmp(&editor, project_editor(&destination_project), sizeof(editor)) == 0);
                ASSERT_TRUE(memcmp(&dependencies, &destination_project.dependencies, sizeof(dependencies)) == 0);
            }
        }
    }
}

static void test_inverse_trig_sources_round_trip_through_generic_codec(void) {
    static const char *const sources[] = {"a=arcsin(1)", "b=arccos(0)", "c=arctan(1)"};
    static const float values[] = {1.5707963f, 1.5707963f, 0.7853982f};
    AppState state, loaded;
    CodecError error;
    size_t length, index;

    project_dispose(&source_project);
    app_state_init(&state);
    for (index = 0; index < sizeof(sources) / sizeof(sources[0]); index++) {
        set_and_commit(&source_project, (uint8_t)index, sources[index]);
    }
    length = project_encode(&state, &source_project, encoded, sizeof(encoded), &error);
    ASSERT_TRUE(length > 0);
    ASSERT_EQ_INT(CODEC_OK, error);
    ASSERT_TRUE(project_validate(encoded, length, &error));
    ASSERT_TRUE(project_decode(&loaded, &destination_project, encoded, length, &error));
    for (index = 0; index < sizeof(sources) / sizeof(sources[0]); index++) {
        EvalResult result = project_evaluate_line(&destination_project, (uint8_t)index);
        ASSERT_STR_EQ(sources[index], project_line_source(&destination_project, (uint8_t)index));
        ASSERT_EQ_INT(EVAL_OK, result.error);
        ASSERT_FLOAT_EQ(values[index], result.value);
    }
    ASSERT_EQ_INT(length, project_encode(&loaded, &destination_project,
                                        damaged, sizeof(damaged), &error));
    ASSERT_TRUE(memcmp(encoded, damaged, length) == 0);
}

void run_project_codec_tests(void) {
    project_init(&source_project);
    project_init(&destination_project);
    test_inverse_trig_sources_round_trip_through_generic_codec();
    test_v1_fixture_migrates_with_default_viewport();
    test_v2_exact_bytes_and_clean_viewport_save();
    test_v2_omits_a_draft_when_editing_an_unselected_line();
    test_invalid_viewport_encode_keeps_output_and_project_intact();
    test_v1_truncation_length_crc_and_version_fail_transactionally();
    test_v2_viewport_corruption_is_rejected_before_allocation();
    test_v2_accepts_finite_centers_and_inclusive_scale_clamps();
    test_decode_allocation_failures_preserve_destination_and_release_staging();
    test_project_codec_round_trips_committed_and_draft_sources();
    test_project_codec_round_trips_an_empty_project();
    test_project_codec_round_trips_a_cleared_definition();
    test_loaded_uncommitted_draft_cancel_preserves_saved_slot_count();
    test_project_codec_rejects_corrupt_inputs_transactionally();
    test_project_codec_rejects_duplicate_saved_definitions();
    test_project_codec_restores_only_the_active_duplicate_draft();
    test_decoder_fuzz_keeps_destinations_transactional();
    test_both_versions_truncate_and_fuzz_valid_documents();
    project_dispose(&source_project);
    project_dispose(&destination_project);
}
