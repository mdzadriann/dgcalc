#include "test.h"
#include "graph/plot2d.h"

#include <float.h>
#include <limits.h>
#include <stdlib.h>

typedef struct { int16_t x0, y0, x1, y1; } Segment;
typedef struct { Segment segments[4096]; unsigned count; GraphRect rect; } Capture;
typedef struct {
    int kind;
    float sign, band;
    unsigned calls, centers;
    int failure;
} Field;

static bool field_sample(void *user, float x, float y, float *value) {
    Field *f = user;
    float v;
    f->calls++;
    switch (f->kind) {
        case 0: v = y - x; break;
        case 1: v = y - 0.3f; break;
        case 2: v = x - 0.3f; break;
        case 3: v = x*x + y*y - 25; break;
        case 4: v = 9*x*x + 4*y*y - 36; break;
        case 5: v = 1; break;
        case 6: v = 0; break;
        case 7:
            if (fabsf(x) < 0.6f) {
                *value = f->failure == 1 ? NAN : INFINITY;
                return f->failure != 0;
            }
            v = y - 0.3f; break;
        case 8:
            if (fabsf(y) < 0.6f) { *value = NAN; return f->failure != 0; }
            v = x - 0.3f; break;
        case 9:
            if (x == 0) return false;
            v = y - 1/x; break;
        case 10: v = 100*x + y - 0.01f; break;
        case 11: v = x; break;
        case 12: v = y; break;
        default: v = 1; break;
    }
    *value = v * f->sign;
    return true;
}

static void capture_segment(void *user, int16_t x0, int16_t y0,
                            int16_t x1, int16_t y1) {
    Capture *c = user;
    ASSERT_TRUE(c->count < 4096);
    ASSERT_TRUE(x0 >= c->rect.x && x0 < c->rect.x + c->rect.width);
    ASSERT_TRUE(x1 >= c->rect.x && x1 < c->rect.x + c->rect.width);
    ASSERT_TRUE(y0 >= c->rect.y && y0 < c->rect.y + c->rect.height);
    ASSERT_TRUE(y1 >= c->rect.y && y1 < c->rect.y + c->rect.height);
    ASSERT_TRUE(x0 != x1 || y0 != y1);
    if (c->count < 4096) c->segments[c->count++] = (Segment){x0,y0,x1,y1};
}

static unsigned run_job(Plot2DJob *job, Plot2DSampleFn sample, void *field,
                        Capture *c) {
    unsigned steps = 0;
    Plot2DStep result;
    do {
        result = plot2d_step(job, sample, field, capture_segment, c);
        steps++;
        ASSERT_TRUE(result == PLOT2D_PROGRESS || result == PLOT2D_DONE);
    } while (result == PLOT2D_PROGRESS && steps < 9000);
    ASSERT_EQ_INT(PLOT2D_DONE, result);
    return steps;
}

static Capture plot(Field *field, Viewport2D v, GraphRect r, uint8_t step) {
    Plot2DJob job;
    Capture c = { .rect = r };
    ASSERT_TRUE(plot2d_begin(&job, v, r, step));
    run_job(&job, field_sample, field, &c);
    return c;
}

static int compare_segment(const void *a, const void *b) {
    const Segment *s = a, *t = b;
    if (s->x0 != t->x0) return s->x0 - t->x0;
    if (s->y0 != t->y0) return s->y0 - t->y0;
    if (s->x1 != t->x1) return s->x1 - t->x1;
    return s->y1 - t->y1;
}

static void canonicalize(Capture *c) {
    unsigned i;
    for (i = 0; i < c->count; i++) {
        Segment *s = &c->segments[i];
        if (s->x0 > s->x1 || (s->x0 == s->x1 && s->y0 > s->y1)) {
            int16_t x = s->x0, y = s->y0;
            s->x0 = s->x1; s->y0 = s->y1; s->x1 = x; s->y1 = y;
        }
    }
    qsort(c->segments, c->count, sizeof(Segment), compare_segment);
}

