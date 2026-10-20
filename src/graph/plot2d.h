#ifndef DGCALC_PLOT2D_H
#define DGCALC_PLOT2D_H

#include <stdbool.h>
#include "graph/viewport.h"

#define PLOT2D_COARSE_STEP 8
#define PLOT2D_FINE_STEP 4
#define PLOT2D_MAX_COLUMNS 81

typedef bool (*Plot2DSampleFn)(void *user, float x, float y, float *residual);
typedef void (*Plot2DSegmentFn)(void *user, int16_t x0, int16_t y0,
                               int16_t x1, int16_t y1);
typedef enum { PLOT2D_ERROR, PLOT2D_PROGRESS, PLOT2D_DONE } Plot2DStep;

typedef struct {
    float values[2][PLOT2D_MAX_COLUMNS];
    bool valid[2][PLOT2D_MAX_COLUMNS];
    Viewport2D viewport;
    GraphRect rect;
    int16_t row_y;
    uint8_t columns, step, top;
    bool primed;
    Plot2DStep state;
} Plot2DJob;

/* A failed begin leaves ERROR; begin may restart any job, including DONE. */
bool plot2d_begin(Plot2DJob *job, Viewport2D viewport, GraphRect rect, uint8_t step);
/* Processes at most one cell row. DONE is idempotent, including with NULL callbacks. */
Plot2DStep plot2d_step(Plot2DJob *job, Plot2DSampleFn sample, void *sample_user,
                      Plot2DSegmentFn segment, void *segment_user);

#endif
