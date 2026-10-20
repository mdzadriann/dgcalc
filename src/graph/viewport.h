#ifndef DGCALC_VIEWPORT_H
#define DGCALC_VIEWPORT_H

#include <stdint.h>

typedef struct {
    float center_x;
    float center_y;
    float units_per_pixel;
} Viewport2D;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
} GraphRect;

void viewport2d_init(Viewport2D *viewport);
/* Absolute screen coordinates increase right/down; world coordinates right/up. */
void viewport2d_screen_to_world(const Viewport2D *viewport, GraphRect rect,
                                float screen_x, float screen_y,
                                float *world_x, float *world_y);
void viewport2d_world_to_screen(const Viewport2D *viewport, GraphRect rect,
                                float world_x, float world_y,
                                float *screen_x, float *screen_y);
/* Move the viewport center in screen directions; negative dy moves world up. */
void viewport2d_pan_pixels(Viewport2D *viewport, float dx, float dy);
/* Multiply scale by a finite positive factor, preserving the center. */
void viewport2d_zoom(Viewport2D *viewport, float factor);
float viewport2d_grid_step(const Viewport2D *viewport);

#endif
