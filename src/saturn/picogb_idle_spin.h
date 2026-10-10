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

#include "fabric.h"
#include "microcode.h"
#include "ppu.h"

/* Return complete, side-effect-free three-instruction polling iterations.
 * The caller must commit 7 machine cycles (28 PPU dots) per iteration before
 * executing another opcode. Zero means the ordinary interpreter must run. */
word picogb_idle_spin_try(mb_state *mb, const ppu_t *ppu,
    const struct pgf_userdata_t *fabric);

#if defined(KNOVGBC_IDLE_EVENT)
/* As above, but include the first completed instruction reaching a PPU
 * event. Return machine cycles, not trips. Commit devices immediately after
 * success and deliver their IRQ before another CPU instruction. The optional
 * instruction count is for host verification only; unchanged on rejection.
 * KNOVGBC_IDLE_RESUME additionally accepts the A7/JR suffix with verified
 * live CPU state. Its cap is that suffix plus 16 complete polling trips. */
word picogb_idle_event_try(mb_state *mb, const ppu_t *ppu,
    const struct pgf_userdata_t *fabric, word *instructions);
#endif

#if defined(KNOVGBC_IDLE_COUNTDOWN)
/* Recognize DEC A; JR NZ,-3 in mapped ROM or ordinary HRAM. Return M-cycles
 * through the first PPU-event boundary, or the terminal DEC (the untaken JR
 * remains in the interpreter), capped at 16 completed taken iterations.
 * Commit devices and deliver IRQ before another opcode. On rejection no CPU
 * state or instruction count is changed. The count may be NULL on Saturn. */
word picogb_idle_countdown_try(mb_state *mb, const ppu_t *ppu,
    const struct pgf_userdata_t *fabric, word *instructions);
#endif

#if defined(KNOVGBC_IDLE_STAT)
/* ROM-only F0 41 E6 03 20 FA. Accept modes 1..3 and standard STAT callback;
 * return M-cycles through the first event-crossing opcode, at most 16 taken
 * trips. Commit devices/IRQ before another opcode. Reject without mutation.
 * Called by idle_event_try on a rejected F0/HRAM pattern, not every opcode. */
word picogb_idle_stat_try(mb_state *mb, const ppu_t *ppu,
    const struct pgf_userdata_t *fabric, word *instructions);
#endif

#if defined(KNOVGBC_SUM_LOOP)
/* Exact ADD HL,DE; DEC B; JR NZ,-4, in ROM or ordinary HRAM. Return M-cycles
 * through the first event-crossing instruction, or terminal DEC, capped at
 * 16 taken trips. Commit devices and deliver IRQ before another opcode.
 * Reject without changing state; optional instruction counter is host-only. */
word picogb_sum_loop_try(mb_state *mb, const ppu_t *ppu,
    const struct pgf_userdata_t *fabric, word *instructions);
#endif

#if defined(KNOVGBC_IDLE_SPIN_CACHE)
/* Only immutable ROM instruction bytes may be remembered. Call this before
 * starting/resetting a machine and before replacing or patching its ROM,
 * including reusing the same ROM allocation for different contents. */
void picogb_idle_spin_cache_reset(void);

#if defined(KNOVGBC_IDLE_SPIN_CACHE_STATS)
/* Host verification only: these counters are absent from Saturn builds. */
extern word picogb_idle_spin_cache_hits;
extern word picogb_idle_spin_cache_misses;
#endif
#endif
