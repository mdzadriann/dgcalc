#include "graph/project_plot2d.h"
#include "test.h"
#include "project_memory.h"

#include <float.h>
#include <limits.h>

/* Observe real calls without adding instrumentation to the production API. */
static unsigned row_calls, sample_calls, sampled[PROJECT_MAX_EXPRESSIONS];
static unsigned sample_errors[PROJECT_MAX_EXPRESSIONS];
static Plot2DStep observed_plot_step(Plot2DJob *job, Plot2DSampleFn sample,
                                    void *sample_user, Plot2DSegmentFn segment,
                                    void *segment_user) {
    row_calls++;
    return plot2d_step(job, sample, sample_user, segment, segment_user);
}
static EvalResult observed_evaluate(const Project *project, uint8_t index,
                                    float x, float y) {
    EvalResult result = project_evaluate_2d(project, index, x, y);
    sample_calls++;
    ASSERT_TRUE(index < PROJECT_MAX_EXPRESSIONS);
    if (index < PROJECT_MAX_EXPRESSIONS) {
        sampled[index]++;
        if (result.error != EVAL_OK) sample_errors[index]++;
    }
    return result;
}
#define plot2d_step observed_plot_step
#define project_evaluate_2d observed_evaluate
#define project_plot2d_begin observed_begin
#define project_plot2d_step observed_step
#define project_plot2d_cancel observed_cancel
#include "../src/graph/project_plot2d.c"
#undef plot2d_step
#undef project_evaluate_2d
#undef project_plot2d_begin
#undef project_plot2d_step
#undef project_plot2d_cancel

typedef struct {
    GraphRect rect;
    unsigned counts[PROJECT_MAX_EXPRESSIONS], total;
    uint8_t last;
} Capture;

static const Viewport2D viewport = {0, 0, 0.5f};
static const GraphRect rectangle = {3, 7, 33, 25};

static void commit(Project *project, uint8_t index, const char *source) {
    ASSERT_TRUE(project_set_draft(project, index, source));
    ASSERT_TRUE(project_commit_line(project, index).ok);
}

static void capture(void *user, uint8_t index, int16_t x0, int16_t y0,
                    int16_t x1, int16_t y1) {
    Capture *c = user;
    ASSERT_TRUE(index < PROJECT_MAX_EXPRESSIONS);
    ASSERT_TRUE(c->total == 0 || index >= c->last);
    ASSERT_TRUE(x0 >= c->rect.x && x0 < c->rect.x + c->rect.width);
    ASSERT_TRUE(x1 >= c->rect.x && x1 < c->rect.x + c->rect.width);
    ASSERT_TRUE(y0 >= c->rect.y && y0 < c->rect.y + c->rect.height);
    ASSERT_TRUE(y1 >= c->rect.y && y1 < c->rect.y + c->rect.height);
    ASSERT_TRUE(x0 != x1 || y0 != y1);
    if (index < PROJECT_MAX_EXPRESSIONS) c->counts[index]++;
    c->total++;
    c->last = index;
}

static unsigned render(ProjectPlot2D *job, const Project *project, Capture *c) {
    Project before;
    unsigned steps = 0, calls = memory_calls, i;
    size_t bytes = project_payload_bytes(project);
    unsigned char payloads[8192];
    size_t offsets[32], used = 0;
    ProjectPlot2DStep result;
    memcpy(&before, project, sizeof(before));
    for (i = 0; i < 32; i++) {
        offsets[i] = used;
        if (!memory_pointers[i]) continue;
        ASSERT_TRUE(used + memory_sizes[i] <= sizeof(payloads));
        if (used + memory_sizes[i] > sizeof(payloads)) return 0;
        memcpy(payloads + used, memory_pointers[i], memory_sizes[i]);
        used += memory_sizes[i];
    }
    do {
        result = project_plot2d_step(job, project, capture, c);
        steps++;
        ASSERT_TRUE(result == PROJECT_PLOT2D_PROGRESS || result == PROJECT_PLOT2D_DONE);
        ASSERT_EQ_INT(bytes, project_payload_bytes(project));
        ASSERT_EQ_INT(calls, memory_calls);
        ASSERT_TRUE(memcmp(&before, project, sizeof(before)) == 0);
        for (i = 0; i < 32; i++) if (memory_pointers[i]) {
            ASSERT_TRUE(memcmp(payloads + offsets[i], memory_pointers[i], memory_sizes[i]) == 0);
        }
    } while (result == PROJECT_PLOT2D_PROGRESS && steps < 200);
    ASSERT_EQ_INT(PROJECT_PLOT2D_DONE, result);
    ASSERT_EQ_INT(PROJECT_PLOT2D_DONE, project_plot2d_step(job, NULL, NULL, NULL));
    return steps;
}