static void test_geometry_and_sign_invariance(void) {
    Viewport2D v = {0,0,0.0625f};
    GraphRect rects[] = {{0,20,320,196}, {210,31,103,133}};
    unsigned r, kind, step, i;
    for (r = 0; r < 2; r++) for (step = 4; step <= 8; step += 4) {
        for (kind = 0; kind <= 6; kind++) {
            Field f = {.kind = (int)kind, .sign = 1};
            Capture c = plot(&f, v, rects[r], (uint8_t)step), reversed;
            f.sign = -1;
            reversed = plot(&f, v, rects[r], (uint8_t)step);
            if (kind >= 5) ASSERT_EQ_INT(0, c.count);
            else ASSERT_TRUE(c.count > 0);
            canonicalize(&c); canonicalize(&reversed);
            ASSERT_EQ_INT(c.count, reversed.count);
            ASSERT_TRUE(memcmp(c.segments, reversed.segments,
                               c.count * sizeof(Segment)) == 0);
            for (i = 0; i < c.count; i++) {
                Segment s = c.segments[i];
                float x, y;
                viewport2d_screen_to_world(&v, rects[r],
                    (s.x0+s.x1)*0.5f, (s.y0+s.y1)*0.5f, &x, &y);
                if (kind == 0) ASSERT_TRUE(fabsf(y-x) <= 0.065f);
                if (kind == 1) ASSERT_TRUE(fabsf(y-0.3f) <= 0.032f);
                if (kind == 2) ASSERT_TRUE(fabsf(x-0.3f) <= 0.032f);
                if (kind == 3) ASSERT_TRUE(fabsf(sqrtf(x*x+y*y)-5) <= 0.075f);
                if (kind == 4) ASSERT_TRUE(fabsf(9*x*x+4*y*y-36) <= 2.1f);
            }
            if (kind == 1 || kind == 2) {
                int min = INT_MAX, max = INT_MIN;
                for (i=0; i<c.count; i++) {
                    Segment s = c.segments[i];
                    int a = kind == 1 ? s.x0 : s.y0;
                    int b = kind == 1 ? s.x1 : s.y1;
                    if (a < min) min = a;
                    if (b < min) min = b;
                    if (a > max) max = a;
                    if (b > max) max = b;
                }
                ASSERT_EQ_INT(kind == 1 ? rects[r].x : rects[r].y, min);
                ASSERT_EQ_INT(kind == 1 ? rects[r].x+rects[r].width-1 :
                                         rects[r].y+rects[r].height-1, max);
            }
            if (kind == 0 && r == 0) {
                int min = INT_MAX, max = INT_MIN;
                for (i=0; i<c.count; i++) {
                    Segment s=c.segments[i];
                    if(s.y0<min) min=s.y0;
                    if(s.y1<min) min=s.y1;
                    if(s.y0>max) max=s.y0;
                    if(s.y1>max) max=s.y1;
                }
                ASSERT_EQ_INT(20,min); ASSERT_EQ_INT(215,max);
            }
        }
    }
}

typedef struct { unsigned calls; float x[6], y[6]; unsigned seen[6][6]; } Grid;
static bool grid_sample(void *user, float x, float y, float *value) {
    Grid *g = user;
    unsigned i, j;
    for (i=0; i<6 && fabsf(g->x[i]-x)>0.001f; i++) {}
    for (j=0; j<6 && fabsf(g->y[j]-y)>0.001f; j++) {}
    ASSERT_TRUE(i<6 && j<6);
    if (i<6 && j<6) g->seen[j][i]++;
    g->calls++; *value = 1; return true;
}

