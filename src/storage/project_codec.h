#ifndef DGCALC_PROJECT_CODEC_H
#define DGCALC_PROJECT_CODEC_H

#include "app/app.h"
#include "project/project.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROJECT_FORMAT_VERSION 1
#define PROJECT_MAGIC "DGCE"
#define PROJECT_MAX_BYTES                                                   \
    (13 + 4 + PROJECT_MAX_EXPRESSIONS * (4 + 2 * (EDITOR_CAPACITY - 1)))

typedef enum {
    CODEC_OK,
    CODEC_ERR_CAPACITY,
    CODEC_ERR_MAGIC,
    CODEC_ERR_VERSION,
    CODEC_ERR_LENGTH,
    CODEC_ERR_CRC,
    CODEC_ERR_CONTENT
} CodecError;

size_t project_encode(const AppState *state,
                      const Project *project,
                      uint8_t *output,
                      size_t capacity,
                      CodecError *error);
bool project_validate(const uint8_t *input,
                      size_t length,
                      CodecError *error);
bool project_decode(AppState *state_destination,
                    Project *project_destination,
                    const uint8_t *input,
                    size_t length,
                    CodecError *error);
uint32_t project_crc32(const uint8_t *data, size_t length);

#endif