static void observe_render(ProjectPlot2D *job, const Project *project, Capture *c) {
    unsigned calls = 0;
    ProjectPlot2DStep result;
    row_calls = sample_calls = 0;
    memset(sampled, 0, sizeof(sampled));
    memset(sample_errors, 0, sizeof(sample_errors));
    observed_begin(job, viewport, c->rect, 4);
    do {
        unsigned before = row_calls;
        result = observed_step(job, project, capture, c);
        ASSERT_TRUE(row_calls - before <= 1);
        calls++;
    } while (result == PROJECT_PLOT2D_PROGRESS && calls < 200);
    ASSERT_EQ_INT(PROJECT_PLOT2D_DONE, result);
}

static void test_mixed_slots_render_retained_equation_with_invalid_draft_visible(void) {
    Project project;
    ProjectPlot2D job;
    Capture c = {.rect = rectangle};
    unsigned i;
    project_init_with_memory(&project, counting_memory());
    commit(&project, 0, "a=2");
    commit(&project, 2, "y=x+a");
    commit(&project, 3, "y=q");
    commit(&project, 4, "b=c");
    commit(&project, 5, "c=b");
    commit(&project, 6, "y=b");
    commit(&project, 7, "y<x");
    commit(&project, 8, "2+3");
    commit(&project, 9, "x+y+z=1");
    commit(&project, 10, "f(t)=t^2");
    commit(&project, 11, "x=2");
    commit(&project, 14, "y=x");
    ASSERT_TRUE(project_set_draft(&project, 14, "y="));
    ASSERT_FALSE(project_validate_draft(&project, 14).ok);
    ASSERT_TRUE(project_line_has_committed(&project, 14));
    ASSERT_EQ_INT(EXPRESSION_INVALID, project_line_status(&project, 14));
    ASSERT_STR_EQ("y=", project_line_display_text(&project, 14));
    ASSERT_TRUE(project_line_is_drawable_2d(&project, 14));
    project_plot2d_begin(&job, viewport, rectangle, 4);
    ASSERT_EQ_INT(19, render(&job, &project, &c));
    for (i = 0; i < PROJECT_MAX_EXPRESSIONS; i++) {
        if (i == 2 || i == 11 || i == 14) ASSERT_TRUE(c.counts[i] > 0);
        else ASSERT_EQ_INT(0, c.counts[i]);
    }
    c = (Capture){.rect = rectangle};
    observe_render(&job, &project, &c);
    ASSERT_EQ_INT(18, row_calls);
    for (i = 0; i < PROJECT_MAX_EXPRESSIONS; i++) {
        if (i == 2 || i == 11 || i == 14) ASSERT_TRUE(sampled[i] > 0);
        else ASSERT_EQ_INT(0, sampled[i]);
    }
    project_dispose(&project);
    ASSERT_EQ_INT(0, memory_bytes);
}

