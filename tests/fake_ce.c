#include "fake_ce.h"

#include <graphx.h>
#include <string.h>

uint16_t fake_gfx_palette[256];
uint8_t kb_Data[8];
uint8_t fake_kb_mode;

static const kb_lkey_t *scan_keys;
static size_t scan_key_count;
size_t scan_key_index;
int screen_blits;
int buffer_blits;
int swaps;
static uint8_t draw_color;
static uint8_t text_color;
uint8_t transparent_text_color;
static bool dialog_draw_pending;
TestDrawOperation draw_operations[32];
size_t draw_operation_count;
FooterPrint footer_prints[8];
size_t footer_print_count;
int footer_highlights[5];
uint8_t footer_highlight_colors[5];
size_t footer_highlight_count;

static void record_operation(TestDrawOperation operation) {
    if (draw_operation_count <
        sizeof(draw_operations) / sizeof(draw_operations[0])) {
        draw_operations[draw_operation_count++] = operation;
    }
}

void reset_fake_hardware(void) {
    memset(kb_Data, 0, sizeof(kb_Data));
    scan_keys = NULL;
    scan_key_count = 0;
    scan_key_index = 0;
    screen_blits = 0;
    buffer_blits = 0;
    swaps = 0;
    draw_color = 0;
    text_color = 0;
    transparent_text_color = 0;
    fake_kb_mode = MODE_3_CONTINUOUS;
    dialog_draw_pending = false;
    draw_operation_count = 0;
    footer_print_count = 0;
    footer_highlight_count = 0;
}

void script_keys(const kb_lkey_t *keys, size_t count) {
    scan_keys = keys;
    scan_key_count = count;
    scan_key_index = 0;
}

void kb_Scan(void) {
    kb_lkey_t key = 0;

    memset(kb_Data, 0, sizeof(kb_Data));
    if (scan_key_index < scan_key_count) {
        key = scan_keys[scan_key_index++];
    }
    if (key != 0) {
        kb_Data[key >> 8] = (uint8_t)key;
    }
    fake_kb_mode = MODE_0_IDLE;
}

void kb_Reset(void) { memset(kb_Data, 0, sizeof(kb_Data)); }

void fake_kb_set_mode(uint8_t mode) { fake_kb_mode = mode; }

uint16_t gfx_RGBTo1555(uint8_t red, uint8_t green, uint8_t blue) {
    return (uint16_t)(red + green + blue);
}

void gfx_Begin(void) {}
void gfx_End(void) {}
void fake_gfx_set_draw_buffer(void) {}
void gfx_SetTextScale(uint8_t width, uint8_t height) {
    (void)width;
    (void)height;
}
void gfx_SetTextFGColor(uint8_t color) { text_color = color; }
void gfx_SetTextBGColor(uint8_t color) { (void)color; }
void gfx_SetTextTransparentColor(uint8_t color) {
    transparent_text_color = color;
}
void gfx_SetColor(uint8_t color) { draw_color = color; }
void gfx_FillScreen(uint8_t color) { (void)color; }
void gfx_FillRectangle_NoClip(int x, int y, int width, int height) {
    if (dialog_draw_pending) {
        record_operation(TEST_DRAW_DIALOG);
        dialog_draw_pending = false;
    }
    if (y == 221 && width == 63 && height == 19 &&
        footer_highlight_count < 5) {
        footer_highlights[footer_highlight_count] = x;
        footer_highlight_colors[footer_highlight_count++] = draw_color;
    }
}
void gfx_Rectangle_NoClip(int x, int y, int width, int height) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}
void gfx_HorizLine_NoClip(int x, int y, int length) {
    (void)x;
    (void)y;
    (void)length;
}
void gfx_VertLine_NoClip(int x, int y, int length) {
    (void)x;
    (void)y;
    (void)length;
}
void gfx_Line(int x0, int y0, int x1, int y1) {
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
}
void gfx_SetTextXY(int x, int y) {
    (void)x;
    (void)y;
}
void gfx_PrintUInt(unsigned int value, int digits) {
    (void)value;
    (void)digits;
}
int gfx_GetStringWidth(const char *text) {
    return (int)strlen(text) * 8;
}
void gfx_PrintStringXY(const char *text, int x, int y) {
    if (y >= 220 && footer_print_count < 8) {
        FooterPrint *printed = &footer_prints[footer_print_count++];
        size_t length = strlen(text);

        if (length >= sizeof(printed->text)) {
            length = sizeof(printed->text) - 1;
        }
        memcpy(printed->text, text, length);
        printed->text[length] = '\0';
        printed->x = x;
        printed->color = text_color;
    }
}
void gfx_Blit(gfx_location_t source) {
    if (source == gfx_screen) {
        screen_blits++;
        record_operation(TEST_COPY_SCREEN_TO_BUFFER);
        dialog_draw_pending = true;
    } else {
        buffer_blits++;
        record_operation(TEST_COPY_BUFFER_TO_SCREEN);
    }
}
void gfx_SwapDraw(void) {
    swaps++;
    record_operation(TEST_SWAP_BUFFERS);
}
