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
#include "picogb_idle_spin.h"

#include <stddef.h>

#if !defined(KNOVGBC_OPCODE_FETCH_INLINE) || !defined(KNOVGBC_FAST_MEMORY)
#error "Sum-loop shortcut requires the 033 direct-fetch memory configuration"
#endif

/* This is useful arithmetic, not an idle wait. Compute exactly the same
 * additions/countdown, stopping at an observable instruction boundary.
 * Keep the uncommon recognizer out of the main interpreter's hot body. */
__attribute__((noinline)) word
picogb_sum_loop_try(mb_state *mb, const ppu_t *ppu,
    const struct pgf_userdata_t *fabric, word *instructions)
{
#if CONFIG_USE_FLAT_ROM && CONFIG_NOBOOTMEME && !CONFIG_ENABLE_LRU
    const r8 *code;
    word pc, offset, trips, until_zero, adds, decrements, phase;
    word cycles, steps, left, result, count, flags;

    if ((mb == NULL) || (ppu == NULL) || (fabric == NULL) ||
        (mb->mi == NULL)) return 0U;
    if ((mb->IR.raw != 0x0019U) || (mb->HALTING != 0U) ||
        ((mb->IME == 0U) && (mb->IME_ASK != 0U)) ||
        ((mb->IME != 0U) && (mbh_irq_get_pending(mb) != 0U)) ||
        ((fabric->TIMER_CNT & 4U) != 0U) ||
        ((ppu->rLCDC & 0x80U) == 0U) || (ppu->_redrawed != 0U) ||
        (ppu->next_update_ticks == 0U)) return 0U;

    /* PC follows the prefetched ADD. All four bytes must be in one ordinary
     * executable region; recheck mutable HRAM and the current bank each time.
     * Neither the terminal JR nor its subsequent fetch is synthesized. */
    pc = mb->PC;
    if ((pc >= 0xFF81U) && (pc <= 0xFFFCU)) {
        if (mb->mi->HRAM == NULL) return 0U;
        code = mb->mi->HRAM + ((pc - 1U) & 0xFFU);
    }
    else if ((pc >= 1U) && (pc <= 0x3FFDU)) {
        if ((mb->mi->ROM == NULL) || (mb->mi->N_ROM == 0U)) return 0U;
        code = mb->mi->ROM + pc - 1U;
    }
    else if ((pc >= 0x4001U) && (pc <= 0x7FFDU) &&
        (mb->mi->BANK_ROM < mb->mi->N_ROM)) {
        if (mb->mi->ROM == NULL) return 0U;
        offset = (mb->mi->BANK_ROM << 14) | ((pc - 1U) & 0x3FFFU);
        code = mb->mi->ROM + offset;
    }
    else return 0U;
    if ((code[0] != 0x19U) || (code[1] != 0x05U) ||
        (code[2] != 0x20U) || (code[3] != 0xFCU)) return 0U;

    /* ADD HL,DE (8 dots), DEC B (4), taken JR NZ,-4 (12).
     * B=0 means 256 decrements, not zero trips. DE is constant throughout.
     * Complete only nonterminal trips strictly before the next PPU event. */
    until_zero = mb->reg.B != 0U ? mb->reg.B : 256U;
    trips = (ppu->next_update_ticks - 1U) / 24U;
    if (trips > 16U) trips = 16U;
    if (trips >= until_zero) trips = until_zero - 1U;
    adds = decrements = trips;
    cycles = trips * 6U;
    steps = trips * 3U;
    phase = 0U; /* 0: after taken JR, 1: after ADD, 2: after DEC. */

    if (trips < 16U) {
        const word remaining = ppu->next_update_ticks - trips * 24U;
        ++adds;
        cycles += 2U;
        ++steps;
        if (remaining <= 8U) {
            phase = 1U;
        }
        else {
            ++decrements;
            ++cycles;
            ++steps;
            if ((decrements == until_zero) || (remaining <= 12U)) {
                phase = 2U;
            }
            else {
                cycles += 3U;
                ++steps;
            }
        }
    }

    /* The last ADD's inputs must survive in FR1/FR2 even if a following
     * DEC clears deferred flag mode. Carry comes from THAT addition, not
     * from the unbounded sum across the whole batch. */
    left = (mb->reg.HL + (adds - 1U) * mb->reg.DE) & 0xFFFFU;
    result = left + mb->reg.DE;
    count = ((word)mb->reg.B - decrements) & 0xFFU;
    flags = (result >> 16) != 0U ? MB_FLAG_C : 0U;
    mb->FR1 = left;
    mb->FR2 = mb->reg.DE;
    mb->reg.HL = (r16)result;
    mb->reg.B = (r8)count;
    if (phase == 1U) {
        /* ADD preserves incoming Z only if no earlier DEC ran. Its H flag
         * is deferred exactly as in the normal PicoGB interpreter. */
        if (trips == 0U) flags |= mb->reg.F & MB_FLAG_Z;
        mb->FMC_MODE = MB_FMC_MODE_ADD_r16;
        mb->IR.raw = 0x0005U;
        mb->PC = (r16)(pc + 1U);
    }
    else {
        flags |= MB_FLAG_N;
        if ((count & 0x0FU) == 0x0FU) flags |= MB_FLAG_H;
        if (count == 0U) flags |= MB_FLAG_Z;
        mb->FMC_MODE = MB_FMC_MODE_NONE;
        if (phase == 2U) {
            mb->IR.raw = 0x0020U;
            mb->PC = (r16)(pc + 2U);
        }
    }
    mb->reg.F = (r8)flags;
    if (instructions != NULL) *instructions = steps;
    return cycles;
#else
    (void)mb;
    (void)ppu;
    (void)fabric;
    (void)instructions;
    return 0U;
#endif
}
