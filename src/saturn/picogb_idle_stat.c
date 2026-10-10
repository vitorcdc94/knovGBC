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
#error "STAT shortcut requires the 033 direct-fetch memory configuration"
#endif

/* Called from the existing F0 recognizer's rejection path, never as a new
 * per-instruction admission check. STAT reads through the current standard
 * I/O callback are side-effect free; its value is stable until a PPU event.
 * Do not carry a remembered value across that event. */
/* Keep this helper out of cross-unit constant propagation. The current SM83
 * PC and PPU phase still change on every invocation. */
__attribute__((noipa)) word
picogb_idle_stat_try(mb_state *mb, const ppu_t *ppu,
    const struct pgf_userdata_t *fabric, word *instructions)
{
#if CONFIG_USE_FLAT_ROM && CONFIG_NOBOOTMEME && !CONFIG_ENABLE_LRU
    const struct mi_dispatch *mi;
    const r8 *code;
    word pc, offset, stat, mode, trips, cycles, steps;

    if ((mb == NULL) || (ppu == NULL) || (fabric == NULL) ||
        (mb->mi == NULL)) return 0U;
    if ((mb->IR.raw != 0x00F0U) || (mb->HALTING != 0U) ||
        ((mb->IME == 0U) && (mb->IME_ASK != 0U)) ||
        ((mb->IME != 0U) && (mbh_irq_get_pending(mb) != 0U)) ||
        ((fabric->TIMER_CNT & 4U) != 0U) ||
        ((ppu->rLCDC & 0x80U) == 0U) || (ppu->_redrawed != 0U) ||
        (ppu->next_update_ticks == 0U)) return 0U;
    mi = mb->mi;
    /* Never bypass a different callback, bus hook or device binding. */
    if ((mi->dispatch_IO != pgf_cb_IO_) || (mi->userdata != fabric) ||
        (fabric->ppu != ppu) || (mi->ROM == NULL)) return 0U;
    stat = ppu->rSTAT | 0x80U;
    mode = stat & 3U;
    if (mode == 0U) return 0U; /* The ordinary interpreter exits the wait. */

    /* Six bytes, wholly inside one mapped ROM bank. PC is F0's operand.
     * Other operands, masks, branches and executable regions remain normal. */
    pc = mb->PC;
    if ((pc >= 1U) && (pc <= 0x3FFBU) && (mi->N_ROM != 0U)) {
        offset = pc - 1U;
    }
    else if ((pc >= 0x4001U) && (pc <= 0x7FFBU) &&
        (mi->BANK_ROM < mi->N_ROM)) {
        offset = (mi->BANK_ROM << 14) | ((pc - 1U) & 0x3FFFU);
    }
    else return 0U;
    code = mi->ROM + offset;
    if ((code[0] != 0xF0U) || (code[1] != 0x41U) ||
        (code[2] != 0xE6U) || (code[3] != 0x03U) ||
        (code[4] != 0x20U) || (code[5] != 0xFAU)) return 0U;

    /* Full trip: LDH (12 dots), AND n8 (8), taken JR (12). Preserve exactly
     * the first completed opcode at/after the event, including an LDH that
     * read the OLD STAT immediately before the caller commits that event. */
    trips = (ppu->next_update_ticks - 1U) >> 5;
    if (trips >= 16U) {
        trips = 16U;
        cycles = 128U;
        steps = 48U;
    }
    else {
        const word remaining = ppu->next_update_ticks - trips * 32U;
        cycles = trips * 8U;
        steps = trips * 3U;
        if (remaining <= 12U) {
            cycles += 3U;
            ++steps;
            mb->IR.raw = 0x00E6U;
            mb->PC = (r16)(pc + 2U);
            mb->reg.A = (r8)stat;
            if (trips != 0U) {
                mb->reg.F = MB_FLAG_H;
                mb->FMC_MODE = MB_FMC_MODE_NONE;
            }
            if (instructions != NULL) *instructions = steps;
            return cycles;
        }
        else if (remaining <= 20U) {
            cycles += 5U;
            steps += 2U;
            mb->IR.raw = 0x0020U;
            mb->PC = (r16)(pc + 4U);
        }
        else {
            cycles += 8U;
            steps += 3U;
            /* Taken JR restores the original prefetched F0 and its PC. */
        }
    }
    mb->reg.A = (r8)mode;
    mb->reg.F = MB_FLAG_H; /* AND 3 yielded 1..3, hence Z=0. */
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
