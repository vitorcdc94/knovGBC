/* knovGBC PicoGB library extension/snapshot; notice updated 2026-10-10.
 * This source extends/adapts the PicoGB library based on upstream commit
 * 325a12a25e2d1f4799424cc18c0e69a7810695b0.
 * The preserved local implementation predates the 2026-10-07 inventory.
 * Exact historical code-edit dates are not established by surviving records.
 * Only this source-handoff notice was changed on 2026-10-10.
 * HRAM batching evidence: docs/history/kmvpe041.md and
 * docs/history/PPU_FIXED_COMPARISON_042_043.md, dated 2026-09-25.
 * See third_party/PicoGB/KNOVGBC_CHANGES.md for dated validation evidence;
 * report dates are not claims that every code change occurred on those dates.
 * Distributed with the modified library under GNU LGPL v2.1.
 * See third_party/PicoGB/LICENSE.txt for the library license.
 * Original upstream license and copyright notices remain applicable.
 */
#include "picogb_hram_wait.h"

#include <stddef.h>

#if !defined(KNOVGBC_HRAM_WAIT) || !defined(KNOVGBC_IDLE_RESUME) || \
    !defined(KNOVGBC_IDLE_COUNTDOWN) || !CONFIG_USE_FLAT_ROM || \
    !CONFIG_NOBOOTMEME || CONFIG_ENABLE_LRU || CONFIG_IS_CGB
#error "HRAM wait requires the 033 flat-ROM DMG baseline"
#endif
#if defined(KNOVGBC_IDLE_STAT) || defined(KNOVGBC_IDLE_SPIN_CACHE) || \
    defined(KNOVGBC_SUM_LOOP) || defined(KNOVGBC_SCHEDULER_BATCH) || \
    defined(KNOVGBC_SLAVE_PPU_SHADOW) || defined(KNOVGBC_OPCODE_PROFILE) || \
    defined(KNOVGBC_COMPONENT_PROFILE)
#error "HRAM wait is an isolated 033 experiment"
#endif
#if defined(KNOVGBC_PPU_EVENT_COALESCE) && PPU_SCANLINE_UPDATES
#error "PPU event coalescing cannot cross scanline redraw callbacks"
#endif
#if defined(KNOVGBC_PPU_TWO_LINES) && !defined(KNOVGBC_PPU_EVENT_COALESCE)
#error "Two-line PPU batching requires the 039 coalescing baseline"
#endif
#if defined(KNOVGBC_PPU_ADAPTIVE_BATCH) && !defined(KNOVGBC_PPU_TWO_LINES)
#error "Adaptive PPU batching requires the 040 two-line baseline"
#endif
#if defined(KNOVGBC_PPU_FIXED_LINES)
#if !defined(KNOVGBC_PPU_TWO_LINES) || defined(KNOVGBC_PPU_ADAPTIVE_BATCH)
#error "Fixed-line PPU batching requires the 040 baseline, not 041"
#endif
#if KNOVGBC_PPU_FIXED_LINES != 4 && KNOVGBC_PPU_FIXED_LINES != 8
#error "Fixed-line PPU batching supports only four or eight lines"
#endif
#endif

/* The same event-boundary arithmetic as 033, after its full admission check.
 * This is NOT a general opcode executor: only the zero HRAM polling loop may
 * enter. No CPU instruction runs between groups and this target's PPU only
 * modifies PPU state/framebuffer, so neither HRAM nor ROM mapping can change.
 * Interrupts and frame returns end the invocation before that can cease to
 * hold. Nothing is remembered across invocations. */
