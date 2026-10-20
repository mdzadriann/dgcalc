#include "graph/viewport.h"
#include "test.h"

#include <float.h>
#include <math.h>

static void test_default_extent_and_offset_round_trips(void) {
    const GraphRect full = {0, 24, 320, 196};
    const GraphRect offset = {163, 31, 150, 180};
    Viewport2D viewport;
    float x, y, sx, sy;

    viewport2d_init(&viewport);
    viewport2d_screen_to_world(&viewport, full, 0, 122, &x, &y);
    ASSERT_FLOAT_EQ(-10, x);
    ASSERT_FLOAT_EQ(0, y);
    viewport2d_screen_to_world(&viewport, full, 320, 24, &x, &y);
    ASSERT_FLOAT_EQ(10, x);
    ASSERT_FLOAT_EQ(6.125f, y);

    viewport.center_x = 3.5f;
    viewport.center_y = -2.25f;
    viewport2d_screen_to_world(&viewport, offset, 201.25f, 92.5f, &x, &y);
    ASSERT_FLOAT_EQ(1.203125f, x);
    ASSERT_FLOAT_EQ(-0.46875f, y);
    viewport2d_world_to_screen(&viewport, offset, x, y, &sx, &sy);
    ASSERT_FLOAT_EQ(201.25f, sx);
    ASSERT_FLOAT_EQ(92.5f, sy);
    viewport2d_world_to_screen(&viewport, offset, -1.25f, 2.5f, &sx, &sy);
    viewport2d_screen_to_world(&viewport, offset, sx, sy, &x, &y);
    ASSERT_FLOAT_EQ(-1.25f, x);
    ASSERT_FLOAT_EQ(2.5f, y);
}

static void test_pan_direction_and_centered_zoom(void) {
    Viewport2D viewport;

    viewport2d_init(&viewport);
    viewport2d_pan_pixels(&viewport, 32, -19.6f);
    ASSERT_FLOAT_EQ(2, viewport.center_x);
    ASSERT_FLOAT_EQ(1.225f, viewport.center_y);
    viewport2d_zoom(&viewport, 0.8f);
    ASSERT_FLOAT_EQ(0.05f, viewport.units_per_pixel);
    ASSERT_FLOAT_EQ(2, viewport.center_x);
    ASSERT_FLOAT_EQ(1.225f, viewport.center_y);
    viewport2d_zoom(&viewport, 1.25f);
    ASSERT_FLOAT_EQ(0.0625f, viewport.units_per_pixel);
    viewport2d_pan_pixels(&viewport, -32, 19.6f);
    ASSERT_FLOAT_EQ(0, viewport.center_x);
    ASSERT_FLOAT_EQ(0, viewport.center_y);
}

static void test_zoom_clamps_and_rejects_invalid_requests(void) {
    static const float invalid[] = {0, -1, NAN, INFINITY, -INFINITY};
    Viewport2D viewport;
    size_t i;

    viewport2d_init(&viewport);
    for (i = 0; i < 300; i++) viewport2d_zoom(&viewport, 0.8f);
    ASSERT_FLOAT_EQ(0.0001f, viewport.units_per_pixel);
    for (i = 0; i < 300; i++) viewport2d_zoom(&viewport, 1.25f);
    ASSERT_FLOAT_EQ(10000, viewport.units_per_pixel);
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        viewport2d_zoom(&viewport, invalid[i]);
        ASSERT_FLOAT_EQ(10000, viewport.units_per_pixel);
    }
    viewport2d_zoom(&viewport, FLT_MAX);
    ASSERT_FLOAT_EQ(10000, viewport.units_per_pixel);
    viewport2d_zoom(&viewport, FLT_MIN);
    ASSERT_FLOAT_EQ(0.0001f, viewport.units_per_pixel);
    ASSERT_TRUE(isfinite(viewport.units_per_pixel));
}

static void test_adaptive_grid_uses_readable_125_spacing(void) {
    static const struct { float scale, step; } cases[] = {
        {0.0001f, 0.002f}, {0.0003f, 0.01f}, {0.001f, 0.02f},
        {0.0125f, 0.5f}, {0.025f, 0.5f}, {0.0625f, 2},
        {0.1f, 2}, {0.25f, 5}, {1, 20}, {100, 2000},
        {10000, 200000}
    };
    Viewport2D viewport;
    size_t i;

    viewport2d_init(&viewport);
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        float step, spacing;
        viewport.units_per_pixel = cases[i].scale;
        step = viewport2d_grid_step(&viewport);
        spacing = step / viewport.units_per_pixel;
        ASSERT_TRUE(isfinite(step) && step > 0);
        ASSERT_TRUE(fabsf(step - cases[i].step) <= cases[i].step * 0.00001f);
        ASSERT_TRUE(spacing >= 19.999f && spacing <= 50.001f);
        printf("grid scale=%.7g step=%.7g pixels=%.7g\n",
               (double)viewport.units_per_pixel, (double)step, (double)spacing);
    }
}

void run_viewport_tests(void) {
    test_default_extent_and_offset_round_trips();
    test_pan_direction_and_centered_zoom();
    test_zoom_clamps_and_rejects_invalid_requests();
    test_adaptive_grid_uses_readable_125_spacing();
}
