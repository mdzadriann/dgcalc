#include "fake_ce.h"

#include <graphx.h>
#include <stdio.h>
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
TestLine graph_lines[8192];
size_t graph_line_count;
TestPrint graph_prints[512];
size_t graph_print_count;
int static_frames;
int swapped_frames[32];
static int buffer_frames[2];
static unsigned draw_buffer;
unsigned visible_buffer;
uint8_t buffer_pixels[2][240][320];
char header_text[2][2][81];
static int text_x, text_y;

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
    graph_line_count = 0;
    graph_print_count = 0;
    static_frames = 0;
    buffer_frames[0] = buffer_frames[1] = 0;
    draw_buffer = 1;
    visible_buffer = 0;
    memset(buffer_pixels, 0, sizeof(buffer_pixels));
    memset(header_text, 0, sizeof(header_text));
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
void fake_gfx_set_draw_buffer(void) { gfx_SetDraw(gfx_buffer); }
void gfx_SetDraw(uint8_t location) {
    draw_buffer = visible_buffer ^ (location == gfx_buffer);
}
uint8_t gfx_GetDraw(void) {
    return draw_buffer == visible_buffer ? gfx_screen : gfx_buffer;
}
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
void gfx_FillScreen(uint8_t color) {
    memset(buffer_pixels[draw_buffer], color, sizeof(buffer_pixels[draw_buffer]));
    memset(header_text[draw_buffer], 0, sizeof(header_text[draw_buffer]));
    buffer_frames[draw_buffer] = ++static_frames;
    footer_print_count = 0;
    footer_highlight_count = 0;
}
void gfx_FillRectangle_NoClip(int x, int y, int width, int height) {
    int row;
    for (row = y; row < y + height; row++)
        memset(&buffer_pixels[draw_buffer][row][x], draw_color, (size_t)width);
    if (x == 0 && y == 0 && width == 320 && height == 24)
        memset(header_text[draw_buffer], 0, sizeof(header_text[draw_buffer]));
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
static void record_line(int x0, int y0, int x1, int y1, bool segment) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int error = dx + dy, x = x0, y = y0;
    for (;;) {
        int twice = 2 * error;
        if (x >= 0 && x < 320 && y >= 0 && y < 240)
            buffer_pixels[draw_buffer][y][x] = draw_color;
        if (x == x1 && y == y1) break;
        if (twice >= dy) { error += dy; x += sx; }
        if (twice <= dx) { error += dx; y += sy; }
    }
    if (graph_line_count < sizeof(graph_lines) / sizeof(graph_lines[0]))
        graph_lines[graph_line_count++] = (TestLine){
            x0, y0, x1, y1, draw_color, buffer_frames[draw_buffer], segment};
}
void gfx_HorizLine_NoClip(int x, int y, int length) {
    record_line(x, y, x + length - 1, y, false);
}
void gfx_VertLine_NoClip(int x, int y, int length) {
    record_line(x, y, x, y + length - 1, false);
}
void gfx_Line(int x0, int y0, int x1, int y1) {
    record_line(x0, y0, x1, y1, true);
}
void gfx_SetTextXY(int x, int y) {
    text_x = x;
    text_y = y;
}
void gfx_PrintUInt(unsigned int value, int digits) {
    char text[16];
    (void)digits;
    snprintf(text, sizeof(text), "%u", value);
    gfx_PrintStringXY(text, text_x, text_y);
    text_x += gfx_GetStringWidth(text);
}
unsigned int gfx_GetStringWidth(const char *text) {
    return (unsigned int)strlen(text) * 8;
}
void gfx_PrintStringXY(const char *text, int x, int y) {
    if (y < 24) {
        char *line = header_text[draw_buffer][y >= 12];
        size_t used = strlen(line);
        if (used + strlen(text) + 1 < 81) {
            if (used) strcat(line, " ");
            strcat(line, text);
        }
    }
    if (y < 220 && graph_print_count < 512) {
        TestPrint *printed = &graph_prints[graph_print_count++];
        size_t length = strlen(text);
        if (length > 40) length = 40;
        memcpy(printed->text, text, length);
        printed->text[length] = '\0';
        printed->x = x;
        printed->y = y;
        printed->color = text_color;
        printed->frame = buffer_frames[draw_buffer];
        printed->buffer = draw_buffer;
        printed->swaps = swaps;
        printed->visible = draw_buffer == visible_buffer;
    }
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
        buffer_frames[visible_buffer ^ 1] = buffer_frames[visible_buffer];
        memcpy(buffer_pixels[visible_buffer ^ 1], buffer_pixels[visible_buffer],
               sizeof(buffer_pixels[0]));
        memcpy(header_text[visible_buffer ^ 1], header_text[visible_buffer], sizeof(header_text[0]));
        screen_blits++;
        record_operation(TEST_COPY_SCREEN_TO_BUFFER);
        dialog_draw_pending = true;
    } else {
        buffer_frames[visible_buffer] = buffer_frames[visible_buffer ^ 1];
        memcpy(buffer_pixels[visible_buffer], buffer_pixels[visible_buffer ^ 1],
               sizeof(buffer_pixels[0]));
        memcpy(header_text[visible_buffer], header_text[visible_buffer ^ 1], sizeof(header_text[0]));
        buffer_blits++;
        record_operation(TEST_COPY_BUFFER_TO_SCREEN);
    }
}
void gfx_SwapDraw(void) {
    if (swaps < 32) swapped_frames[swaps] = buffer_frames[draw_buffer];
    visible_buffer = draw_buffer;
    draw_buffer ^= 1;
    swaps++;
    record_operation(TEST_SWAP_BUFFERS);
}