static void test_grid_rows_and_validation(void) {
    Plot2DJob job = {0};
    Viewport2D v = {0,0,1};
    GraphRect r = {10,20,20,18};
    Capture c = {.rect = r};
    Grid g = {.x={-10,-6,-2,2,6,9}, .y={9,5,1,-3,-7,-8}};
    unsigned i,j;
    ASSERT_EQ_INT(PLOT2D_ERROR, plot2d_step(&job,grid_sample,&g,capture_segment,&c));
    ASSERT_TRUE(plot2d_begin(&job,v,r,4));
    for (j=0; j<5; j++) {
        ASSERT_EQ_INT(j==4 ? PLOT2D_DONE : PLOT2D_PROGRESS,
                      plot2d_step(&job,grid_sample,&g,capture_segment,&c));
        ASSERT_EQ_INT((j+2)*6,g.calls);
    }
    for (i=0;i<6;i++) for(j=0;j<6;j++) ASSERT_EQ_INT(1,g.seen[j][i]);
    ASSERT_EQ_INT(PLOT2D_DONE,plot2d_step(&job,NULL,NULL,NULL,NULL));
    ASSERT_EQ_INT(36,g.calls); ASSERT_EQ_INT(0,c.count);
    {
        Grid coarse={.x={-10,-2,6,9,100,100},.y={9,1,-7,-8,100,100}};
        ASSERT_TRUE(plot2d_begin(&job,v,r,8));
        ASSERT_EQ_INT(3,run_job(&job,grid_sample,&coarse,&c));
        ASSERT_EQ_INT(16,coarse.calls);
        for(i=0;i<4;i++) for(j=0;j<4;j++) ASSERT_EQ_INT(1,coarse.seen[j][i]);
    }
    ASSERT_FALSE(plot2d_begin(NULL,v,r,4));
    ASSERT_FALSE(plot2d_begin(&job,v,r,3));
    ASSERT_FALSE(plot2d_begin(&job,v,(GraphRect){0,0,1,2},4));
    ASSERT_FALSE(plot2d_begin(&job,v,(GraphRect){0,0,2,-1},4));
    ASSERT_FALSE(plot2d_begin(&job,v,(GraphRect){INT16_MAX,0,2,2},4));
    ASSERT_FALSE(plot2d_begin(&job,v,(GraphRect){0,INT16_MAX,2,2},4));
    ASSERT_FALSE(plot2d_begin(&job,(Viewport2D){NAN,0,1},r,4));
    ASSERT_FALSE(plot2d_begin(&job,(Viewport2D){0,INFINITY,1},r,4));
    ASSERT_FALSE(plot2d_begin(&job,(Viewport2D){0,0,0},r,4));
    ASSERT_FALSE(plot2d_begin(&job,(Viewport2D){0,0,-1},r,4));
    ASSERT_FALSE(plot2d_begin(&job,(Viewport2D){0,0,INFINITY},r,4));
    ASSERT_FALSE(plot2d_begin(&job,(Viewport2D){0,0,FLT_MAX},r,4));
    ASSERT_TRUE(plot2d_begin(&job,v,(GraphRect){0,0,320,196},4));
    ASSERT_TRUE(plot2d_begin(&job,v,(GraphRect){0,0,321,196},4));
    ASSERT_FALSE(plot2d_begin(&job,v,(GraphRect){0,0,322,196},4));
    ASSERT_EQ_INT(PLOT2D_ERROR,plot2d_step(&job,grid_sample,&g,capture_segment,&c));
    ASSERT_EQ_INT(PLOT2D_ERROR,plot2d_step(NULL,grid_sample,&g,capture_segment,&c));
    ASSERT_TRUE(plot2d_begin(&job,v,r,4));
    ASSERT_EQ_INT(PLOT2D_ERROR,plot2d_step(&job,NULL,&g,capture_segment,&c));
    ASSERT_TRUE(plot2d_begin(&job,v,r,4));
    ASSERT_EQ_INT(PLOT2D_ERROR,plot2d_step(&job,grid_sample,&g,NULL,&c));
    for (i=4;i<=8;i+=4) {
        Field f = {.kind=5,.sign=1};
        r=(GraphRect){0,20,320,196}; c=(Capture){.rect=r};
        ASSERT_TRUE(plot2d_begin(&job,v,r,(uint8_t)i));
        ASSERT_EQ_INT(i==4 ? 49 : 25, run_job(&job,field_sample,&f,&c));
        ASSERT_EQ_INT(i==4 ? 4050 : 1066,f.calls);
        printf("plot2d step %u: %u grid samples\n",i,f.calls);
    }
}

/* Bilinear saddle plus a center bump: boundary roots stay at edge centers. */
static bool saddle_sample(void *user,float x,float y,float *value) {
    Field *f=user;
    float bump=(1-x*x)*(1-y*y);
    f->calls++;
    if (f->failure>=3 && x==-0.5f && y==0.5f) {
        *value=NAN; return f->failure==4;
    }
    if (x==0 && y==0) {
        f->centers++;
        if(f->failure==1 || f->failure==2) { *value=NAN; return f->failure==2; }
    }
    *value=f->sign*(x*y+f->band*bump);
    return true;
}

