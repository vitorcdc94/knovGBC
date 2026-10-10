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

#include <stdint.h>

/* One Saturn 4-bpp character per visible 8x8 position.  A unique character
 * for each screen position lets later Game Boy scanlines retain VRAM,
 * scrolling and palette changes made in the middle of a visual frame. */
#define PICOGB_VDP12_TILE_COLS 20U
#define PICOGB_VDP12_TILE_ROWS 18U
#define PICOGB_VDP12_TILE_BYTES 32U
#define PICOGB_VDP12_TILE_COUNT \
    (PICOGB_VDP12_TILE_COLS * PICOGB_VDP12_TILE_ROWS)
#define PICOGB_VDP12_MAX_SPRITE_STRIPS (144U * 10U)

typedef struct picogb_vdp12_sprite_strip {
    /* Visible Game Boy coordinates. X may be negative for a clipped sprite. */
    int16_t x;
    uint8_t y;
    uint8_t oam_index;
    /* Eight 4-bpp pixels, high nibble first. The last four bytes pad an 8x1
     * VDP1 texture to an eight-byte address boundary. */
    uint8_t pixels[8];
} picogb_vdp12_sprite_strip_t;

typedef struct picogb_vdp12_frame {
    /* Tile index = (y / 8) * 20 + (x / 8). Each row occupies four bytes.
     * BG/window pixels are final DMG shades 1..4; zero is reserved. */
    uint8_t bg_tiles[PICOGB_VDP12_TILE_COUNT][PICOGB_VDP12_TILE_BYTES];
    /* Sprite pixels are zero when transparent, otherwise shade + 1. Entries
     * are already in drawing order: higher selected OAM indices first. */
    picogb_vdp12_sprite_strip_t
        sprite_strips[PICOGB_VDP12_MAX_SPRITE_STRIPS];
    uint16_t sprite_count;
    uint8_t captured_lines[18];
    /* Zero means the native compositor cannot safely present this frame.
     * Build 049 keeps its previous native image; it has no software fallback. */
    uint8_t valid;
} picogb_vdp12_frame_t;

struct ppu_t;

void picogb_vdp12_frame_reset(void);
const picogb_vdp12_frame_t *picogb_vdp12_frame_get(void);
void picogb_vdp12_capture_scanline(const struct ppu_t *ppu);
