#ifndef DGCALC_TEST_KEYPADC_H
#define DGCALC_TEST_KEYPADC_H

#include <stdint.h>

typedef uint16_t kb_lkey_t;

typedef enum {
    MODE_0_IDLE = 0,
    MODE_1_INDISCRIMINATE,
    MODE_2_SINGLE,
    MODE_3_CONTINUOUS
} kb_scan_mode_t;

extern uint8_t kb_Data[8];
extern uint8_t fake_kb_mode;
void kb_Scan(void);
void kb_Reset(void);
void fake_kb_set_mode(uint8_t mode);

#define kb_SetMode(mode) fake_kb_set_mode(mode)
#define kb_GetMode() fake_kb_mode

#define TEST_KEY(group, bit) ((kb_lkey_t)(((group) << 8) | (bit)))
#define kb_KeyTrace TEST_KEY(1, 1 << 1)
#define kb_Key0 TEST_KEY(3, 1 << 0)
#define kb_Key1 TEST_KEY(3, 1 << 1)
#define kb_Key2 TEST_KEY(4, 1 << 1)
#define kb_Key3 TEST_KEY(5, 1 << 1)
#define kb_Key4 TEST_KEY(3, 1 << 2)
#define kb_Key5 TEST_KEY(4, 1 << 2)
#define kb_Key6 TEST_KEY(5, 1 << 2)
#define kb_Key7 TEST_KEY(3, 1 << 3)
#define kb_Key8 TEST_KEY(4, 1 << 3)
#define kb_Key9 TEST_KEY(5, 1 << 3)
#define kb_KeyGraph TEST_KEY(1, 1 << 0)
#define kb_KeyZoom TEST_KEY(1, 1 << 2)
#define kb_KeyWindow TEST_KEY(1, 1 << 3)
#define kb_KeyYequ TEST_KEY(1, 1 << 4)
#define kb_KeyDel TEST_KEY(1, 1 << 7)
#define kb_KeySto TEST_KEY(2, 1 << 1)
#define kb_KeyLn TEST_KEY(2, 1 << 2)
#define kb_KeyLog TEST_KEY(2, 1 << 3)
#define kb_KeySquare TEST_KEY(2, 1 << 4)
#define kb_KeyMath TEST_KEY(2, 1 << 6)
#define kb_KeyAlpha TEST_KEY(2, 1 << 7)
#define kb_KeyComma TEST_KEY(3, 1 << 4)
#define kb_KeySin TEST_KEY(3, 1 << 5)
#define kb_KeyApps TEST_KEY(3, 1 << 6)
#define kb_KeyGraphVar TEST_KEY(3, 1 << 7)
#define kb_KeyDecPnt TEST_KEY(4, 1 << 0)
#define kb_KeyLParen TEST_KEY(4, 1 << 4)
#define kb_KeyCos TEST_KEY(4, 1 << 5)
#define kb_KeyPrgm TEST_KEY(4, 1 << 6)
#define kb_KeyChs TEST_KEY(5, 1 << 0)
#define kb_KeyRParen TEST_KEY(5, 1 << 4)
#define kb_KeyTan TEST_KEY(5, 1 << 5)
#define kb_KeyVars TEST_KEY(5, 1 << 6)
#define kb_KeyAdd TEST_KEY(6, 1 << 1)
#define kb_KeySub TEST_KEY(6, 1 << 2)
#define kb_KeyMul TEST_KEY(6, 1 << 3)
#define kb_KeyDiv TEST_KEY(6, 1 << 4)
#define kb_KeyPower TEST_KEY(6, 1 << 5)
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