static void test_ambiguity_center_decider(void) {
    Viewport2D v={0.25f,-0.25f,0.5f};
    GraphRect r={10,20,5,5};
    int sign,center;
    for(sign=-1;sign<=1;sign+=2) for(center=-1;center<=1;center+=2) {
        Plot2DJob job;
        Field f={.sign=(float)sign,.band=(float)center};
        Capture c={.rect=r};
        ASSERT_TRUE(plot2d_begin(&job,v,r,4));
        run_job(&job,saddle_sample,&f,&c);
        ASSERT_EQ_INT(1,f.centers); ASSERT_EQ_INT(2,c.count);
        canonicalize(&c);
        ASSERT_EQ_INT(10,c.segments[0].x0); ASSERT_EQ_INT(22,c.segments[0].y0);
        ASSERT_EQ_INT(12,c.segments[0].x1);
        ASSERT_EQ_INT(center>0 ? 20 : 24,c.segments[0].y1);
    }
    for(center=1;center<=4;center++) {
        Plot2DJob job; Field f={.sign=1,.band=1,.failure=center};
        Capture c={.rect=r};
        ASSERT_TRUE(plot2d_begin(&job,v,r,4));
        run_job(&job,saddle_sample,&f,&c); ASSERT_EQ_INT(center>=3 ? 1 : 0,c.count);
    }
}

static void test_invalid_bands_and_poles(void) {
    Viewport2D v={0,0,0.25f}; GraphRect r={7,11,81,65};
    unsigned i; int kind,failure;
    for(kind=7;kind<=8;kind++) for(failure=0;failure<=2;failure++) {
        Field f={.kind=kind,.sign=1,.failure=failure}; Capture c=plot(&f,v,r,4);
        ASSERT_TRUE(c.count>4);
        for(i=0;i<c.count;i++) {
            float x0,y0,x1,y1; Segment s=c.segments[i];
            viewport2d_screen_to_world(&v,r,s.x0,s.y0,&x0,&y0);
            viewport2d_screen_to_world(&v,r,s.x1,s.y1,&x1,&y1);
            ASSERT_TRUE(kind==7 ? (x0<=-0.6f && x1<=-0.6f)||(x0>=0.6f && x1>=0.6f) :
                                  (y0<=-0.6f && y1<=-0.6f)||(y0>=0.6f && y1>=0.6f));
        }
    }
    for(kind=0;kind<3;kind++) {
        Field f={.kind=9,.sign=1}; Capture c;
        v.center_x=kind==0 ? 0.125f : kind==1 ? 0.25f : 0.375f;
        c=plot(&f,v,r,4); ASSERT_TRUE(c.count>4);
        for(i=0;i<c.count;i++) {
            float x0,y0,x1,y1; Segment s=c.segments[i];
            viewport2d_screen_to_world(&v,r,s.x0,s.y0,&x0,&y0);
            viewport2d_screen_to_world(&v,r,s.x1,s.y1,&x1,&y1);
            ASSERT_TRUE(x0*x1>0);
        }
    }
    {
        Field f={.kind=10,.sign=1};
        Capture c=plot(&f,(Viewport2D){2.5f,-2,1},(GraphRect){0,0,5,5},4);
        ASSERT_EQ_INT(1,c.count);
        ASSERT_TRUE(abs(c.segments[0].y1-c.segments[0].y0)>=3);
    }
}

typedef struct { unsigned calls; int mode; } MidpointFailure;
static bool midpoint_failure(void *user,float x,float y,float *value) {
    MidpointFailure *f=user; f->calls++;
    if ((f->mode<3 && x==0) || (f->mode>=3 && x==0 && y==3)) {
        *value=f->mode==1 || f->mode==4 ? NAN :
               f->mode==2 || f->mode==5 ? INFINITY : 100;
        return f->mode!=0 && f->mode!=6;
    }
    *value=x;
    return true;
}

static void test_candidate_midpoint_failures(void) {
    unsigned mode;
    for(mode=0;mode<7;mode++) {
        Plot2DJob job; MidpointFailure f={.mode=(int)mode};
        Capture c={.rect={0,0,5,9}};
        ASSERT_TRUE(plot2d_begin(&job,(Viewport2D){-0.5f,0.5f,1},c.rect,4));
        run_job(&job,midpoint_failure,&f,&c);
        ASSERT_EQ_INT(mode>=3 ? 1 : 0,c.count);
    }
}

