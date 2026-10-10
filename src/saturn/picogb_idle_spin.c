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

#if defined(KNOVGBC_IDLE_RESUME) && !defined(KNOVGBC_IDLE_EVENT)
#error "Idle resume requires the 031 event-boundary policy"
#endif

#if defined(KNOVGBC_IDLE_STAT) && (!defined(KNOVGBC_IDLE_EVENT) || \
    !defined(KNOVGBC_IDLE_RESUME) || defined(KNOVGBC_IDLE_SPIN_CACHE))
#error "STAT fallback requires the 033 event/resume policy without code cache"
#endif

#if defined(KNOVGBC_IDLE_EVENT) && (!defined(KNOVGBC_A7_DIRECT) || \
    !defined(KNOVGBC_F0_ROM_OPERAND_INLINE) || \
    !defined(KNOVGBC_OPCODE_FETCH_INLINE) || defined(KNOVGBC_IDLE_SPIN_CACHE))
#error "Idle event requires the 029 direct-opcode baseline without the 030 cache"
#endif

#if defined(KNOVGBC_IDLE_SPIN_CACHE)
/* One positive recognition, keyed by the mapped address in immutable ROM.
 * Bank changes are resolved before consulting this entry. The
 * HRAM pointer/value and all device/CPU state are deliberately never cached. */
static struct {
    const r8 *site;
    word operand;
} idle_recognition;

#if defined(KNOVGBC_IDLE_SPIN_CACHE_STATS)
word picogb_idle_spin_cache_hits;
word picogb_idle_spin_cache_misses;
#endif

void
picogb_idle_spin_cache_reset(void)
{
    idle_recognition.site = NULL;
#if defined(KNOVGBC_IDLE_SPIN_CACHE_STATS)
    picogb_idle_spin_cache_hits = 0U;
    picogb_idle_spin_cache_misses = 0U;
#endif
}
#endif

/* An event-free polling loop has no useful CPU work between device events.
 * Recognize a general SM83 instruction sequence, never a Tetris PC or ROM
 * address: LDH A,(a8); AND A; JR Z,-5.  Restrict the read to ordinary HRAM,
 * whose contents cannot change while this loop executes without an IRQ.
 * All other programs and memory maps retain the normal interpreter. */
word
#if defined(KNOVGBC_IDLE_EVENT)
picogb_idle_event_try(mb_state *mb, const ppu_t *ppu,
    const struct pgf_userdata_t *fabric, word *instructions)
#else
picogb_idle_spin_try(mb_state *mb, const ppu_t *ppu,
    const struct pgf_userdata_t *fabric)
