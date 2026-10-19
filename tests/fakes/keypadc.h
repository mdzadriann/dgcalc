#ifndef DGCALC_TEST_KEYPADC_H
#define DGCALC_TEST_KEYPADC_H

#include <stdint.h>

typedef uint16_t kb_lkey_t;

extern uint8_t kb_Data[8];
void kb_Scan(void);

#define TEST_KEY(group, bit) ((kb_lkey_t)(((group) << 8) | (bit)))
#define kb_KeyEnter TEST_KEY(6, 1 << 0)
#define kb_KeyClear TEST_KEY(6, 1 << 6)
#define kb_KeyDown TEST_KEY(7, 1 << 0)
#define kb_KeyLeft TEST_KEY(7, 1 << 1)
#define kb_KeyRight TEST_KEY(7, 1 << 2)
#define kb_KeyUp TEST_KEY(7, 1 << 3)
#define kb_Key2nd TEST_KEY(1, 1 << 5)
#define kb_KeyMode TEST_KEY(1, 1 << 6)
#define kb_IsDown(key) (kb_Data[(key) >> 8] & (uint8_t)(key))

#endif
