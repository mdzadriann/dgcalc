#include "graph/plot2d.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <string.h>

typedef struct { float x, y; } Point;

static bool sample_at(const Plot2DJob *job, Point p, Plot2DSampleFn sample,
                      void *user, float *value) {
    float x, y;
    viewport2d_screen_to_world(&job->viewport, job->rect, p.x, p.y, &x, &y);
    return isfinite(x) && isfinite(y) && sample(user, x, y, value) && isfinite(*value);
}

bool plot2d_begin(Plot2DJob *job, Viewport2D viewport, GraphRect rect, uint8_t step) {
    int32_t right = (int32_t)rect.x + rect.width - 1;
    int32_t bottom = (int32_t)rect.y + rect.height - 1;
    unsigned columns;
    float x, y;
    if (!job) return false;
    memset(job, 0, sizeof(*job));
    if ((step != PLOT2D_COARSE_STEP && step != PLOT2D_FINE_STEP) ||
        rect.width < 2 || rect.height < 2 || right > INT16_MAX || bottom > INT16_MAX ||
        !isfinite(viewport.center_x) || !isfinite(viewport.center_y) ||
        !isfinite(viewport.units_per_pixel) || viewport.units_per_pixel <= 0) return false;
    columns = ((unsigned)rect.width - 2) / step + 2;
    if (columns > PLOT2D_MAX_COLUMNS) return false;
    viewport2d_screen_to_world(&viewport, rect, rect.x, rect.y, &x, &y);
    if (!isfinite(x) || !isfinite(y)) return false;
    viewport2d_screen_to_world(&viewport, rect, (float)right, (float)bottom, &x, &y);
    if (!isfinite(x) || !isfinite(y)) return false;
    job->viewport = viewport;
    job->rect = rect;
    job->columns = (uint8_t)columns;
    job->step = step;
    job->row_y = rect.y;
    job->state = PLOT2D_PROGRESS;
    return true;
}

static int16_t column_x(const Plot2DJob *job, uint8_t column) {
    if (column == job->columns - 1) return job->rect.x + job->rect.width - 1;
    return job->rect.x + column * job->step;
}

static void sample_row(Plot2DJob *job, uint8_t row, int16_t y,
                       Plot2DSampleFn sample, void *user) {
    uint8_t column;
    for (column = 0; column < job->columns; column++) {
        Point p = {(float)column_x(job, column), (float)y};
        job->valid[row][column] = sample_at(job, p, sample, user,
                                           &job->values[row][column]);
    }
}

static bool crossing(const Plot2DJob *job, Point a, Point b, float va, float vb,
                      Plot2DSampleFn sample, void *user, Point *p) {
    uint8_t i;
    bool toward_zero = false;
    float scale, fraction;
    if (va == 0) { *p = a; return true; }
    if (vb == 0) { *p = b; return true; }
    for (i = 0; i < 3; i++) {
        Point mid = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
        float vm;
        if (!sample_at(job, mid, sample, user, &vm)) return false;
        if (vm == 0) { *p = mid; return true; }
        if ((vm > 0) == (va > 0)) {
            if (fabsf(vm) < fabsf(va)) toward_zero = true;
            a = mid; va = vm;
        } else {
            if (fabsf(vm) < fabsf(vb)) toward_zero = true;
            b = mid; vb = vm;
        }
    }
    if (!toward_zero) return false;
    scale = fmaxf(fabsf(va), fabsf(vb));
    fraction = (fabsf(va) / scale) / (fabsf(va) / scale + fabsf(vb) / scale);
    if (fraction < 0) fraction = 0;
    if (fraction > 1) fraction = 1;
    p->x = a.x + (b.x - a.x) * fraction;
    p->y = a.y + (b.y - a.y) * fraction;
    return true;
}

static int16_t clipped_pixel(float value, int16_t min, int16_t length) {
    int32_t pixel = (int32_t)floorf(value + 0.5f);
    int32_t max = (int32_t)min + length - 1;
    if (pixel < min) pixel = min;
    if (pixel > max) pixel = max;
    return (int16_t)pixel;
}