static void test_zero_five_and_sixteen_lines_reuse_fixed_memory(void) {
    static const char *const typical[] = {
        "y=x", "x=2", "x=y^2", "x^2+y^2=25", "9x^2+4y^2=36"
    };
    const unsigned counts[] = {0, 5, 16};
    Project project;
    ProjectPlot2D job;
    unsigned fixture, i, step;
    ASSERT_EQ_INT(sizeof(Plot2DJob), sizeof(job.job));
    ASSERT_TRUE(sizeof(ProjectPlot2D) <= sizeof(Plot2DJob) + 16);
    printf("sizeof(ProjectPlot2D) host: %zu; embedded Plot2DJob: %zu\n",
           sizeof(job), sizeof(job.job));
    for (fixture = 0; fixture < 3; fixture++) {
        size_t bytes;
        project_init_with_memory(&project, counting_memory());
        for (i = 0; i < counts[fixture]; i++) {
            commit(&project, (uint8_t)i, counts[fixture] == 5 ? typical[i] : "y=x");
        }
        bytes = project_payload_bytes(&project);
        for (step = 8; step >= 4; step -= 4) {
            Capture c = {.rect = rectangle};
            unsigned steps;
            project_plot2d_begin(&job, viewport, rectangle, (uint8_t)step);
            steps = render(&job, &project, &c);
            ASSERT_EQ_INT(counts[fixture] * (24 / step) + (counts[fixture] < 16), steps);
            for (i = 0; i < counts[fixture]; i++) ASSERT_TRUE(c.counts[i] > 0);
            ASSERT_EQ_INT(counts[fixture], memory_live);
            printf("scheduler %u lines step %u: %u calls, %u segments, payload %zu unchanged\n",
                   counts[fixture], step, steps, c.total, bytes);
        }
        for (i = 0; i < counts[fixture]; i++) ASSERT_TRUE(project_clear_line(&project, (uint8_t)i));
        ASSERT_EQ_INT(0, project_payload_bytes(&project));
        ASSERT_EQ_INT(0, memory_live);
        project_dispose(&project);
        ASSERT_EQ_INT(0, memory_bytes);
    }
}

static void test_one_underlying_row_per_call_and_no_sampling_skipped_slots(void) {
    Project project;
    ProjectPlot2D job;
    Capture c = {.rect = {0, 0, 9, 9}};
    unsigned call, i;
    project_init_with_memory(&project, counting_memory());
    commit(&project, 0, "a=2");
    commit(&project, 2, "y=x+100");
    commit(&project, 4, "y<x");
    commit(&project, 6, "y=x+100");
    row_calls = sample_calls = 0;
    memset(sampled, 0, sizeof(sampled));
    observed_begin(&job, viewport, c.rect, 4);
    for (call = 0; call < 4; call++) {
        ASSERT_EQ_INT(PROJECT_PLOT2D_PROGRESS, observed_step(&job, &project, capture, &c));
        ASSERT_EQ_INT(call + 1, row_calls);
        /* No crossings: first row samples 2x3 vertices, later rows sample 3. */
        ASSERT_EQ_INT(call == 0 ? 6 : call == 1 ? 9 : call == 2 ? 15 : 18, sample_calls);
        ASSERT_EQ_INT(call < 2 ? (call == 0 ? 6 : 9) : 9, sampled[2]);
        ASSERT_EQ_INT(call < 2 ? 0 : call == 2 ? 6 : 9, sampled[6]);
    }
    ASSERT_EQ_INT(PROJECT_PLOT2D_DONE, observed_step(&job, &project, capture, &c));
    for (i = 0; i < 16; i++) if (i != 2 && i != 6) ASSERT_EQ_INT(0, sampled[i]);
    ASSERT_EQ_INT(0, c.total);
    ASSERT_EQ_INT(4, row_calls);
    ASSERT_EQ_INT(18, sample_calls);
    ASSERT_EQ_INT(PROJECT_PLOT2D_DONE, observed_step(&job, NULL, NULL, NULL));
    observed_cancel(&job);
    ASSERT_EQ_INT(PROJECT_PLOT2D_DONE, observed_step(&job, &project, capture, &c));
    observed_begin(&job, viewport, c.rect, 4);
    ASSERT_EQ_INT(PROJECT_PLOT2D_PROGRESS, observed_step(&job, &project, capture, &c));
    observed_cancel(&job);
    ASSERT_EQ_INT(PROJECT_PLOT2D_DONE, observed_step(&job, &project, capture, &c));
    ASSERT_EQ_INT(5, row_calls);
    ASSERT_EQ_INT(24, sample_calls);
    observed_begin(&job, viewport, c.rect, 8);
    ASSERT_EQ_INT(PROJECT_PLOT2D_PROGRESS, observed_step(&job, &project, capture, &c));
    ASSERT_EQ_INT(6, row_calls);
    ASSERT_EQ_INT(28, sample_calls);
    observed_begin(&job, viewport, c.rect, 4);
    ASSERT_EQ_INT(PROJECT_PLOT2D_PROGRESS, observed_step(&job, &project, capture, &c));
    ASSERT_EQ_INT(7, row_calls);
    ASSERT_EQ_INT(34, sample_calls);
    project_dispose(&project);
    project_init_with_memory(&project, counting_memory());
    observed_begin(&job, viewport, c.rect, 4);
    ASSERT_EQ_INT(PROJECT_PLOT2D_DONE, observed_step(&job, &project, capture, &c));
    ASSERT_EQ_INT(7, row_calls);
    ASSERT_EQ_INT(34, sample_calls);
    ASSERT_EQ_INT(0, memory_calls);
    project_dispose(&project);
}

