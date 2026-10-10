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
#include "picogb_sm83_compact.h"

#include "microcode_dispatch.h"

#include <stdint.h>

/*
 * Compact C front end for PicoGB's SM83 interpreter.
 *
 * The original mb_exec() remains the canonical implementation for the
 * uncommon instructions. This function implements dense, general SM83
 * families, then reuses mb_exec() for everything else. It is deliberately
 * not tied to a particular ROM: all memory still goes through PicoGB's
 * mapper/I/O dispatcher and the fallback preserves every legal DMG opcode.
 *
 * Contract shared with mb_exec(): IR.low is already prefetched and PC points
 * at the first operand. A completed instruction fetches the next opcode and
 * includes that fetch in its returned machine-cycle count.
 */

#define C_FLAG_Z MB_FLAG_Z
#define C_FLAG_N MB_FLAG_N
#define C_FLAG_H MB_FLAG_H
#define C_FLAG_C MB_FLAG_C

/* PicoGB's raw byte view follows the original little-endian decoder. This
 * lvalue keeps B/C/D/E/H/L/A correct on both host tests and the Saturn's
 * big-endian SH-2. Register code 6 is always (HL). */
#define C_REG8(mb, code) MB_REG8((mb), ((word)(code) ^ 1U))

#define COMPACT_EXEC_ATTRIBUTES \
    __attribute__((noinline, noclone, externally_visible, hot, optimize("Os")))

/* The executor itself is intentionally size-optimized.  Its helpers must use
 * the same optimization options when forced inline, otherwise GCC correctly
 * rejects the call as a target-option mismatch. */
#define COMPACT_INLINE_ATTRIBUTES \
    __attribute__((always_inline, optimize("Os")))

static inline COMPACT_INLINE_ATTRIBUTES word
compact_finish(mb_state *mb, word cycles_before_fetch)
{
    mb->IR.raw = mch_memory_fetch_PC(mb);
    return cycles_before_fetch + 1U;
}

static inline COMPACT_INLINE_ATTRIBUTES word
compact_read_r8(mb_state *mb, word code, word *extra_cycles)
{
    if (code == 6U) {
        ++*extra_cycles;
        return mch_memory_dispatch_read(mb, mb->reg.HL);
    }

    return C_REG8(mb, code);
}

static inline COMPACT_INLINE_ATTRIBUTES void
compact_write_r8(mb_state *mb, word code, word value, word *extra_cycles)
{
    if (code == 6U) {
        ++*extra_cycles;
        mch_memory_dispatch_write(mb, mb->reg.HL, value);
        return;
    }

    C_REG8(mb, code) = (r8)value;
}

/* Preserve PicoGB's lazy half-carry representation. The reference fallback
 * uses it for DAA and PUSH AF, so carrying it forward exactly is safer than
 * maintaining a second representation of the flags. */
static inline COMPACT_INLINE_ATTRIBUTES void
compact_lazy_add8(mb_state *mb, word left, word right)
{
    mb->FR1 = (r16)left;
    mb->FR2 = (r16)right;
    mb->FMC_MODE = MB_FMC_MODE_ADD_r8;
}

static inline COMPACT_INLINE_ATTRIBUTES void
compact_lazy_adc8(mb_state *mb, word left, word right)
{
    mb->FR1 = (r16)left;
    mb->FR2 = (r16)right;
    mb->FMC_MODE = MB_FMC_MODE_ADC_r8;
}

static inline COMPACT_INLINE_ATTRIBUTES void
compact_lazy_sub8(mb_state *mb, word left, word right)
{
    mb->FR1 = (r16)left;
    mb->FR2 = (r16)right;
    mb->FMC_MODE = MB_FMC_MODE_SUB_r8;
}

static inline COMPACT_INLINE_ATTRIBUTES void
compact_lazy_sbc8(mb_state *mb, word left, word right)
{
    mb->FR1 = (r16)left;
    mb->FR2 = (r16)right;
    mb->FMC_MODE = MB_FMC_MODE_SBC_r8;
}