static void emit(const Plot2DJob *job, Point a, Point b, float largest,
                  Plot2DSampleFn sample, void *sample_user,
                  Plot2DSegmentFn segment, void *segment_user) {
    Point mid = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    float value;
    int16_t x0, y0, x1, y1;
    if (!sample_at(job, mid, sample, sample_user, &value) ||
        (fabsf(value) > largest && fabsf(value) - largest > 8 * FLT_EPSILON * largest))
        return;
    x0 = clipped_pixel(a.x, job->rect.x, job->rect.width);
    x1 = clipped_pixel(b.x, job->rect.x, job->rect.width);
    y0 = clipped_pixel(a.y, job->rect.y, job->rect.height);
    y1 = clipped_pixel(b.y, job->rect.y, job->rect.height);
    if (x0 != x1 || y0 != y1) segment(segment_user, x0, y0, x1, y1);
}

static void cell(const Plot2DJob *job, uint8_t column, int16_t bottom,
                  Plot2DSampleFn sample, void *sample_user,
                  Plot2DSegmentFn segment, void *segment_user) {
    uint8_t top = job->top, lower = top ^ 1;
    float values[4], largest = 0, orientation = 0;
    Point corners[4], points[4];
    uint8_t edges[4], count = 0, i;
    bool positive[4], valid[4];
    if (!job->valid[top][column] || !job->valid[top][column+1] ||
        !job->valid[lower][column] || !job->valid[lower][column+1]) return;
    values[0] = job->values[top][column];
    values[1] = job->values[top][column+1];
    values[2] = job->values[lower][column+1];
    values[3] = job->values[lower][column];
    for (i = 0; i < 4; i++) {
        if (orientation == 0 && values[i] != 0) orientation = values[i];
        largest = fmaxf(largest, fabsf(values[i]));
    }
    if (orientation == 0) return;
    /* Normalize signs so exact-zero corners have the same topology after negation. */
    for (i = 0; i < 4; i++) positive[i] = orientation > 0 ? values[i] > 0 : values[i] < 0;
    corners[0] = (Point){(float)column_x(job,column), (float)job->row_y};
    corners[1] = (Point){(float)column_x(job,column+1), (float)job->row_y};
    corners[2] = (Point){corners[1].x, (float)bottom};
    corners[3] = (Point){corners[0].x, (float)bottom};
    for (i = 0; i < 4; i++) {
        uint8_t next = (i+1) % 4;
        if (positive[i] == positive[next]) continue;
        edges[count++] = i;
        valid[i] = crossing(job, corners[i], corners[next], values[i], values[next],
                             sample, sample_user, &points[i]);
    }
    if (count == 2) {
        if (valid[edges[0]] && valid[edges[1]])
            emit(job, points[edges[0]], points[edges[1]], largest,
                 sample, sample_user, segment, segment_user);
    } else if (count == 4) {
        Point center = {(corners[0].x + corners[2].x)*0.5f,
                        (corners[0].y + corners[2].y)*0.5f};
        float value;
        bool center_positive;
        uint8_t start;
        if (!sample_at(job, center, sample, sample_user, &value)) return;
        center_positive = orientation > 0 ? value > 0 : value < 0;
        start = center_positive == positive[0] ? 0 : 1;
        for (i = 0; i < 2; i++) {
            uint8_t a = (start + 2*i) % 4, b = (a+1) % 4;
            if (valid[a] && valid[b]) emit(job, points[a], points[b], largest,
                                         sample, sample_user, segment, segment_user);
        }
    }
}

Plot2DStep plot2d_step(Plot2DJob *job, Plot2DSampleFn sample, void *sample_user,
                      Plot2DSegmentFn segment, void *segment_user) {
    int32_t next, bottom;
    uint8_t column;
    if (!job) return PLOT2D_ERROR;
    if (job->state != PLOT2D_PROGRESS) return job->state;
    if (!sample || !segment) { job->state = PLOT2D_ERROR; return job->state; }
    if (!job->primed) {
        sample_row(job, job->top, job->row_y, sample, sample_user);
        job->primed = true;
    }
    bottom = (int32_t)job->rect.y + job->rect.height - 1;
    next = (int32_t)job->row_y + job->step;
    if (next > bottom) next = bottom;
    sample_row(job, job->top ^ 1, (int16_t)next, sample, sample_user);
    for (column = 0; column < job->columns - 1; column++)
        cell(job, column, (int16_t)next, sample, sample_user, segment, segment_user);
    job->top ^= 1;
    job->row_y = (int16_t)next;
    if (next == bottom) job->state = PLOT2D_DONE;
    return job->state;
}
