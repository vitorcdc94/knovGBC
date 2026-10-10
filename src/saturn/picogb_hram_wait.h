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

#include "picogb_idle_spin.h"

/* Private to the single-threaded DMG frontend. Enter after delivering pending
 * PPU IRQs. Success commits CPU time and devices itself, leaving frame/IRQ
 * handling to the caller; do not commit the returned cycles a second time.
 * No recognition survives return. Optional counters are for host tests only.
 * Output pointers must refer to separate caller storage, not CPU/device state
 * or emulated memory. Rejection changes nothing. */
#define PICOGB_HRAM_WAIT_MAX_GROUPS 512U
word picogb_hram_wait_try(mb_state *mb, ppu_t *ppu,
    struct pgf_userdata_t *fabric, word *pending_ppu_irq,
    word *instructions, word *groups);