static inline COMPACT_INLINE_ATTRIBUTES void
compact_alu(mb_state *mb, word opcode, word operand)
{
    const word a = mb->reg.A;
    word result;
    word flags;

    switch (opcode & 0x38U) {
    case 0x00U: /* ADD A,r */
        compact_lazy_add8(mb, a, operand);
        result = a + operand;
        flags = (result > 0xFFU) ? C_FLAG_C : 0U;
        if ((result & 0xFFU) == 0U)
            flags |= C_FLAG_Z;
        mb->reg.A = (r8)result;
        mb->reg.F = (r8)flags;
        break;

    case 0x08U: /* ADC A,r */
    {
        const word carry = (mb->reg.F & C_FLAG_C) != 0U;
        if (carry != 0U)
            compact_lazy_adc8(mb, a, operand);
        else
            compact_lazy_add8(mb, a, operand);
        result = a + operand + carry;
        flags = (result > 0xFFU) ? C_FLAG_C : 0U;
        if ((result & 0xFFU) == 0U)
            flags |= C_FLAG_Z;
        mb->reg.A = (r8)result;
        mb->reg.F = (r8)flags;
        break;
    }

    case 0x10U: /* SUB r */
        compact_lazy_sub8(mb, a, operand);
        result = (a - operand) & 0x1FFU;
        flags = C_FLAG_N;
        if (result > 0xFFU)
            flags |= C_FLAG_C;
        if ((result & 0xFFU) == 0U)
            flags |= C_FLAG_Z;
        mb->reg.A = (r8)result;
        mb->reg.F = (r8)flags;
        break;

    case 0x18U: /* SBC A,r */
    {
        const word carry = (mb->reg.F & C_FLAG_C) != 0U;
        if (carry != 0U)
            compact_lazy_sbc8(mb, a, operand);
        else
            compact_lazy_sub8(mb, a, operand);
        result = (a - operand - carry) & 0x1FFU;
        flags = C_FLAG_N;
        if (result > 0xFFU)
            flags |= C_FLAG_C;
        if ((result & 0xFFU) == 0U)
            flags |= C_FLAG_Z;
        mb->reg.A = (r8)result;
        mb->reg.F = (r8)flags;
        break;
    }

    case 0x20U: /* AND */
        result = a & operand;
        flags = C_FLAG_H;
        if (result == 0U)
            flags |= C_FLAG_Z;
        mb->reg.A = (r8)result;
        mb->reg.F = (r8)flags;
        mb->FMC_MODE = MB_FMC_MODE_NONE;
        break;

    case 0x28U: /* XOR */
        result = a ^ operand;
        mb->reg.A = (r8)result;
        mb->reg.F = (result == 0U) ? C_FLAG_Z : 0U;
        mb->FMC_MODE = MB_FMC_MODE_NONE;
        break;

    case 0x30U: /* OR */
        result = a | operand;
        mb->reg.A = (r8)result;
        mb->reg.F = (result == 0U) ? C_FLAG_Z : 0U;
        mb->FMC_MODE = MB_FMC_MODE_NONE;
        break;

    default: /* CP */
        compact_lazy_sub8(mb, a, operand);
        result = (a - operand) & 0x1FFU;
        flags = C_FLAG_N;
        if (result > 0xFFU)
            flags |= C_FLAG_C;
        if ((result & 0xFFU) == 0U)
            flags |= C_FLAG_Z;
        mb->reg.F = (r8)flags;
        break;
    }
}

static inline COMPACT_INLINE_ATTRIBUTES word
compact_push(mb_state *mb, word value, word cycles_before_fetch)
{
    const word sp = mb->SP;

    mch_memory_dispatch_write(mb, (sp - 1U) & 0xFFFFU, value >> 8);
    mch_memory_dispatch_write(mb, (sp - 2U) & 0xFFFFU, value & 0xFFU);
    mb->SP = (r16)(sp - 2U);
    return compact_finish(mb, cycles_before_fetch + 3U);
}

static inline COMPACT_INLINE_ATTRIBUTES word
compact_jr(mb_state *mb)
{
    const word displacement = mch_memory_fetch_PC_op_1(mb);

    /* Match the reference core's deliberate idle-loop convention. */
    if (displacement == 0xFEU) {
        if (((mb->IME == 0U) && (mb->IME_ASK == 0U)) ||
            ((mb->IE == 0U) && (mb->IF == 0U))) {
            return 0U;
        }
        mb->PC = (r16)(mb->PC - 2U);
    }
    else {
        mb->PC = (r16)(mb->PC + (int8_t)displacement);
    }

    return compact_finish(mb, 2U);
}

static inline COMPACT_INLINE_ATTRIBUTES word
compact_interrupt_if_pending(mb_state *mb)
{
    word pending;
    word bit = 0U;

    if (mb->IME == 0U) {
        if (mb->IME_ASK != 0U) {
            mb->IME = 1U;
            mb->IME_ASK = 0U;
        }
        return 0U;
    }

    pending = mbh_irq_get_pending(mb);
    if (pending == 0U)
        return 0U;

    while ((pending & (1U << bit)) == 0U)
        ++bit;

    /* IR is prefetched, so the reference interrupts at PC - 1. */
    {
        const word return_pc = (mb->PC - 1U) & 0xFFFFU;
        mb->PC = (r16)(0x40U + (bit << 3));
        mb->IME = 0U;
        mb->IME_ASK = 0U;
        mb->IF &= ~(1U << bit);
        return compact_push(mb, return_pc, 1U);
    }
}

