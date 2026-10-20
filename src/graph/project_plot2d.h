#ifndef DGCALC_PROJECT_PLOT2D_H
#define DGCALC_PROJECT_PLOT2D_H

#include "graph/plot2d.h"
#include "project/project.h"

typedef enum {
    PROJECT_PLOT2D_ERROR, PROJECT_PLOT2D_PROGRESS, PROJECT_PLOT2D_DONE
} ProjectPlot2DStep;

typedef void (*ProjectPlot2DSegmentFn)(void *user, uint8_t expression_index,
                                      int16_t x0, int16_t y0,
                                      int16_t x1, int16_t y1);

typedef struct {
    /* The embedded job also retains the viewport, rectangle and step for reuse. */
    Plot2DJob job;
    uint8_t expression_index;
    bool active;
    ProjectPlot2DStep state;
} ProjectPlot2D;

/* Restarts any state. Invalid settings leave ERROR; NULL scheduler is a no-op. */
void project_plot2d_begin(ProjectPlot2D *scheduler, Viewport2D viewport,
                          GraphRect rect, uint8_t step);
/* At most one plot row. NULL scheduler returns ERROR; NULL project/callback
   makes an in-progress scheduler ERROR. Terminal states do no work even with
   NULL arguments. user may be NULL. No supplied pointers survive this call. */
ProjectPlot2DStep project_plot2d_step(ProjectPlot2D *scheduler,
                                     const Project *project,
                                     ProjectPlot2DSegmentFn segment, void *user);
/* Stops work as DONE; safe on zeroed storage and NULL. Begin can restart it. */
void project_plot2d_cancel(ProjectPlot2D *scheduler);

#endif
