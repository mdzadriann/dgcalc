#ifndef DGCALC_TEST_GRAPHX_H
#define DGCALC_TEST_GRAPHX_H

#include <stdint.h>

typedef enum {
    gfx_screen,
    gfx_buffer
} gfx_location_t;

extern uint16_t fake_gfx_palette[256];
#define gfx_palette fake_gfx_palette

uint16_t gfx_RGBTo1555(uint8_t red, uint8_t green, uint8_t blue);
void gfx_Begin(void);
void gfx_End(void);
void fake_gfx_set_draw_buffer(void);
#define gfx_SetDrawBuffer() fake_gfx_set_draw_buffer()
void gfx_SetDraw(uint8_t location);
uint8_t gfx_GetDraw(void);
void gfx_SetTextScale(uint8_t width, uint8_t height);
void gfx_SetTextFGColor(uint8_t color);
void gfx_SetTextBGColor(uint8_t color);
void gfx_SetTextTransparentColor(uint8_t color);
void gfx_SetColor(uint8_t color);
void gfx_FillScreen(uint8_t color);
void gfx_FillRectangle_NoClip(int x, int y, int width, int height);
void gfx_Rectangle_NoClip(int x, int y, int width, int height);
void gfx_HorizLine_NoClip(int x, int y, int length);
void gfx_VertLine_NoClip(int x, int y, int length);
void gfx_Line(int x0, int y0, int x1, int y1);
void gfx_PrintStringXY(const char *text, int x, int y);
void gfx_SetTextXY(int x, int y);
void gfx_PrintUInt(unsigned int value, int digits);
unsigned int gfx_GetStringWidth(const char *text);
void gfx_Blit(gfx_location_t source);
#define gfx_BlitScreen() gfx_Blit(gfx_screen)
#define gfx_BlitBuffer() gfx_Blit(gfx_buffer)
void gfx_SwapDraw(void);

#endif
