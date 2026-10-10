/* knovGBC PicoGB library extension/snapshot; notice updated 2026-10-10.
 * This source extends/adapts the PicoGB library based on upstream commit
 * 325a12a25e2d1f4799424cc18c0e69a7810695b0.
 * The preserved local implementation predates the 2026-10-07 inventory.
 * Exact historical code-edit dates are not established by surviving records.
 * Only this source-handoff notice was changed on 2026-10-10.
 * See third_party/PicoGB/KNOVGBC_CHANGES.md for dated validation evidence;
 * report dates are not claims that every code change occurred on those dates.
 * Distributed with the modified library under GNU LGPL v2.1.
 * See third_party/PicoGB/LICENSE.txt for the library license.
 * Original upstream license and copyright notices remain applicable.
 */
#include "picogb_sm83_sh2.h"

#include "microcode_dispatch.h"

#include <stddef.h>

/*
 * The assembly source deliberately uses these offsets rather than C field
 * accesses.  Keep the contract checked by the compiler that builds the
 * Saturn binary, so a future PicoGB layout change fails loudly instead of
 * silently corrupting the emulated CPU state.
 */
#if defined(__sh__)
_Static_assert(offsetof(mb_state, reg.B) == 0U, "SM83 B offset");
_Static_assert(offsetof(mb_state, reg.C) == 1U, "SM83 C offset");
_Static_assert(offsetof(mb_state, reg.D) == 2U, "SM83 D offset");
_Static_assert(offsetof(mb_state, reg.E) == 3U, "SM83 E offset");
_Static_assert(offsetof(mb_state, reg.H) == 4U, "SM83 H offset");
_Static_assert(offsetof(mb_state, reg.L) == 5U, "SM83 L offset");
_Static_assert(offsetof(mb_state, reg.F) == 6U, "SM83 F offset");
_Static_assert(offsetof(mb_state, reg.A) == 7U, "SM83 A offset");
_Static_assert(offsetof(mb_state, IR) == 8U, "SM83 IR offset");
_Static_assert(offsetof(mb_state, PC) == 12U, "SM83 PC offset");
_Static_assert(offsetof(mb_state, SP) == 14U, "SM83 SP offset");
_Static_assert(offsetof(mb_state, IF) == 16U, "SM83 IF offset");
_Static_assert(offsetof(mb_state, IE) == 17U, "SM83 IE offset");
_Static_assert(offsetof(mb_state, IME) == 18U, "SM83 IME offset");
_Static_assert(offsetof(mb_state, IME_ASK) == 19U, "SM83 IME delay offset");
_Static_assert(offsetof(mb_state, HALTING) == 24U, "SM83 HALT offset");
_Static_assert(offsetof(mb_state, FR1) == 28U, "SM83 lazy flag offset");
_Static_assert(offsetof(mb_state, FR2) == 30U, "SM83 lazy flag offset");
_Static_assert(offsetof(mb_state, FMC_MODE) == 32U, "SM83 lazy flag mode");
_Static_assert(offsetof(mb_state, mi) == 36U, "SM83 memory pointer offset");
_Static_assert(sizeof(mb_state) == 236U, "SM83 state size");
#endif

#if defined(__sh__) && defined(KNOVGBC_SH2_SM83_ASM)
extern word picogb_sm83_exec_sh2(mb_state *mb);
#endif

/*
 * These helpers form the small, stable C ABI called by the assembly code.
 * Keeping memory and pipeline updates here preserves every existing mapper,
 * I/O, cache-invalidation and PC-wrap rule while the hot register-only
 * instructions move to SH-2 assembly.
 */
word __attribute__((noinline, used, externally_visible))
picogb_sm83_sh2_fallback(mb_state *mb)
{
    return mb_exec(mb);
}

word __attribute__((noinline, used, externally_visible))
picogb_sm83_sh2_fetch8(mb_state *mb)
{
    return mch_memory_fetch_PC_op_1(mb);
}

word __attribute__((noinline, used, externally_visible))
picogb_sm83_sh2_fetch16(mb_state *mb)
{
    return mch_memory_fetch_PC_op_2(mb);
}