/*
 * This is intentionally a dense C function rather than a 256-entry function
 * pointer table. The latter would add an indirect branch and a 1 KiB table to
 * the SH-2's 4 KiB cache. The fast families are general SM83 instructions;
 * paths not covered here use the complete reference core.
 */
word COMPACT_EXEC_ATTRIBUTES
picogb_sm83_compact_exec(mb_state *mb)
{
    const word opcode = mb->IR.low;
    /* 20, 28, 30 and 38 are exactly the four encodings selected by this
     * mask.  Keeping that relation explicit prevents SH-2 GCC from lowering
     * four equality tests to a variable shift/libgcc helper in the hot
     * classifier. */
    const wbool conditional_jr = (opcode & 0xE7U) == 0x20U;
    const wbool compact_path =
        (((opcode >= 0x40U) && (opcode < 0x80U) && (opcode != 0x76U)) ||
         ((opcode >= 0x80U) && (opcode < 0xC0U)) ||
         (opcode == 0xF0U) || conditional_jr || (opcode == 0x05U));

    /* Let the reference own its whole prologue as well as its uncommon
     * opcode. Promoting IME_ASK here and then falling back would make an
     * already-pending interrupt fire one instruction too early after EI. */
    if (compact_path == 0U)
        return mb_exec(mb);

    {
        const word interrupt_cycles = compact_interrupt_if_pending(mb);

        if (interrupt_cycles != 0U)
            return interrupt_cycles;
    }

    if (opcode == 0xA7U) { /* AND A: a frequent, general one-byte ALU op. */
        const word result = mb->reg.A;

        mb->reg.F = (r8)(C_FLAG_H | ((result == 0U) ? C_FLAG_Z : 0U));
        mb->FMC_MODE = MB_FMC_MODE_NONE;
        return compact_finish(mb, 0U);
    }

    if ((opcode >= 0x40U) && (opcode < 0x80U)) {
        word cycles = 0U;
        const word source = opcode & 7U;
        const word destination = (opcode >> 3) & 7U;

        compact_write_r8(mb, destination,
            compact_read_r8(mb, source, &cycles), &cycles);
        return compact_finish(mb, cycles);
    }

    if ((opcode >= 0x80U) && (opcode < 0xC0U)) {
        word cycles = 0U;
        const word operand = compact_read_r8(mb, opcode & 7U, &cycles);

        compact_alu(mb, opcode, operand);
        return compact_finish(mb, cycles);
    }

    if (opcode == 0xF0U) {
        const word address = mch_memory_fetch_PC_op_1(mb);

        mb->reg.A = (r8)mch_memory_dispatch_read_Haddr(mb, address);
        return compact_finish(mb, 2U);
    }

    if (conditional_jr != 0U) {
        const word flags = mb->reg.F;
        const wbool taken =
            (opcode == 0x20U) ? ((flags & C_FLAG_Z) == 0U) :
            (opcode == 0x28U) ? ((flags & C_FLAG_Z) != 0U) :
            (opcode == 0x30U) ? ((flags & C_FLAG_C) == 0U) :
                                ((flags & C_FLAG_C) != 0U);

        if (taken != 0U)
            return compact_jr(mb);

        /* PicoGB deliberately skips the operand without reading it here. */
        mb->PC = (r16)(mb->PC + 1U);
        return compact_finish(mb, 1U);
    }

    if (opcode == 0x05U) { /* DEC B, common in normal loop code. */
        const word previous = mb->reg.B;
        const word value = (previous - 1U) & 0xFFU;
        word flags = (mb->reg.F & C_FLAG_C) | C_FLAG_N;

        if ((previous & 0x0FU) == 0U)
            flags |= C_FLAG_H;
        if (value == 0U)
            flags |= C_FLAG_Z;
        mb->reg.B = (r8)value;
        mb->reg.F = (r8)flags;
        mb->FMC_MODE = MB_FMC_MODE_NONE;
        return compact_finish(mb, 0U);
    }

    return mb_exec(mb);
}

#undef COMPACT_INLINE_ATTRIBUTES
#undef COMPACT_EXEC_ATTRIBUTES