static void test_exact_zero_edges_and_extreme_clipping(void) {
    int kind;
    for(kind=11;kind<=12;kind++) {
        Viewport2D v={0.5f,-0.5f,1}; GraphRect r={-32768,-32768,9,9};
        Field f={.kind=kind,.sign=1}; Capture c=plot(&f,v,r,4),d;
        ASSERT_TRUE(c.count>0);
        f.sign=-1; d=plot(&f,v,r,4); canonicalize(&c); canonicalize(&d);
        ASSERT_EQ_INT(c.count,d.count);
        ASSERT_TRUE(memcmp(c.segments,d.segments,c.count*sizeof(Segment))==0);
    }
}

static void test_pole_between_vertical_edge_crossings(void) {
    Viewport2D viewports[]={{-1,0,1},{1.5f,-0.5f,1}};
    GraphRect rects[]={{0,0,320,196},{0,0,5,5}};
    unsigned fixture,i; int sign;
    for(fixture=0;fixture<2;fixture++) for(sign=-1;sign<=1;sign+=2) {
        Field f={.kind=9,.sign=(float)sign};
        Capture c=plot(&f,viewports[fixture],rects[fixture],4);
        unsigned bridges=0;
        for(i=0;i<c.count;i++) {
            float x0,y0,x1,y1; Segment s=c.segments[i];
            viewport2d_screen_to_world(&viewports[fixture],rects[fixture],s.x0,s.y0,&x0,&y0);
            viewport2d_screen_to_world(&viewports[fixture],rects[fixture],s.x1,s.y1,&x1,&y1);
            if(x0*x1<0) bridges++;
        }
        printf("pole vertical-edge fixture %u sign %d: %u segments, %u bridges\n",
               fixture,sign,c.count,bridges);
        ASSERT_EQ_INT(0,bridges);
        if(fixture==0) ASSERT_TRUE(c.count>0);
        else ASSERT_EQ_INT(0,c.count);
    }
}

static void test_zero_edge_uniqueness_and_boundary_ownership(void) {
    int kind,position,sign;
    for(kind=11;kind<=12;kind++) for(position=0;position<3;position++) {
        for(sign=-1;sign<=1;sign+=2) {
            Viewport2D v={0.5f,-0.5f,1}; GraphRect r={0,0,9,9};
            Field f={.kind=kind,.sign=(float)sign}; Capture c;
            unsigned i; int min=INT_MAX,max=INT_MIN;
            int fixed=position==0 ? 4 : position==1 ? 0 : 8;
            if(kind==11) v.center_x=position==0 ? 0.5f : position==1 ? 4.5f : -3.5f;
            else v.center_y=position==0 ? -0.5f : position==1 ? -4.5f : 3.5f;
            c=plot(&f,v,r,4); canonicalize(&c);
            ASSERT_EQ_INT(2,c.count);
            for(i=0;i<c.count;i++) {
                Segment s=c.segments[i];
                int a=kind==11 ? s.y0 : s.x0, b=kind==11 ? s.y1 : s.x1;
                ASSERT_EQ_INT(fixed,kind==11 ? s.x0 : s.y0);
                ASSERT_EQ_INT(fixed,kind==11 ? s.x1 : s.y1);
                if(i>0) ASSERT_TRUE(compare_segment(&c.segments[i-1],&s)!=0);
                if(a<min) min=a;
                if(b<min) min=b;
                if(a>max) max=a;
                if(b>max) max=b;
            }
            ASSERT_EQ_INT(0,min); ASSERT_EQ_INT(8,max);
        }
    }
}

void run_plot2d_tests(void) {
    printf("sizeof(Plot2DJob) host: %zu\n",sizeof(Plot2DJob));
    test_geometry_and_sign_invariance();
    test_grid_rows_and_validation();
    test_ambiguity_center_decider();
    test_invalid_bands_and_poles();
    test_candidate_midpoint_failures();
    test_exact_zero_edges_and_extreme_clipping();
    test_pole_between_vertical_edge_crossings();
    test_zero_edge_uniqueness_and_boundary_ownership();
}