word __attribute__((noinline, used, externally_visible))
picogb_sm83_sh2_finish(mb_state *mb, word cycles_before_prefetch)
{
    mb->IR.raw = mch_memory_fetch_PC(mb);
    return cycles_before_prefetch + 1U;
}

word
picogb_sm83_exec(mb_state *mb)
{
#if defined(__sh__) && defined(KNOVGBC_SH2_SM83_ASM)
    return picogb_sm83_exec_sh2(mb);
#else
    /* Host tests deliberately keep the C oracle selected.  The target-only
     * assembly is verified by layout assertions, disassembly and hardware;
     * semantic smoke tests always retain this exact reference path. */
    return mb_exec(mb);
#endif
}

int
picogb_sm83_selftest(mb_state *prototype)
{
#if defined(__sh__) && defined(KNOVGBC_SH2_SM83_ASM)
    /* Execute only native, register-only opcodes from HRAM.  The test keeps
     * the real memory dispatcher in the loop, so it also checks the exact
     * prefetched-IR/PC contract used by the assembly/C boundary. */
    if ((prototype == NULL) || (prototype->mi == NULL) ||
        (prototype->mi->HRAM == NULL)) {
        return 0;
    }

    r8 *const hram = prototype->mi->HRAM;
    /* The dispatcher maps FF80 to HRAM[0x80], not HRAM[0]. */
    r8 *const program = &hram[0x80U];
    const r8 saved0 = program[0];
    const r8 saved1 = program[1];
    const r8 saved2 = program[2];
    mb_state test = *prototype;
    int passed = 1;

    test.IME = 0U;
    test.IME_ASK = 0U;
    test.HALTING = 0U;
    test.FMC_MODE = MB_FMC_MODE_NONE;

    /* LD B,C; the next IR is fetched from FF80. */
    program[0] = 0x00U;
    test.PC = 0xFF80U;
    test.IR.low = 0x41U;
    test.reg.B = 0x12U;
    test.reg.C = 0xA5U;
    if ((picogb_sm83_exec(&test) != 1U) || (test.reg.B != 0xA5U) ||
        (test.PC != 0xFF81U) || (test.IR.low != 0x00U)) {
        passed = 0;
    }

    /* ADD A,B must materialize Z/H/C and clear PicoGB's lazy-flag mode. */
    program[0] = 0x00U;
    test.PC = 0xFF80U;
    test.IR.low = 0x80U;
    test.reg.A = 0x0FU;
    test.reg.B = 0x01U;
    test.reg.F = 0U;
    test.FMC_MODE = MB_FMC_MODE_NONE;
    if ((picogb_sm83_exec(&test) != 1U) || (test.reg.A != 0x10U) ||
        (test.reg.F != MB_FLAG_H) ||
        (test.FMC_MODE != MB_FMC_MODE_NONE)) {
        passed = 0;
    }

    /* ADD A,d8 verifies immediate ALU dispatch and its second fetch cycle. */
    program[0] = 0x01U;
    program[1] = 0x00U;
    test.PC = 0xFF80U;
    test.IR.low = 0xC6U;
    test.reg.A = 0xFFU;
    test.reg.F = 0U;
    test.FMC_MODE = MB_FMC_MODE_NONE;
    if ((picogb_sm83_exec(&test) != 2U) || (test.reg.A != 0x00U) ||
        (test.reg.F != (MB_FLAG_Z | MB_FLAG_H | MB_FLAG_C)) ||
        (test.PC != 0xFF82U) || (test.IR.low != 0x00U)) {
        passed = 0;
    }

    /* ADC/SBC d8 intentionally take the C fallback before consuming d8. */
    program[0] = 0x01U;
    program[1] = 0x00U;
    test.PC = 0xFF80U;
    test.IR.low = 0xCEU;
    test.reg.A = 0x01U;
    test.reg.F = MB_FLAG_C;
    test.FMC_MODE = MB_FMC_MODE_NONE;
    if ((picogb_sm83_exec(&test) != 2U) || (test.reg.A != 0x03U) ||
        (test.PC != 0xFF82U) || (test.IR.low != 0x00U)) {
        passed = 0;
    }
    test.PC = 0xFF80U;
    test.IR.low = 0xDEU;
    test.reg.A = 0x05U;
    test.reg.F = MB_FLAG_C;
    test.FMC_MODE = MB_FMC_MODE_NONE;
    if ((picogb_sm83_exec(&test) != 2U) || (test.reg.A != 0x03U) ||
        (test.PC != 0xFF82U) || (test.IR.low != 0x00U)) {
        passed = 0;
    }

    /* CP d8 is native: it must retain A while producing subtraction flags. */
    test.PC = 0xFF80U;
    test.IR.low = 0xFEU;
    test.reg.A = 0x01U;
    test.reg.F = 0U;
    test.FMC_MODE = MB_FMC_MODE_NONE;
    if ((picogb_sm83_exec(&test) != 2U) || (test.reg.A != 0x01U) ||
        (test.reg.F != (MB_FLAG_Z | MB_FLAG_N)) ||
        (test.PC != 0xFF82U) || (test.IR.low != 0x00U)) {
        passed = 0;
    }

    /* DEC preserves carry and sets N/H when wrapping through zero. */
    program[0] = 0x00U;
    test.PC = 0xFF80U;
    test.IR.low = 0x05U;
    test.reg.B = 0x00U;
    test.reg.F = MB_FLAG_C;
    test.FMC_MODE = MB_FMC_MODE_NONE;
    if ((picogb_sm83_exec(&test) != 1U) || (test.reg.B != 0xFFU) ||
        (test.reg.F != (MB_FLAG_N | MB_FLAG_H | MB_FLAG_C))) {
        passed = 0;
    }

    /* LD C,d8 validates operand fetch, PC increment and prefetch. */
    program[0] = 0x5AU;
    program[1] = 0x00U;
    test.PC = 0xFF80U;
    test.IR.low = 0x0EU;
    if ((picogb_sm83_exec(&test) != 2U) || (test.reg.C != 0x5AU) ||
        (test.PC != 0xFF82U) || (test.IR.low != 0x00U)) {
        passed = 0;
    }

    /* LD BC,d16 catches SH-2 big-endian stores at the CPU boundary. */
    program[0] = 0x34U;
    program[1] = 0x12U;
    program[2] = 0x00U;
    test.PC = 0xFF80U;
    test.IR.low = 0x01U;
    if ((picogb_sm83_exec(&test) != 3U) || (test.reg.BC != 0x1234U) ||
        (test.PC != 0xFF83U) || (test.IR.low != 0x00U)) {
        passed = 0;
    }

    /* ADD HL,BC intentionally retains PicoGB's lazy H representation. */
    program[0] = 0x00U;
    test.PC = 0xFF80U;
    test.IR.low = 0x09U;
    test.reg.HL = 0xFFFFU;
    test.reg.BC = 0x0001U;
    test.reg.F = MB_FLAG_Z;
    test.FMC_MODE = MB_FMC_MODE_NONE;
    if ((picogb_sm83_exec(&test) != 2U) || (test.reg.HL != 0x0000U) ||
        (test.reg.F != (MB_FLAG_Z | MB_FLAG_C)) ||
        (test.FR1 != 0xFFFFU) || (test.FR2 != 0x0001U) ||
        (test.FMC_MODE != MB_FMC_MODE_ADD_r16)) {
        passed = 0;
    }

    /* EI is delayed by precisely one instruction. */
    program[0] = 0x00U;
    program[1] = 0x00U;
    test.PC = 0xFF80U;
    test.IR.low = 0xFBU;
    test.IME = 0U;
    test.IME_ASK = 0U;
    if ((picogb_sm83_exec(&test) != 1U) || (test.IME != 0U) ||
        (test.IME_ASK != 1U)) {
        passed = 0;
    }
    if ((picogb_sm83_exec(&test) != 1U) || (test.IME != 1U) ||
        (test.IME_ASK != 0U)) {
        passed = 0;
    }

    program[0] = saved0;
    program[1] = saved1;
    program[2] = saved2;
    return passed;
#else
    (void)prototype;
    return 1;
#endif
}
