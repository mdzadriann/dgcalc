#include "storage/project_codec.h"

#include "math/parser.h"
#include "math/validate.h"

#include <string.h>

#define CODEC_HEADER_BYTES 13u
#define CODEC_METADATA_BYTES 4u

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
           project->count <= PROJECT_MAX_EXPRESSIONS;
}

size_t project_encode(const AppState *state,
                      const Project *project,
                      uint8_t *output,
                      size_t capacity,
                      CodecError *error) {
    size_t committed_lengths[PROJECT_MAX_EXPRESSIONS];
    size_t draft_lengths[PROJECT_MAX_EXPRESSIONS];
    size_t needed = CODEC_HEADER_BYTES + CODEC_METADATA_BYTES;
    size_t offset;
    uint8_t index;

    if (!valid_state(state, project)) {
        set_error(error, CODEC_ERR_CONTENT);
        return 0;
    }

    for (index = 0; index < project->count; index++) {
        const ExpressionLine *line = &project->lines[index];
        size_t committed = 0;
        size_t draft;

        if ((line->has_committed &&
             !text_length(line->committed_source, &committed)) ||
            !text_length(line->draft.text, &draft) ||
            draft != line->draft.length) {
            set_error(error, CODEC_ERR_CONTENT);
            return 0;
        }
        committed_lengths[index] = line->has_committed ? committed : 0;
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

    for (index = 0; index < project->count; index++) {
        const ExpressionLine *line = &project->lines[index];
        size_t committed = committed_lengths[index];
        size_t draft = draft_lengths[index];

        write_u16(&output[offset], (uint16_t)committed);
        offset += 2;
        memcpy(&output[offset], line->committed_source, committed);
        offset += committed;
        write_u16(&output[offset], (uint16_t)draft);
        offset += 2;
        memcpy(&output[offset], line->draft.text, draft);
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

static bool validate_source(const uint8_t *data, uint16_t length) {
    ParseResult parsed;
    ValidationResult validated;

    memcpy(source_scratch, data, length);
    source_scratch[length] = '\0';
    parsed = parser_parse(source_scratch, &validation_arena);
    if (!parsed.ok) {
        return false;
    }
    validated = expression_validate(&validation_arena, parsed.root);
    return validated.ok;
}

static bool validate_payload(const uint8_t *input,
                             size_t length,
                             uint8_t count,
                             CodecError *error) {
    size_t offset = CODEC_HEADER_BYTES + CODEC_METADATA_BYTES;
    uint8_t index;

    for (index = 0; index < count; index++) {
        uint16_t committed;
        uint16_t draft;

        if (offset + 2 > length) {
            set_error(error, CODEC_ERR_LENGTH);
            return false;
        }
        committed = read_u16(&input[offset]);
        offset += 2;
        if (committed >= EDITOR_CAPACITY || offset + committed > length ||
            !bytes_are_text(&input[offset], committed) ||
            (committed != 0 && !validate_source(&input[offset], committed))) {
            set_error(error, committed >= EDITOR_CAPACITY ||
                                     offset + committed > length
                                 ? CODEC_ERR_LENGTH
                                 : CODEC_ERR_CONTENT);
            return false;
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
    if (input[4] != PROJECT_FORMAT_VERSION) {
        set_error(error, CODEC_ERR_VERSION);
        return false;
    }

    payload_length = read_u32(&input[5]);
    if (payload_length > PROJECT_MAX_BYTES - CODEC_HEADER_BYTES ||
        (size_t)payload_length + CODEC_HEADER_BYTES != length ||
        payload_length < CODEC_METADATA_BYTES) {
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
    if (metadata->mode > GRAPH_MODE_3D || metadata->view > APP_VIEW_GRAPH ||
        metadata->selected >= PROJECT_MAX_EXPRESSIONS ||
        metadata->count > PROJECT_MAX_EXPRESSIONS) {
        set_error(error, CODEC_ERR_CONTENT);
        return false;
    }
    if (!validate_payload(input, length, metadata->count, error)) {
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
    size_t offset;
    uint8_t index;

    if (state_destination == NULL || project_destination == NULL ||
        !validate_document(input, length, error, &metadata)) {
        return false;
    }

    project_init(project_destination);
    offset = CODEC_HEADER_BYTES + CODEC_METADATA_BYTES;
    for (index = 0; index < metadata.count; index++) {
        uint16_t committed = read_u16(&input[offset]);
        uint16_t draft;
        offset += 2;
        if (committed != 0) {
            copy_source(source_scratch, &input[offset], committed);
            (void)project_set_draft(project_destination, index, source_scratch);
            (void)project_commit_line(project_destination, index);
        }
        offset += committed;
        draft = read_u16(&input[offset]);
        offset += 2;
        copy_source(source_scratch, &input[offset], draft);
        (void)project_set_draft(project_destination, index, source_scratch);
        offset += draft;
    }

    project_destination->count = metadata.count;
    project_destination->dirty = false;
    state_destination->mode = (GraphMode)metadata.mode;
    state_destination->view = (AppView)metadata.view;
    state_destination->selected_expression = metadata.selected;
    set_error(error, CODEC_OK);
    return true;
}
