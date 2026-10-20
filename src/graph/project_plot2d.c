#include "graph/project_plot2d.h"

#include <math.h>
#include <string.h>

typedef struct {
    const Project *project;
    ProjectPlot2DSegmentFn segment;
    void *user;
    uint8_t index;
} PlotContext;

static bool sample_project(void *user, float x, float y, float *residual) {
    PlotContext *context = user;
    EvalResult result = project_evaluate_2d(context->project, context->index, x, y);
    *residual = result.value;
    return result.error == EVAL_OK && isfinite(result.value);
}

static void emit_project(void *user, int16_t x0, int16_t y0,
                          int16_t x1, int16_t y1) {
    PlotContext *context = user;
    context->segment(context->user, context->index, x0, y0, x1, y1);
}

void project_plot2d_begin(ProjectPlot2D *scheduler, Viewport2D viewport,
                          GraphRect rect, uint8_t step) {
    if (!scheduler) return;
    memset(scheduler, 0, sizeof(*scheduler));
    if (plot2d_begin(&scheduler->job, viewport, rect, step))
        scheduler->state = PROJECT_PLOT2D_PROGRESS;
}

ProjectPlot2DStep project_plot2d_step(ProjectPlot2D *scheduler,
                                     const Project *project,
                                     ProjectPlot2DSegmentFn segment, void *user) {
    PlotContext context;
    Plot2DStep result;
    if (!scheduler) return PROJECT_PLOT2D_ERROR;
    if (scheduler->state != PROJECT_PLOT2D_PROGRESS) return scheduler->state;
    if (!project || !segment) {
        scheduler->state = PROJECT_PLOT2D_ERROR;
        return scheduler->state;
    }
    if (!scheduler->active) {
        while (scheduler->expression_index < PROJECT_MAX_EXPRESSIONS &&
               !project_line_is_drawable_2d(project, scheduler->expression_index))
            scheduler->expression_index++;
        if (scheduler->expression_index == PROJECT_MAX_EXPRESSIONS) {
            scheduler->state = PROJECT_PLOT2D_DONE;
            return scheduler->state;
        }
        if (!plot2d_begin(&scheduler->job, scheduler->job.viewport,
                          scheduler->job.rect, scheduler->job.step)) {
            scheduler->state = PROJECT_PLOT2D_ERROR;
            return scheduler->state;
        }
        scheduler->active = true;
    }
    context = (PlotContext){project, segment, user, scheduler->expression_index};
    result = plot2d_step(&scheduler->job, sample_project, &context,
                         emit_project, &context);
    if (result == PLOT2D_ERROR) scheduler->state = PROJECT_PLOT2D_ERROR;
    if (result == PLOT2D_DONE) {
        scheduler->active = false;
        scheduler->expression_index++;
        if (scheduler->expression_index == PROJECT_MAX_EXPRESSIONS)
            scheduler->state = PROJECT_PLOT2D_DONE;
    }
    return scheduler->state;
}

void project_plot2d_cancel(ProjectPlot2D *scheduler) {
    if (!scheduler) return;
    scheduler->active = false;
    scheduler->job.state = PLOT2D_DONE;
    scheduler->state = PROJECT_PLOT2D_DONE;
}
