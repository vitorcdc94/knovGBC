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
#error "Countdown shortcut requires the 032 direct-fetch memory configuration"
#endif

/* Keep this less frequent recognizer out of the already cache-sensitive
 * interpreter/main body. The host-only count is specialized away by LTO when
 * the Saturn caller passes NULL. No DMA transfer or emulated time is skipped. */
__attribute__((noinline)) word
picogb_idle_countdown_try(mb_state *mb, const ppu_t *ppu,
    const struct pgf_userdata_t *fabric, word *instructions)
{
#if CONFIG_USE_FLAT_ROM && CONFIG_NOBOOTMEME && !CONFIG_ENABLE_LRU
    const r8 *code;
    word pc, offset, trips, until_zero, value, flags, cycles, steps;

    if ((mb == NULL) || (ppu == NULL) || (fabric == NULL) ||
        (mb->mi == NULL)) return 0U;
    if ((mb->IR.raw != 0x003DU) || (mb->HALTING != 0U) ||
        ((mb->IME == 0U) && (mb->IME_ASK != 0U)) ||
        ((mb->IME != 0U) && (mbh_irq_get_pending(mb) != 0U)) ||
        ((fabric->TIMER_CNT & 4U) != 0U) ||
        ((ppu->rLCDC & 0x80U) == 0U) || (ppu->_redrawed != 0U) ||
        (ppu->next_update_ticks == 0U)) return 0U;

    /* PC is the prefetched DEC's next byte. Check the entire three-byte
     * pattern in one ordinary memory region, including mutable HRAM every
     * time. Do not fetch the opcode after the loop: it could be I/O. */
    pc = mb->PC;
    if ((pc >= 0xFF81U) && (pc <= 0xFFFDU)) {
        if (mb->mi->HRAM == NULL) return 0U;
        code = mb->mi->HRAM + ((pc - 1U) & 0xFFU);
    }
    else if ((pc >= 1U) && (pc <= 0x3FFEU)) {
        if ((mb->mi->ROM == NULL) || (mb->mi->N_ROM == 0U)) return 0U;
        code = mb->mi->ROM + pc - 1U;
    }
    else if ((pc >= 0x4001U) && (pc <= 0x7FFEU) &&
        (mb->mi->BANK_ROM < mb->mi->N_ROM)) {
        if (mb->mi->ROM == NULL) return 0U;
        offset = (mb->mi->BANK_ROM << 14) | ((pc - 1U) & 0x3FFFU);
        code = mb->mi->ROM + offset;
    }
    else return 0U;
    if ((code[0] != 0x3DU) || (code[1] != 0x20U) || (code[2] != 0xFDU))
        return 0U;

    /* Taken trip = DEC (4 dots) + JR (12). A=0 wraps and takes 256 DECs.
     * Full trips before the event must also stop before the zero-producing
     * DEC, so the finite countdown can never be treated as an infinite wait. */
    until_zero = mb->reg.A != 0U ? mb->reg.A : 256U;
    trips = (ppu->next_update_ticks - 1U) >> 4;
    if (trips > 16U) trips = 16U;
    if (trips >= until_zero) trips = until_zero - 1U;
    value = ((word)mb->reg.A - trips) & 0xFFU;
    cycles = trips * 4U;
    steps = trips * 2U;

    if (trips < 16U) {
        const word remaining = ppu->next_update_ticks - trips * 16U;
        value = (value - 1U) & 0xFFU;
        ++cycles;
        ++steps;
        if ((value == 0U) || (remaining <= 4U)) {
            /* Stop after DEC. In particular, the terminal JR and subsequent
             * fetch go through the normal interpreter and memory callbacks. */
            mb->IR.raw = 0x0020U;
            mb->PC = (r16)(pc + 1U);
        }
        else {
            cycles += 3U;
            ++steps;
            /* Taken JR returns to the original prefetched DEC/PC. */
        }
    }

    /* Match the current interpreter's eager DEC flags: preserve raw carry,
     * recompute N/H/Z, discard deferred mode, leave FR1/FR2/IMM untouched. */
    flags = (mb->reg.F & MB_FLAG_C) | MB_FLAG_N;
    if ((value & 0x0FU) == 0x0FU) flags |= MB_FLAG_H;
    if (value == 0U) flags |= MB_FLAG_Z;
    mb->reg.A = (r8)value;
    mb->reg.F = (r8)flags;
    mb->FMC_MODE = MB_FMC_MODE_NONE;
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
