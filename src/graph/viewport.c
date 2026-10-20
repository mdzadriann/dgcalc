#include "graph/viewport.h"

#include <math.h>

void viewport2d_init(Viewport2D *viewport) {
    viewport->center_x = 0;
    viewport->center_y = 0;
    viewport->units_per_pixel = 20.0f / 320.0f;
}

void viewport2d_screen_to_world(const Viewport2D *viewport, GraphRect rect,
                                float screen_x, float screen_y,
                                float *world_x, float *world_y) {
    *world_x = viewport->center_x +
               (screen_x - rect.x - rect.width * 0.5f) * viewport->units_per_pixel;
    *world_y = viewport->center_y -
               (screen_y - rect.y - rect.height * 0.5f) * viewport->units_per_pixel;
}

void viewport2d_world_to_screen(const Viewport2D *viewport, GraphRect rect,
                                float world_x, float world_y,
                                float *screen_x, float *screen_y) {
    *screen_x = rect.x + rect.width * 0.5f +
                (world_x - viewport->center_x) / viewport->units_per_pixel;
    *screen_y = rect.y + rect.height * 0.5f -
                (world_y - viewport->center_y) / viewport->units_per_pixel;
}

void viewport2d_pan_pixels(Viewport2D *viewport, float dx, float dy) {
    viewport->center_x += dx * viewport->units_per_pixel;
    viewport->center_y -= dy * viewport->units_per_pixel;
}

void viewport2d_zoom(Viewport2D *viewport, float factor) {
    float scale;

    if (!isfinite(factor) || factor <= 0) return;
    scale = viewport->units_per_pixel * factor;
    if (scale < VIEWPORT2D_MIN_SCALE) scale = VIEWPORT2D_MIN_SCALE;
    if (scale > VIEWPORT2D_MAX_SCALE) scale = VIEWPORT2D_MAX_SCALE;
    viewport->units_per_pixel = scale;
}

float viewport2d_grid_step(const Viewport2D *viewport) {
    float target = viewport->units_per_pixel * 20.0f;
    float decade = 1.0f;

    while (decade > target) decade *= 0.1f;
    while (decade * 10.0f <= target) decade *= 10.0f;
    /* Allow rounding at a decade boundary to retain 20-pixel spacing. */
    target *= 0.999999f;
    if (decade >= target) return decade;
    if (decade * 2.0f >= target) return decade * 2.0f;
    if (decade * 5.0f >= target) return decade * 5.0f;
    return decade * 10.0f;
}
