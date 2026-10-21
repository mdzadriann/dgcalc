#ifndef DGCALC_FAKE_TIME_H
#define DGCALC_FAKE_TIME_H
#include <stdint.h>
#define CLOCKS_PER_SEC 32768UL
typedef uint32_t clock_t;
clock_t fake_clock(void);
#define clock fake_clock
#endif
