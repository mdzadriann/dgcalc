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

typedef struct {
    int x0, y0, x1, y1;
    uint8_t color;
    int frame;
    bool segment;
} TestLine;

typedef struct {
    char text[41];
    int x, y, frame;
    uint8_t color;
    unsigned buffer;
    int swaps;
    bool visible;
} TestPrint;

extern TestLine graph_lines[8192];
extern size_t graph_line_count;
extern TestPrint graph_prints[512];
extern size_t graph_print_count;
extern int static_frames;
extern int swapped_frames[32];
extern uint8_t buffer_pixels[2][240][320];
extern unsigned visible_buffer;
extern char header_text[2][2][81];

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
