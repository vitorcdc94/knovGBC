/* Compile-only probe for the target compiler's PicoGB register layout. */

#include "microcode.h"

#if PICOGB_LITTLE_ENDIAN
typedef char assert_b_offset[( __builtin_offsetof(struct mb_state, reg.B) == 1) ? 1 : -1];
typedef char assert_c_offset[( __builtin_offsetof(struct mb_state, reg.C) == 0) ? 1 : -1];
typedef char assert_a_offset[( __builtin_offsetof(struct mb_state, reg.A) == 6) ? 1 : -1];
typedef char assert_f_offset[( __builtin_offsetof(struct mb_state, reg.F) == 7) ? 1 : -1];
#else
typedef char assert_b_offset[( __builtin_offsetof(struct mb_state, reg.B) == 0) ? 1 : -1];
typedef char assert_c_offset[( __builtin_offsetof(struct mb_state, reg.C) == 1) ? 1 : -1];
typedef char assert_a_offset[( __builtin_offsetof(struct mb_state, reg.A) == 7) ? 1 : -1];
typedef char assert_f_offset[( __builtin_offsetof(struct mb_state, reg.F) == 6) ? 1 : -1];
#endif

int picogb_endian_probe(void)
{
    struct mb_state mb = {0};

    mb.reg.BC = 0x1234;
    mb.reg.DE = 0x5678;
    mb.reg.HL = 0x9ABC;
    mb.reg.FA = 0xB001;

    return mb.reg.B == 0x12 && mb.reg.C == 0x34 &&
           mb.reg.D == 0x56 && mb.reg.E == 0x78 &&
           mb.reg.H == 0x9A && mb.reg.L == 0xBC &&
           mb.reg.F == 0xB0 && mb.reg.A == 0x01 &&
           MB_REG8(&mb, 1) == 0x12 && MB_REG8(&mb, 0) == 0x34 &&
           MB_REG8(&mb, 6) == 0x01;
}