static inline word
wait_advance(mb_state *mb, word pc, word deadline, word *instructions,
    word max_iterations)
{
    word prefix_cycles = 0U, prefix_steps = 0U;
    hilow16_t fa;

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

    word iterations = (deadline - 1U) / 28U;
    word cycles, steps;
    if (iterations >= max_iterations) {
        cycles = prefix_cycles + max_iterations * 7U;
        steps = prefix_steps + max_iterations * 3U;
    }
    else {
        const word remaining = deadline - iterations * 28U;
        cycles = prefix_cycles + iterations * 7U;
        steps = prefix_steps + iterations * 3U;
        if (remaining <= 12U) {
            cycles += 3U;
            ++steps;
            mb->PC = (r16)(pc + 2U);
            mb->IR.raw = 0x00A7U;
            if (iterations == 0U) {
                mb->reg.A = 0U; /* F0 alone preserves deferred flags. */
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
        }
    }
    fa.low = 0U;
    fa.high = MB_FLAG_H | MB_FLAG_Z;
    mb->reg.hilo16[3] = fa;
    mb->FMC_MODE = MB_FMC_MODE_NONE;
    if (instructions != NULL) *instructions = steps;
    return cycles;
}

#if defined(KNOVGBC_PPU_EVENT_COALESCE)
/* From the start of mode 3, finish one or more visible scanlines in one PPU
 * tick. ppu_tick() still executes every intermediate transition in order.
 * No IRQ or frame may become visible within the skipped CPU loop; every
 * unsafe state retains the 039 one-line or original one-event path. */
static inline word
wait_deadline(const ppu_t *ppu
#if defined(KNOVGBC_PPU_FIXED_LINES)
    , word *max_iterations
#endif
    )
{
    const word ticks = ppu->next_update_ticks;
#if defined(KNOVGBC_PPU_FIXED_LINES)
    *max_iterations = 16U;
#endif
    if (ppu->next_update_n == 1U && ticks <= 80U &&
#if defined(KNOVGBC_PPU_ADAPTIVE_BATCH)
        ppu->state.scanY < 144U &&
#else
        ppu->state.scanY < 143U &&
#endif
        (ppu->rSTAT & 0x78U) == 0U &&
        ppu->IF_SCHED == 0U) {
#if defined(KNOVGBC_PPU_ADAPTIVE_BATCH)
        /* The next CPU-visible PPU event is entry to LY=144 (VBlank and
         * frame completion). With every STAT IRQ source disabled, each
         * intervening visible line is internal to the PPU. Stop at the
         * first CPU instruction boundary crossing that event; the maximum
         * overshoot is less than a single VBlank scanline. */
        return ticks + 168U + 208U +
            (143U - ppu->state.scanY) * 456U;
#else
#if defined(KNOVGBC_PPU_FIXED_LINES) && KNOVGBC_PPU_FIXED_LINES == 8
        /* Eight lines end at the start of Y+8, still before VBlank. */
        if (ppu->state.scanY < 136U) {
            *max_iterations = 131U;
            return ticks + 3568U;
        }
#endif
#if defined(KNOVGBC_PPU_FIXED_LINES)
        /* Near VBlank, fall back to four, then two, then one line. */
        if (ppu->state.scanY < 140U) {
            *max_iterations = 66U;
            return ticks + 1744U;
        }
#endif
#if defined(KNOVGBC_PPU_TWO_LINES)
        /* End at the start of line Y+2. Y<=141 leaves at least one full
         * visible line before VBlank, even after instruction overshoot. */
        if (ppu->state.scanY < 142U) {
#if defined(KNOVGBC_PPU_FIXED_LINES)
            *max_iterations = 33U;
#endif
            return ticks + 168U + 208U + 80U + 168U + 208U;
        }
#endif
#if defined(KNOVGBC_PPU_FIXED_LINES)
        *max_iterations = 17U;
#endif
        return ticks + 168U + 208U;
#endif
    }
    return ticks;
}
#endif

/* Keep this entry out of cross-unit specialization: the SH-2 LTO build
 * otherwise replaces its call with a constant rejection (verified in the
 * final map/assembly). This is local to the new helper; -O3/LTO and the 033
 * interpreter remain unchanged. Its runtime guards must remain executable. */
word __attribute__((noipa))
picogb_hram_wait_try(mb_state *mb, ppu_t *ppu,
    struct pgf_userdata_t *fabric, word *pending_ppu_irq,
    word *instructions, word *groups)
{
    if (mb == NULL || ppu == NULL || fabric == NULL ||
        pending_ppu_irq == NULL || *pending_ppu_irq != 0U ||
        fabric->mb != mb || fabric->ppu != ppu) {
        return 0U;
    }

    word step_count = 0U;
    word cycles = picogb_idle_event_try(mb, ppu, fabric,
        instructions != NULL ? &step_count : NULL);
    if (cycles == 0U) return 0U;

    /* Normalize from the first group's already-prefetched next instruction. */
    const word pc = mb->PC - (mb->IR.raw == 0x00A7U ? 2U :
        mb->IR.raw == 0x0028U ? 3U : 0U);
    word total_cycles = 0U, total_steps = 0U, completed = 0U;

    for (;;) {
        total_cycles += cycles;
        total_steps += step_count;
        ++completed;
        mb->DIV += cycles;
        pgf_timer_update(fabric, cycles);
#if CONFIG_APU_ENABLE
        /* A shortcut removes interpreter work, not emulated audio time. */
        apu_tick(fabric->apu, cycles, 1);
#endif
        *pending_ppu_irq |= ppu_tick(ppu, cycles << 2);

        /* Match the frontend: a frame return has priority over delivery of
         * the IRQ produced by that same event. No guest instruction executes
         * after an enabled IRQ. The caller retains ownership of pending IRQ
         * on EVERY return, including the bounded-work fallback. */
        if (ppu->_redrawed != 0U ||
            (mb->IME != 0U &&
                (((mb->IF | *pending_ppu_irq) & mb->IE) != 0U)) ||
            completed == PICOGB_HRAM_WAIT_MAX_GROUPS ||
            ppu->next_update_ticks == 0U) {
            break;
        }

        mb->IF |= *pending_ppu_irq;
        *pending_ppu_irq = 0U;
#if defined(KNOVGBC_PPU_EVENT_COALESCE)
#if defined(KNOVGBC_PPU_FIXED_LINES)
        word max_iterations;
        const word deadline = wait_deadline(ppu, &max_iterations);
#else
        const word deadline = wait_deadline(ppu);
#endif
#if defined(KNOVGBC_PPU_ADAPTIVE_BATCH)
        /* Allow the entire proven event-free interval in a single group.
         * The ordinary one-event fallback retains its original cap. */
        const word max_iterations = deadline == ppu->next_update_ticks ?
            16U : (deadline - 1U) / 28U + 1U;
#elif defined(KNOVGBC_PPU_FIXED_LINES)
        /* Constant caps avoid a division on the SH-2 hot path. */
#elif defined(KNOVGBC_PPU_TWO_LINES)
        const word max_iterations = deadline == ppu->next_update_ticks ?
            16U : (deadline > 456U ? 33U : 17U);
#else
        const word max_iterations =
            deadline == ppu->next_update_ticks ? 16U : 17U;
#endif
        cycles = wait_advance(mb, pc, deadline,
            instructions != NULL ? &step_count : NULL, max_iterations);
#else
        cycles = wait_advance(mb, pc, ppu->next_update_ticks,
            instructions != NULL ? &step_count : NULL, 16U);
#endif
    }

    if (instructions != NULL) *instructions = total_steps;
    if (groups != NULL) *groups = completed;
    return total_cycles;
}