static void test_sample_errors_leave_gaps_and_continue_to_later_equations(void) {
    Project project;
    ProjectPlot2D job;
    Capture c = {.rect = {0, 0, 33, 33}};
    project_init_with_memory(&project, counting_memory());
    commit(&project, 0, "y=sqrt(x)");
    commit(&project, 1, "y=1/x");
    commit(&project, 2, "y=exp(x*1000)");
    commit(&project, 3, "y=x");
    ASSERT_EQ_INT(EVAL_ERR_DOMAIN, project_evaluate_2d(&project, 0, -1, 0).error);
    ASSERT_EQ_INT(EVAL_ERR_DIV_ZERO, project_evaluate_2d(&project, 1, 0, 0).error);
    ASSERT_EQ_INT(EVAL_ERR_NONFINITE, project_evaluate_2d(&project, 2, 1, 0).error);
    project_plot2d_begin(&job, viewport, c.rect, 4);
    ASSERT_EQ_INT(33, render(&job, &project, &c));
    ASSERT_TRUE(c.counts[0] > 0);
    ASSERT_TRUE(c.counts[1] > 0);
    ASSERT_TRUE(c.counts[3] > 0);
    c = (Capture){.rect = {0, 0, 33, 33}};
    observe_render(&job, &project, &c);
    ASSERT_EQ_INT(32, row_calls);
    ASSERT_TRUE(sample_errors[0] > 0);
    ASSERT_TRUE(sample_errors[1] > 0);
    ASSERT_TRUE(sample_errors[2] > 0);
    ASSERT_EQ_INT(0, sample_errors[3]);
    ASSERT_TRUE(c.counts[3] > 0);
    project_dispose(&project);
    ASSERT_EQ_INT(0, memory_bytes);
}

