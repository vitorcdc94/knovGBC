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
#include "picogb_vdp12_frame.h"

#include "ppu.h"

#include <string.h>

/* The Master SH-2 owns this trace. It is filled at the same mode-3 -> HBlank
 * transition at which PicoGB normally writes the software framebuffer. */
static picogb_vdp12_frame_t captured_frame;
static const uint8_t pixel_masks[8] = {
    0x80U, 0x40U, 0x20U, 0x10U, 0x08U, 0x04U, 0x02U, 0x01U
};

void
picogb_vdp12_frame_reset(void)
{
    /* Every 4-bpp character row is replaced on a complete visual frame.
     * Clearing 11.5 KiB of character data on every logical frame would add
     * work even on the skipped half of the frameskip schedule. */
    captured_frame.sprite_count = 0U;
    memset(captured_frame.captured_lines, 0,
        sizeof(captured_frame.captured_lines));
    captured_frame.valid = 1U;
}

const picogb_vdp12_frame_t *
picogb_vdp12_frame_get(void)
{
    /* A partial visual pass cannot be presented as a full 160x144 image.
     * This also catches an unexpected frontend/PPU frameskip mismatch. */
    for (unsigned int row = 0U; row < sizeof(captured_frame.captured_lines);
         row++) {
        if (captured_frame.captured_lines[row] != 0xFFU) {
            captured_frame.valid = 0U;
            break;
        }
    }
    return &captured_frame;
}

static inline uint8_t
dmg_shade(uint8_t palette, unsigned int raw_colour)
{
    /* The SH-2 has no variable-count shift instruction. Select the two
     * palette bits with constant shifts, as the existing PPU does. */
    if (raw_colour & 2U) palette >>= 4U;
    if (raw_colour & 1U) palette >>= 2U;
    unsigned int mapped = palette & 3U;
#if CONFIG_PPU_INVERT
    mapped ^= 3U;
#endif
    /* Colour zero belongs to transparent VDP1 sprite pixels. Even DMG shade
     * zero is therefore placed at Saturn palette index one. */
    return (uint8_t)(mapped + 1U);
}

static inline unsigned int
dmg_tile_slot(unsigned int lcdc, uint8_t tile_number)
{
    return (lcdc & 0x10U) ? tile_number :
        (0x80U + ((unsigned int)tile_number ^ 0x80U));
}

static void
capture_background_line(const struct ppu_t *ppu, unsigned int y)
{
    const unsigned int lcdc = ppu->rLCDC;
    const unsigned int bg_map = 0x1800U | ((lcdc & 0x08U) << 7U);
    const unsigned int window_map = 0x1800U | ((lcdc & 0x40U) << 4U);
    const unsigned int bg_y = (y + ppu->rSCY) & 0xFFU;
    const unsigned int window_y = ppu->_internal_WY;
    const int window_x = (int)ppu->rWX - 7;
    const int window_enabled = ((lcdc & 0x20U) != 0U) &&
        (ppu->rWY <= y) && (ppu->rWX < 168U);
    const unsigned int tile_row = (y >> 3U) * PICOGB_VDP12_TILE_COLS;
    const unsigned int byte_row = (y & 7U) << 2U;
    const uint8_t *const bg_map_row =
        &ppu->VRAM[bg_map | ((bg_y >> 3U) << 5U)];
    const uint8_t *const window_map_row =
        &ppu->VRAM[window_map | (((window_y >> 3U) & 31U) << 5U)];
    unsigned int cached_tile_key = ~0U;
    unsigned int low = 0U;
    unsigned int high = 0U;

    for (unsigned int tile_x = 0U;
         tile_x < PICOGB_VDP12_TILE_COLS; tile_x++) {
        uint8_t *const target =
            &captured_frame.bg_tiles[tile_row + tile_x][byte_row];
        const unsigned int visible_x = tile_x << 3U;

        for (unsigned int pair = 0U; pair < 4U; pair++) {
            unsigned int x = visible_x + (pair << 1U);
            uint8_t colours[2];

            for (unsigned int pixel = 0U; pixel < 2U; pixel++, x++) {
                const unsigned int use_window =
                    window_enabled && ((int)x >= window_x);
                if (!use_window && ((lcdc & 1U) == 0U)) {
                    /* PicoGB deliberately leaves the previous framebuffer
                     * pixel intact when BG is disabled. A freshly generated
                     * native tile cannot represent that retained composite,
                     * which could include old sprites. Report the frame as
                     * invalid; there is no implicit presentation fallback. */
                    captured_frame.valid = 0U;
                    colours[pixel] = 1U;
                    continue;
                }

                const unsigned int source_x = use_window ?
                    (unsigned int)((int)x - window_x) :
                    ((x + ppu->rSCX) & 0xFFU);
                const unsigned int tile_key =
                    (use_window << 8U) | (source_x >> 3U);
                if (tile_key != cached_tile_key) {
                    const uint8_t *const map_row = use_window ?
                        window_map_row : bg_map_row;
                    const unsigned int source_y = use_window ?
                        window_y : bg_y;
                    const unsigned int tile = dmg_tile_slot(lcdc,
                        map_row[(source_x >> 3U) & 31U]);
                    const unsigned int offset = (tile << 4U) |
                        ((source_y & 7U) << 1U);
                    low = ppu->VRAM[offset];
                    high = ppu->VRAM[offset + 1U];
                    cached_tile_key = tile_key;
                }

                const unsigned int mask = pixel_masks[source_x & 7U];
                const unsigned int raw_colour =
                    ((low & mask) != 0U) |
                    (((high & mask) != 0U) << 1U);
                colours[pixel] =
                    dmg_shade((uint8_t)ppu->rBGP, raw_colour);
            }

            target[pair] = (uint8_t)((colours[0] << 4U) | colours[1]);
        }
    }
}

