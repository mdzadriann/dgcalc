#ifndef DGCALC_FAKE_CE_H
#define DGCALC_FAKE_CE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <keypadc.h>

typedef struct {
    char text[16];
    int x;
    uint8_t color;
} FooterPrint;

typedef enum {
    TEST_COPY_SCREEN_TO_BUFFER,
    TEST_DRAW_DIALOG,
    TEST_COPY_BUFFER_TO_SCREEN,
    TEST_SWAP_BUFFERS
} TestDrawOperation;

extern size_t scan_key_index;
extern int screen_blits;
extern int buffer_blits;
extern int swaps;
extern uint8_t transparent_text_color;
extern TestDrawOperation draw_operations[32];
extern size_t draw_operation_count;
extern FooterPrint footer_prints[8];
extern size_t footer_print_count;
extern int footer_highlights[5];
extern uint8_t footer_highlight_colors[5];
extern size_t footer_highlight_count;

void reset_fake_hardware(void);
void script_keys(const kb_lkey_t *keys, size_t count);

#endif