static void test_invalid_inputs_cancel_and_restart(void) {
    const Viewport2D bad_views[] = {
        {NAN, 0, 1}, {0, INFINITY, 1}, {0, 0, 0}, {0, 0, -1},
        {0, 0, INFINITY}, {0, 0, FLT_MAX}
    };
    const GraphRect bad_rects[] = {
        {0, 0, 1, 2}, {0, 0, 2, -1}, {INT16_MAX, 0, 2, 2},
        {0, INT16_MAX, 2, 2}, {0, 0, 322, 2}
    };
    Project project;
    ProjectPlot2D job = {0};
    Capture c = {.rect = rectangle};
    unsigned i;
    project_init_with_memory(&project, counting_memory());
    commit(&project, 0, "y=x");
    ASSERT_EQ_INT(PROJECT_PLOT2D_ERROR, project_plot2d_step(&job, &project, capture, &c));
    project_plot2d_cancel(NULL);
    project_plot2d_begin(NULL, viewport, rectangle, 4);
    ASSERT_EQ_INT(PROJECT_PLOT2D_ERROR, project_plot2d_step(NULL, &project, capture, &c));
    project_plot2d_cancel(&job);
    ASSERT_EQ_INT(PROJECT_PLOT2D_DONE, project_plot2d_step(&job, NULL, NULL, NULL));
    project_plot2d_begin(&job, viewport, rectangle, 4);
    c = (Capture){.rect = rectangle};
    ASSERT_EQ_INT(PROJECT_PLOT2D_PROGRESS, project_plot2d_step(&job, &project, capture, &c));
    {
        unsigned segments = c.total;
        Project before;
        memcpy(&before, &project, sizeof(before));
        ASSERT_TRUE(segments > 0);
        project_plot2d_cancel(&job);
        project_plot2d_cancel(&job);
        ASSERT_EQ_INT(PROJECT_PLOT2D_DONE, project_plot2d_step(&job, &project, capture, &c));
        ASSERT_EQ_INT(segments, c.total);
        ASSERT_TRUE(memcmp(&before, &project, sizeof(before)) == 0);
    }
    c = (Capture){.rect = rectangle};
    for (i = 0; i < sizeof(bad_views) / sizeof(bad_views[0]); i++) {
        project_plot2d_begin(&job, bad_views[i], rectangle, 4);
        ASSERT_EQ_INT(PROJECT_PLOT2D_ERROR, project_plot2d_step(&job, &project, capture, &c));
    }
    for (i = 0; i < sizeof(bad_rects) / sizeof(bad_rects[0]); i++) {
        project_plot2d_begin(&job, viewport, bad_rects[i], 4);
        ASSERT_EQ_INT(PROJECT_PLOT2D_ERROR, project_plot2d_step(&job, &project, capture, &c));
    }
    project_plot2d_begin(&job, viewport, rectangle, 3);
    ASSERT_EQ_INT(PROJECT_PLOT2D_ERROR, project_plot2d_step(&job, &project, capture, &c));
    project_plot2d_begin(&job, viewport, rectangle, 4);
    ASSERT_EQ_INT(PROJECT_PLOT2D_ERROR, project_plot2d_step(&job, NULL, capture, &c));
    ASSERT_EQ_INT(PROJECT_PLOT2D_ERROR, project_plot2d_step(&job, &project, capture, &c));
    project_plot2d_begin(&job, viewport, rectangle, 4);
    ASSERT_EQ_INT(PROJECT_PLOT2D_ERROR, project_plot2d_step(&job, &project, NULL, &c));
    ASSERT_EQ_INT(0, c.total);
    project_plot2d_begin(&job, viewport, rectangle, 4);
    ASSERT_EQ_INT(7, render(&job, &project, &c));
    ASSERT_TRUE(c.total > 0);
    project_plot2d_cancel(&job);
    ASSERT_EQ_INT(PROJECT_PLOT2D_DONE, project_plot2d_step(&job, NULL, NULL, NULL));
    project_dispose(&project);
    ASSERT_EQ_INT(0, memory_bytes);
}

void run_project_plot2d_tests(void) {
    test_mixed_slots_render_retained_equation_with_invalid_draft_visible();
    test_zero_five_and_sixteen_lines_reuse_fixed_memory();
    test_one_underlying_row_per_call_and_no_sampling_skipped_slots();
    test_sample_errors_leave_gaps_and_continue_to_later_equations();
    test_invalid_inputs_cancel_and_restart();
}
