#include "storage/project_codec.h"

#include "math/parser.h"
#include "math/validate.h"

#include <float.h>
#include <math.h>
#include <string.h>

#define CODEC_HEADER_BYTES 13u
#define CODEC_V1_METADATA_BYTES 4u
#define CODEC_V2_METADATA_BYTES 16u

_Static_assert(sizeof(float) == sizeof(uint32_t) && FLT_RADIX == 2 &&
                   FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
               "Saved viewports require IEEE-754 binary32 floats");

static AstArena validation_arena;
static char source_scratch[EDITOR_CAPACITY];

static void set_error(CodecError *error, CodecError value) {
    if (error != NULL) {
        *error = value;
    }
}

static bool text_length(const char *text, size_t *length) {
    size_t index;

    for (index = 0; index < EDITOR_CAPACITY; index++) {
        if (text[index] == '\0') {
            *length = index;
            return true;
        }
    }
    return false;
}

static void write_u16(uint8_t *destination, uint16_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *destination, uint32_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
    destination[2] = (uint8_t)(value >> 16);
    destination[3] = (uint8_t)(value >> 24);
}

static uint16_t read_u16(const uint8_t *source) {
    return (uint16_t)((uint16_t)source[0] |
                      ((uint16_t)source[1] << 8));
}

static uint32_t read_u32(const uint8_t *source) {
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) |
           ((uint32_t)source[3] << 24);
}

static void write_float(uint8_t *destination, float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    write_u32(destination, bits);
}