#endif
{
#if CONFIG_USE_FLAT_ROM && CONFIG_NOBOOTMEME && !CONFIG_ENABLE_LRU
    const struct mi_dispatch *mi;
    const r8 *rom;
    word pc;
    word offset;
    word operand;
    word iterations;
    hilow16_t fa;

    if ((mb == NULL) || (ppu == NULL) || (fabric == NULL) ||
        (mb->mi == NULL)) {
        return 0U;
    }

    if (
#if defined(KNOVGBC_IDLE_RESUME)
        ((mb->IR.raw != 0x00F0U) && (mb->IR.raw != 0x00A7U) &&
            (mb->IR.raw != 0x0028U)) ||
#else
        (mb->IR.raw != 0x00F0U) ||
#endif
        (mb->HALTING != 0U) ||
        ((mb->IME == 0U) && (mb->IME_ASK != 0U)) ||
        ((mb->IME != 0U) && (mbh_irq_get_pending(mb) != 0U)) ||
        ((fabric->TIMER_CNT & 4U) != 0U) ||
        ((ppu->rLCDC & 0x80U) == 0U) ||
        (ppu->_redrawed != 0U) ||
#if defined(KNOVGBC_IDLE_EVENT)
        (ppu->next_update_ticks == 0U)) {
#else
        (ppu->next_update_ticks <= 28U)) {
#endif
        return 0U;
    }

    /* PC points at F0's immediate operand because the opcode was prefetched.
     * Keep the five-byte loop wholly inside one mapped ROM bank. */
    pc = mb->PC;
#if defined(KNOVGBC_IDLE_RESUME)
    /* A matching ROM sequence alone does not prove how we entered it.
     * Admit only live CPU states that make the remaining suffix a zero
     * polling iteration. Canonical JR flags avoid changing flags that the
     * branch itself must preserve. Normalize to F0's operand address. */
    if (mb->IR.raw == 0x00A7U) {
        if ((mb->reg.A != 0U) || (pc < 3U)) return 0U;
        pc -= 2U;
    }
    else if (mb->IR.raw == 0x0028U) {
        if ((mb->reg.A != 0U) ||
            (mb->reg.F != (MB_FLAG_H | MB_FLAG_Z)) ||
            (mb->FMC_MODE != MB_FMC_MODE_NONE) || (pc < 4U)) return 0U;
        pc -= 3U;
    }
#endif
    if ((pc >= 1U) && (pc <= 0x3FFCU)) {
        offset = pc;
    }
    else if ((pc >= 0x4001U) && (pc <= 0x7FFCU) &&
        (mb->mi->BANK_ROM < mb->mi->N_ROM)) {
        offset = mb->mi->BANK_ROM << 14;
        offset |= pc & 0x3FFFU;
    }
    else {
        return 0U;
    }

    mi = mb->mi;
    rom = mi->ROM;
#if defined(KNOVGBC_IDLE_SPIN_CACHE)
    if ((rom == NULL) || (mi->HRAM == NULL)) {
        return 0U;
    }

    const r8 *const site = rom + offset;
    if (idle_recognition.site == site) {
        operand = idle_recognition.operand;
#if defined(KNOVGBC_IDLE_SPIN_CACHE_STATS)
        ++picogb_idle_spin_cache_hits;
#endif
    }
    else {
#if defined(KNOVGBC_IDLE_SPIN_CACHE_STATS)
        ++picogb_idle_spin_cache_misses;
#endif
        if ((site[-1] != 0xF0U) || (site[1] != 0xA7U) ||
            (site[2] != 0x28U) || (site[3] != 0xFBU)) {
            return 0U;
        }
        operand = site[0];
        if ((operand < 0x80U) || (operand >= 0xFFU)) {
            return 0U;
        }
        idle_recognition.site = site;
        idle_recognition.operand = operand;
    }
    if (mi->HRAM[operand] != 0U) {
        return 0U;
    }
#else
    if ((rom == NULL) || (mi->HRAM == NULL) ||
        (rom[offset - 1U] != 0xF0U) ||
        (rom[offset + 1U] != 0xA7U) ||
        (rom[offset + 2U] != 0x28U) ||
        (rom[offset + 3U] != 0xFBU)) {
#if defined(KNOVGBC_IDLE_STAT)
        /* Extend only the existing recognizer's rejection path. Hot HRAM
         * waits and their A7/JR resumptions retain the original path. */
        if ((mb->IR.raw == 0x00F0U) && (rom != NULL) &&
            (rom[offset] == 0x41U)) {
            return picogb_idle_stat_try(mb, ppu, fabric, instructions);
        }
#endif
        return 0U;
    }

    operand = rom[offset];
    if ((operand < 0x80U) || (operand >= 0xFFU) ||
        (mi->HRAM[operand] != 0U)) {
        return 0U;
    }
#endif

#if defined(KNOVGBC_IDLE_EVENT)
    word deadline = ppu->next_update_ticks;
    word prefix_cycles = 0U;
    word prefix_steps = 0U;
#if defined(KNOVGBC_IDLE_RESUME)
    /* Finish the initial suffix, stopping at its first event-crossing
     * opcode, before using the unchanged F0 batching calculation below.
     * All admission checks above precede these CPU mutations. No device
     * state or remembered recognition is carried across the event. */
    if (mb->IR.raw == 0x00A7U) {
        fa.low = 0U;
        fa.high = MB_FLAG_H | MB_FLAG_Z;
        mb->reg.hilo16[3] = fa;
        mb->FMC_MODE = MB_FMC_MODE_NONE;
        mb->PC = (r16)(pc + 3U);
        mb->IR.raw = 0x0028U;
        prefix_cycles = 1U;
        prefix_steps = 1U;
        if (deadline <= 4U) {
            if (instructions != NULL) *instructions = prefix_steps;
            return prefix_cycles;
        }
        deadline -= 4U;
    }
    if (mb->IR.raw == 0x0028U) {
        mb->PC = (r16)pc;
        mb->IR.raw = 0x00F0U;
        prefix_cycles += 3U;
        ++prefix_steps;
        if (deadline <= 12U) {
            if (instructions != NULL) *instructions = prefix_steps;
            return prefix_cycles;
        }
        deadline -= 12U;
    }
#endif
    /* Events are observed at completed-instruction boundaries by the normal
     * frontend. Include exactly the first such boundary at/after the event:
     * F0 = 12 dots, AND = 4 dots, taken JR = 12 dots. Do not execute another
     * opcode after that boundary: the caller must tick the PPU and deliver
     * its pending IRQ (or return the completed frame) first.
     *
     * No device event occurs during the full trips preceding that final
     * instruction. HRAM cannot change there, and TIMA is disabled. */
    iterations = (deadline - 1U) / 28U;
    word cycles;
    word steps;
    if (iterations >= 16U) {
        iterations = 16U;
        cycles = prefix_cycles + 112U;
        steps = prefix_steps + 48U;
    }
    else {
        const word remaining = deadline - iterations * 28U;
        cycles = prefix_cycles + iterations * 7U;
        steps = prefix_steps + iterations * 3U;
        if (remaining <= 12U) {
            cycles += 3U;
            steps += 1U;
            mb->PC = (r16)(pc + 2U);
            mb->IR.raw = 0x00A7U;
            /* A single F0 must preserve the incoming deferred/raw flags. */
            if (iterations == 0U) {
                mb->reg.A = 0U;
                if (instructions != NULL) *instructions = steps;
                return cycles;
            }
        }
        else if (remaining <= 16U) {
            cycles += 4U;
            steps += 2U;
            mb->PC = (r16)(pc + 3U);
            mb->IR.raw = 0x0028U;
        }
        else {
            cycles += 7U;
            steps += 3U;
            /* Complete trip: the prefetched F0/PC remains unchanged. */
        }
    }
    fa.low = 0U;
    fa.high = MB_FLAG_H | MB_FLAG_Z;
    mb->reg.hilo16[3] = fa;
    mb->FMC_MODE = MB_FMC_MODE_NONE;
    if (instructions != NULL) *instructions = steps;
    return cycles;
#else
    /* Stop strictly before the next PPU transition. This leaves its IRQ and
     * all rendering work at the same completed-opcode boundary as normal. */
    iterations = (ppu->next_update_ticks - 1U) / 28U;
    if (iterations == 0U) {
        return 0U;
    }
    if (iterations > 16U) {
        iterations = 16U;
    }

    /* F0 reads zero; A7 sets H+Z and clears the deferred flag mode; the taken
     * JR returns to the same prefetched F0/PC state. No other CPU field moves. */
    fa.low = 0U;
    fa.high = MB_FLAG_H | MB_FLAG_Z;
    mb->reg.hilo16[3] = fa;
    mb->FMC_MODE = MB_FMC_MODE_NONE;
    return iterations;
#endif
#else
    (void)mb;
    (void)ppu;
    (void)fabric;
#if defined(KNOVGBC_IDLE_EVENT)
    (void)instructions;
#endif
    return 0U;
#endif
}
