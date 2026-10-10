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
#pragma once

/*
 * Conservative event batching for the PicoGB frontend.
 *
 * The SM83 itself still executes exactly one instruction at a time.  What is
 * deferred is only the bookkeeping that normally follows every instruction:
 * timer progression and PPU progression.  The accumulator is flushed before
 * either device can produce an interrupt, and the frontend's I/O wrapper
 * flushes it before FF00-FF7F is observed or changed.
 *
 * This keeps the baseline's instruction-boundary timing while avoiding the
 * overwhelmingly-common timer/PPU calls that merely subtract a few ticks.
 * It is header-only so the Saturn frontend and the host differential test
 * exercise the identical policy.
 */

#include <stdbool.h>
#include <stdint.h>

#include "fabric.h"
#include "microcode.h"
#include "ppu.h"

struct picogb_event_scheduler {
    struct mb_state *cpu;
    struct ppu_t *ppu;
    struct pgf_userdata_t *fabric;

    uint32_t pending_cycles;
    word pending_ppu_irq;
    uint32_t ppu_event_cycles;
    uint32_t timer_event_cycles;
};

/* Return the number of SM83 machine cycles until TIMA next overflows.  Normal
 * TIMA increments are invisible until a read/write barrier; an overflow must
 * be committed before the following opcode can observe its interrupt. */
static inline uint32_t
picogb_event_scheduler_timer_deadline(
    const struct picogb_event_scheduler *scheduler)
{
    const struct pgf_userdata_t *const fabric = scheduler->fabric;

    if ((fabric->TIMER_CNT & 4U) == 0U) {
        return UINT32_MAX;
    }

    const uint32_t step = pgf_timer_step_periods[fabric->TIMER_CNT & 3U];
    return ((0x100U - fabric->TIMER_ACCUM) * step) -
        fabric->TIMER_SUB;
}

/* Device state changes only when we flush or use FF00-FF7F.  Calculate the
 * expensive absolute deadlines there, then let the hot per-opcode path merely
 * subtract its cycle count from two countdowns. */
static inline void
picogb_event_scheduler_reschedule(struct picogb_event_scheduler *scheduler)
{
    if ((scheduler->ppu->rLCDC & 0x80U) != 0U) {
        scheduler->ppu_event_cycles =
            (scheduler->ppu->next_update_ticks + 3U) >> 2;
    }
    else {
        scheduler->ppu_event_cycles = UINT32_MAX;
    }

    scheduler->timer_event_cycles =
        picogb_event_scheduler_timer_deadline(scheduler);
}

static inline void
picogb_event_scheduler_init(struct picogb_event_scheduler *scheduler,
    struct mb_state *cpu, struct ppu_t *ppu,
    struct pgf_userdata_t *fabric)
{
    scheduler->cpu = cpu;
    scheduler->ppu = ppu;
    scheduler->fabric = fabric;
    scheduler->pending_cycles = 0U;
    scheduler->pending_ppu_irq = 0U;
    picogb_event_scheduler_reschedule(scheduler);
}

/* A pending amount is always committed at an observable device boundary.
 * The fixed ceiling is only a defensive bound for LCD-off/timer-off code;
 * with the LCD enabled, PPU events impose a much smaller bound (<=114 M
 * cycles). */
static inline bool
picogb_event_scheduler_flush_due(
    const struct picogb_event_scheduler *scheduler)
{
    const uint32_t pending = scheduler->pending_cycles;

    if (pending == 0U) {
        return false;
    }

    /* An I/O write can schedule a STAT interrupt. The baseline calls
     * ppu_tick() after that same opcode, so it must not remain deferred until
     * the next raster transition. */
    if ((scheduler->ppu->rLCDC & 0x80U) != 0U &&
        scheduler->ppu->IF_SCHED != 0U) {
        return true;
    }

    if ((scheduler->ppu_event_cycles == 0U) ||
        (scheduler->timer_event_cycles == 0U)) {
        return true;
    }

    return pending >= 256U;
}

/* Commit deferred device time in the exact order used by the baseline loop.
 * PPU IRQs deliberately remain delayed until the top of the next SM83 step,
 * matching the existing frontend contract. */
static __attribute__((noinline, cold)) void
picogb_event_scheduler_flush(struct picogb_event_scheduler *scheduler)
{
    const uint32_t pending = scheduler->pending_cycles;

    if (pending != 0U) {
        scheduler->cpu->DIV += pending;
        pgf_timer_update(scheduler->fabric, pending);

        if ((scheduler->ppu->rLCDC & 0x80U) != 0U) {
            scheduler->pending_ppu_irq |=
                ppu_tick(scheduler->ppu, pending << 2);
        }

        scheduler->pending_cycles = 0U;
    }

    picogb_event_scheduler_reschedule(scheduler);
}

static inline void
picogb_event_scheduler_advance(struct picogb_event_scheduler *scheduler,
    word cycles)
{
    scheduler->pending_cycles += cycles;

    if (scheduler->ppu_event_cycles != UINT32_MAX) {
        scheduler->ppu_event_cycles = (cycles >= scheduler->ppu_event_cycles) ?
            0U : scheduler->ppu_event_cycles - cycles;
    }

    if (scheduler->timer_event_cycles != UINT32_MAX) {
        scheduler->timer_event_cycles =
            (cycles >= scheduler->timer_event_cycles) ? 0U :
            scheduler->timer_event_cycles - cycles;
    }
}

static inline word
picogb_event_scheduler_take_ppu_irq(
    struct picogb_event_scheduler *scheduler)
{
    const word pending = scheduler->pending_ppu_irq;
    scheduler->pending_ppu_irq = 0U;
    return pending;
}