static float read_float(const uint8_t *source) {
    uint32_t bits = read_u32(source);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static bool valid_viewport(const Viewport2D *viewport) {
    return isfinite(viewport->center_x) && isfinite(viewport->center_y) &&
           isfinite(viewport->units_per_pixel) &&
           viewport->units_per_pixel >= VIEWPORT2D_MIN_SCALE &&
           viewport->units_per_pixel <= VIEWPORT2D_MAX_SCALE;
}

uint32_t project_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = UINT32_MAX;
    size_t index;

    for (index = 0; index < length; index++) {
        uint8_t bit;
        crc ^= data[index];
        for (bit = 0; bit < 8; bit++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }
    return ~crc;
}

static bool valid_state(const AppState *state, const Project *project) {
    return state->mode <= GRAPH_MODE_3D &&
           state->view <= APP_VIEW_GRAPH &&
           state->selected_expression < PROJECT_MAX_EXPRESSIONS &&
           project->count <= PROJECT_MAX_EXPRESSIONS &&
           valid_viewport(&state->viewport);
}

static const char *saved_draft(const AppState *state, const Project *project,
                               uint8_t index) {
    return index == state->selected_expression &&
                   index == project_editing_index(project)
               ? project_line_display_text(project, index)
               : project_line_source(project, index);
}

size_t project_encode(const AppState *state,
                      const Project *project,
                      uint8_t *output,
                      size_t capacity,
                      CodecError *error) {
    size_t committed_lengths[PROJECT_MAX_EXPRESSIONS];
    size_t draft_lengths[PROJECT_MAX_EXPRESSIONS];
    size_t needed = CODEC_HEADER_BYTES + CODEC_V2_METADATA_BYTES;
    size_t offset;
    uint8_t index;

    if (!valid_state(state, project)) {
        set_error(error, CODEC_ERR_CONTENT);
        return 0;
    }

    for (index = 0; index < project->count; index++) {
        size_t committed = 0;
        size_t draft;

        if (!text_length(project_line_source(project, index), &committed) ||
            !text_length(saved_draft(state, project, index), &draft)) {
            set_error(error, CODEC_ERR_CONTENT);
            return 0;
        }
        committed_lengths[index] = committed;
        draft_lengths[index] = draft;
        needed += 4 + committed_lengths[index] + draft;
    }

    if (needed > capacity || needed > PROJECT_MAX_BYTES) {
        set_error(error, CODEC_ERR_CAPACITY);
        return 0;
    }

    memcpy(output, PROJECT_MAGIC, 4);
    output[4] = PROJECT_FORMAT_VERSION;
    offset = CODEC_HEADER_BYTES;
    output[offset++] = (uint8_t)state->mode;
    output[offset++] = (uint8_t)state->view;
    output[offset++] = state->selected_expression;
    output[offset++] = project->count;
    write_float(&output[offset], state->viewport.center_x);
    offset += 4;
    write_float(&output[offset], state->viewport.center_y);
    offset += 4;
    write_float(&output[offset], state->viewport.units_per_pixel);
    offset += 4;

    for (index = 0; index < project->count; index++) {
        size_t committed = committed_lengths[index];
        size_t draft = draft_lengths[index];

        write_u16(&output[offset], (uint16_t)committed);
        offset += 2;
        memcpy(&output[offset], project_line_source(project, index), committed);
        offset += committed;
        write_u16(&output[offset], (uint16_t)draft);
        offset += 2;
        memcpy(&output[offset], saved_draft(state, project, index), draft);
        offset += draft;
    }

    write_u32(&output[5], (uint32_t)(offset - CODEC_HEADER_BYTES));
    write_u32(&output[9],
              project_crc32(&output[CODEC_HEADER_BYTES],
                            offset - CODEC_HEADER_BYTES));
    set_error(error, CODEC_OK);
    return offset;
}

static bool bytes_are_text(const uint8_t *data, size_t length) {
    size_t index;
    for (index = 0; index < length; index++) {
        if (data[index] == 0) {
            return false;
        }
    }
    return true;
}

static bool validate_source(const uint8_t *data,
                            uint16_t length,
                            ExprRole *role,
                            uint8_t *definition_name) {
    ParseResult parsed;
    ValidationResult validated;
    const AstNode *root;

    memcpy(source_scratch, data, length);
    source_scratch[length] = '\0';
    parsed = parser_parse(source_scratch, &validation_arena);
    if (!parsed.ok) {
        return false;
    }
    validated = expression_validate(ast_arena_view(&validation_arena), parsed.root);
    if (!validated.ok) {
        return false;
    }
    *role = validated.role;
    if (validated.role == EXPR_ROLE_DEFINITION ||
        validated.role == EXPR_ROLE_FUNCTION_DEFINITION) {
        root = &validation_arena.nodes[parsed.root];
        *definition_name = validation_arena.nodes[root->left].name;
    }
    return true;
}

static bool validate_payload(const uint8_t *input,
                             size_t length,
                             size_t offset,
                             uint8_t count,
                             CodecError *error) {
    uint32_t scalar_definitions = 0;
    uint32_t function_definitions = 0;
    uint8_t index;

    for (index = 0; index < count; index++) {
        uint16_t committed;
        uint16_t draft;
        ExprRole role = EXPR_ROLE_INVALID;
        uint8_t definition_name = 0;

        if (offset + 2 > length) {
            set_error(error, CODEC_ERR_LENGTH);
            return false;
        }
        committed = read_u16(&input[offset]);
        offset += 2;
        if (committed >= EDITOR_CAPACITY || offset + committed > length ||
            !bytes_are_text(&input[offset], committed) ||
            (committed != 0 &&
             !validate_source(&input[offset],
                              committed,
                              &role,
                              &definition_name))) {
            set_error(error, committed >= EDITOR_CAPACITY ||
                                     offset + committed > length
                                 ? CODEC_ERR_LENGTH
                                 : CODEC_ERR_CONTENT);
            return false;
        }
        if (role == EXPR_ROLE_DEFINITION ||
            role == EXPR_ROLE_FUNCTION_DEFINITION) {
            uint32_t bit = UINT32_C(1) << (definition_name - 'a');
            uint32_t *definitions = role == EXPR_ROLE_DEFINITION
                                        ? &scalar_definitions
                                        : &function_definitions;
            if ((*definitions & bit) != 0) {
                set_error(error, CODEC_ERR_CONTENT);
                return false;
            }
            *definitions |= bit;
        }
        offset += committed;

        if (offset + 2 > length) {
            set_error(error, CODEC_ERR_LENGTH);
            return false;
        }
        draft = read_u16(&input[offset]);
        offset += 2;
        if (draft >= EDITOR_CAPACITY || offset + draft > length) {
            set_error(error, CODEC_ERR_LENGTH);
            return false;
        }
        if (!bytes_are_text(&input[offset], draft)) {
            set_error(error, CODEC_ERR_CONTENT);
            return false;
        }
        offset += draft;
    }

    if (offset != length) {
        set_error(error, CODEC_ERR_CONTENT);
        return false;
    }
    return true;
}

static void copy_source(char *destination,
                        const uint8_t *source,
                        uint16_t length) {
    memcpy(destination, source, length);
    destination[length] = '\0';
}

typedef struct {
    uint8_t mode;
    uint8_t view;
    uint8_t selected;
    uint8_t count;
    size_t bytes;
    Viewport2D viewport;
} CodecMetadata;

static bool validate_document(const uint8_t *input,
                              size_t length,
                              CodecError *error,
                              CodecMetadata *metadata) {
    uint32_t payload_length;

    if (input == NULL || length < CODEC_HEADER_BYTES) {
        set_error(error, CODEC_ERR_LENGTH);
        return false;
    }
    if (memcmp(input, PROJECT_MAGIC, 4) != 0) {
        set_error(error, CODEC_ERR_MAGIC);
        return false;
    }
    if (input[4] != 1 && input[4] != PROJECT_FORMAT_VERSION) {
        set_error(error, CODEC_ERR_VERSION);
        return false;
    }
    metadata->bytes = input[4] == 1 ? CODEC_V1_METADATA_BYTES
                                   : CODEC_V2_METADATA_BYTES;

    payload_length = read_u32(&input[5]);
    if (payload_length > PROJECT_MAX_BYTES - CODEC_HEADER_BYTES -
                             CODEC_V2_METADATA_BYTES + metadata->bytes ||
        (size_t)payload_length + CODEC_HEADER_BYTES != length ||
        payload_length < metadata->bytes) {
        set_error(error, CODEC_ERR_LENGTH);
        return false;
    }
    if (project_crc32(&input[CODEC_HEADER_BYTES], payload_length) !=
        read_u32(&input[9])) {
        set_error(error, CODEC_ERR_CRC);
        return false;
    }

    metadata->mode = input[13];
    metadata->view = input[14];
    metadata->selected = input[15];
    metadata->count = input[16];
    viewport2d_init(&metadata->viewport);
    if (input[4] == PROJECT_FORMAT_VERSION) {
        metadata->viewport.center_x = read_float(&input[17]);
        metadata->viewport.center_y = read_float(&input[21]);
        metadata->viewport.units_per_pixel = read_float(&input[25]);
    }
    if (metadata->mode > GRAPH_MODE_3D || metadata->view > APP_VIEW_GRAPH ||
        metadata->selected >= PROJECT_MAX_EXPRESSIONS ||
        metadata->count > PROJECT_MAX_EXPRESSIONS ||
        !valid_viewport(&metadata->viewport)) {
        set_error(error, CODEC_ERR_CONTENT);
        return false;
    }
    if (!validate_payload(input, length, CODEC_HEADER_BYTES + metadata->bytes,
                          metadata->count, error)) {
        return false;
    }
    set_error(error, CODEC_OK);
    return true;
}

bool project_validate(const uint8_t *input,
                      size_t length,
                      CodecError *error) {
    CodecMetadata metadata;
    return validate_document(input, length, error, &metadata);
}

bool project_decode(AppState *state_destination,
                    Project *project_destination,
                    const uint8_t *input,
                    size_t length,
                    CodecError *error) {
    CodecMetadata metadata;
    Project staging;
    AppState staging_state;
    size_t offset;
    uint8_t index;

    if (state_destination == NULL || project_destination == NULL ||
        !validate_document(input, length, error, &metadata)) {
        return false;
    }

    project_init_with_memory(&staging, project_destination->memory);
    app_state_init(&staging_state);
    staging_state.mode = (GraphMode)metadata.mode;
    staging_state.view = (AppView)metadata.view;
    staging_state.selected_expression = metadata.selected;
    staging_state.viewport = metadata.viewport;
    offset = CODEC_HEADER_BYTES + metadata.bytes;
    for (index = 0; index < metadata.count; index++) {
        uint16_t committed = read_u16(&input[offset]);
        uint16_t draft;
        offset += 2;
        if (committed != 0) {
            CommitResult result;
            copy_source(source_scratch, &input[offset], committed);
            (void)project_set_draft(&staging, index, source_scratch);
            result = project_commit_line(&staging, index);
            if (!result.ok) {
                project_dispose(&staging);
                set_error(error, result.out_of_memory ? CODEC_ERR_MEMORY
                                                     : CODEC_ERR_CONTENT);
                return false;
            }
        }
        offset += committed;
        draft = read_u16(&input[offset]);
        offset += 2;
        offset += draft;
    }

    staging.count = metadata.count;
    staging.dirty = false;
    offset = CODEC_HEADER_BYTES + metadata.bytes;
    for (index = 0; index < metadata.count; index++) {
        uint16_t committed = read_u16(&input[offset]);
        uint16_t draft;
        offset += 2 + committed;
        draft = read_u16(&input[offset]);
        offset += 2;
        /* Legacy inactive drafts cannot coexist in the single shared editor. */
        if (index == metadata.selected) {
            copy_source(source_scratch, &input[offset], draft);
            if (strcmp(source_scratch, project_line_source(&staging, index)) != 0) {
                (void)project_set_draft(&staging, index, source_scratch);
                (void)project_validate_draft(&staging, index);
            }
        }
        offset += draft;
    }

    staging.count = metadata.count;
    staging.dirty = false;
    project_replace(project_destination, &staging);
    *state_destination = staging_state;
    set_error(error, CODEC_OK);
    return true;
}