static void
capture_sprite_line(const struct ppu_t *ppu, unsigned int y)
{
    const unsigned int lcdc = ppu->rLCDC;
    if ((lcdc & 2U) == 0U) return;

    /* PicoGB's mode-2 pass has already chosen at most ten OAM entries and
     * latched each X/index pair. Walk them in the same reverse order as the
     * existing compositor so the lower selected OAM index is painted last. */
    for (unsigned int slot = PPU_N_OBJS; slot != 0U;) {
        const hilow16_t latch = ppu->state.latches[--slot];
        const unsigned int oam_x = latch.low;
        const unsigned int oam_index = latch.high;
        if ((oam_x == 0U) || (oam_x >= 168U) ||
            (oam_index >= 40U)) continue;

        const uint8_t *const object = &ppu->OAM[oam_index << 2U];
        int row = (int)y + 16 - (int)object[0];
        const unsigned int height = (lcdc & 4U) ? 16U : 8U;
        if ((row < 0) || ((unsigned int)row >= height)) continue;

        unsigned int tile = object[2];
        const unsigned int attr = object[3];
        if (height == 16U) tile &= 0xFEU;
        if (attr & 0x40U) row = (int)height - 1 - row;
        tile += (unsigned int)row >> 3U;

        const unsigned int address = (tile << 4U) |
            (((unsigned int)row & 7U) << 1U);
        const unsigned int low = ppu->VRAM[address];
        const unsigned int high = ppu->VRAM[address + 1U];
        const uint8_t palette = (uint8_t)((attr & 0x10U) ?
            ppu->rOBP1 : ppu->rOBP0);

        if (captured_frame.sprite_count >= PICOGB_VDP12_MAX_SPRITE_STRIPS) {
            captured_frame.valid = 0U;
            return;
        }

        picogb_vdp12_sprite_strip_t *const strip =
            &captured_frame.sprite_strips[captured_frame.sprite_count];
        strip->x = (int16_t)((int)oam_x - 8);
        strip->y = (uint8_t)y;
        strip->oam_index = (uint8_t)oam_index;
        strip->pixels[4] = 0U;
        strip->pixels[5] = 0U;
        strip->pixels[6] = 0U;
        strip->pixels[7] = 0U;

        uint8_t any_opaque = 0U;
        for (unsigned int pair = 0U; pair < 4U; pair++) {
            uint8_t colours[2];
            for (unsigned int pixel = 0U; pixel < 2U; pixel++) {
                const unsigned int sprite_x = (pair << 1U) + pixel;
                const unsigned int mask = (attr & 0x20U) ?
                    pixel_masks[7U - sprite_x] : pixel_masks[sprite_x];
                const unsigned int raw_colour =
                    ((low & mask) != 0U) |
                    (((high & mask) != 0U) << 1U);
                colours[pixel] = raw_colour == 0U ? 0U :
                    dmg_shade(palette, raw_colour);
                any_opaque |= colours[pixel];
            }
            strip->pixels[pair] =
                (uint8_t)((colours[0] << 4U) | colours[1]);
        }

        /* An all-zero strip has no visual contribution and need not occupy a
         * VDP1 command. This does not alter the order of visible sprites. */
        if (any_opaque != 0U) captured_frame.sprite_count++;
    }
}

void
picogb_vdp12_capture_scanline(const struct ppu_t *ppu)
{
    if (ppu == NULL) {
        captured_frame.valid = 0U;
        return;
    }

    const unsigned int y = ppu->state.scanY;
    if (y >= 144U) return;
    if (ppu->is_cgb != 0U) {
        captured_frame.valid = 0U;
        return;
    }

    capture_background_line(ppu, y);
    capture_sprite_line(ppu, y);
    captured_frame.captured_lines[y >> 3U] |=
        (uint8_t)(1U << (y & 7U));
}
