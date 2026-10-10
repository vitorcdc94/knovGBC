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
 * Live Slave-SH-2 PPU shadow experiment.
 *
 * The Master remains the only authoritative Game Boy and VDP2 producer. The
 * Slave receives a recorded visual frame, redraws it into a private buffer,
 * and reports whether it agrees. None of these functions may change the
 * emulated CPU, timing, IRQ state, or the framebuffer sent to VDP2.
 */

#include <stdbool.h>
#include <stdint.h>

#include "types.h"

struct ppu_t;

bool picogb_slave_ppu_shadow_init(void);
void picogb_slave_ppu_shadow_poll(void);
void picogb_slave_ppu_shadow_frame_complete(const pixel_t *frame,
    uint32_t stride_pixels, bool rendered);

/* Hooks called by PicoGB only in the dedicated shadow target. */
void knovgbc_slave_ppu_shadow_frame_begin(const struct ppu_t *ppu);
void knovgbc_slave_ppu_shadow_line(const struct ppu_t *ppu);
void picogb_slave_ppu_shadow_vram_write(void *userdata, word offset);
void picogb_slave_ppu_shadow_oam_write(void *userdata, word offset);
