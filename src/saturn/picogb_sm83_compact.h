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
 * Compact, portable DMG SM83 decoder used by the Saturn MVP experiment.
 *
 * It deliberately shares PicoGB's mb_state and memory-dispatch ABI rather
 * than introducing a Tetris-specific CPU state.  The reference mb_exec()
 * remains available to host differential tests; the production target calls
 * this function only when KNOVGBC_SM83_COMPACT is enabled.
 */

#include "microcode.h"

word picogb_sm83_compact_exec(mb_state *mb);

