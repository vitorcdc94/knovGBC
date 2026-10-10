/* knovGBC modification notice updated 2026-10-10.
 * This file differs from upstream PicoGB commit
 * 325a12a25e2d1f4799424cc18c0e69a7810695b0.
 * The preserved local code changes predate the 2026-10-07 inventory.
 * Exact historical code-edit dates are not established by surviving records.
 * Only this source-handoff notice was changed on 2026-10-10.
 * Tile-dirty evidence: tools/tests/picogb_055_validation.md,
 * completed 2026-10-03.
 * See third_party/PicoGB/KNOVGBC_CHANGES.md for dated validation evidence;
 * report dates are not claims that every code change occurred on those dates.
 * PicoGB and this modified library remain under GNU LGPL v2.1.
 * This notice does not replace upstream license or copyright notices.
 */
#include "ppu.h"

#include <assert.h>

#if defined(KNOVGBC_VDP12_NATIVE)
#include "picogb_vdp12_frame.h"
#endif

#if PPU_DMG_FAST_RENDERER || PPU_DMG_DIRTY_SCANLINES
#include <string.h>
#endif

#if PPU_DMG_FAST_RENDERER
#include "picogb_ppu_tile_copy.h"
#endif

#define self ppu_t* __restrict
#define USE_STATE struct ppu_state_t* __restrict ps = &pp->state;

//#define PPU_DEBUG


#if !CONFIG_IS_CGB
#define IS_CGB 0
//#elif CONFIG_FORCE_ENABLE_CGB
//#define IS_CGB 1
#else
#define IS_CGB (pp->is_cgb)
#endif

#if PPU_MODE == 1
#define DITHER_PATTERN (((dstX & 1) << 9) | ((dstY & 1) << 10))
#define DITHER_INDEX ((attr >> 9) & 3)
#elif PPU_MODE == 3
#define DITHER_PATTERN (((srcY & 1) << 10))
#define DITHER_INDEX ((attr >> 9) & 3)
#else
#define DITHER_PATTERN 0
#define DITHER_INDEX 0
#endif

#if PPU_IS_MONOCHROME
#define SCO_CALC ((dstX & 7) << 11)
#define SCO ((attr >> 11) & 7)
#else
#define SCO_CALC 0
#define SCO 0
#endif

#ifdef PPU_DEBUG
#define RENDER_WIDTH 240
#else
#define RENDER_WIDTH 168
#endif


#define UPDATE_0 0
#define UPDATE_1 80
#define UPDATE_2 (80 + 168)
#define UPDATE_3 456

#define crossed(tresh,prev,val) (((prev)<(tresh))&&((val)>=(tresh)))

#if PPU_DMG_FAST_RENDERER
PGB_FUNC static inline void
ppu_dmg_mark_line_dirty(self pp, word line)
{
    if (line < PPU_DMG_VISIBLE_LINES) {
        pp->dmg_dirty_lines[line >> 3] |= (r8)(1U << (line & 7U));
    }
}

PGB_FUNC static inline wbool
ppu_dmg_line_is_dirty(const self pp, word line)
{
    return (pp->dmg_dirty_lines[line >> 3] &
        (r8)(1U << (line & 7U))) != 0U;
}

PGB_FUNC static inline void
ppu_dmg_clear_line_dirty(self pp, word line)
{
    pp->dmg_dirty_lines[line >> 3] &=
        (r8)~(1U << (line & 7U));
}

PGB_FUNC static void
ppu_dmg_mark_all_dirty(self pp)
{
    memset(pp->dmg_dirty_lines, 0xFF, sizeof(pp->dmg_dirty_lines));
}

PGB_FUNC static void
ppu_dmg_reset_derived_cache(self pp)
{
    memset(pp->dmg_tile_valid, 0, sizeof(pp->dmg_tile_valid));
    memset(pp->dmg_bg_valid, 0, sizeof(pp->dmg_bg_valid));
    ppu_dmg_mark_all_dirty(pp);
    pp->dmg_frame_changed = 0U;
    pp->state.dmg_sprite_count = 0U;
    pp->state.dmg_sprite_latches_dirty = 0U;
}
#endif

#if PPU_DMG_DIRTY_SCANLINES
#if defined(KNOVGBC_PPU_TILE_DIRTY_LINES)
PGB_FUNC static inline void
ppu_dmg_indexed_note_tile(self pp, word line, word tile)
{
    pp->dmg_indexed_line_tiles[line][tile >> 3] |=
        (r8)(1U << (tile & 7U));
}

PGB_FUNC static inline word
ppu_dmg_indexed_bg_tile_index(const self pp, word index)
{
    /* LCDC bit 4 selects unsigned 0000-0FFF or signed 0800-17FF data.
     * Translate the map byte to the physical 16-byte VRAM pattern slot. */
    return ((pp->rLCDC & 0x10U) == 0U && index < 0x80U)
        ? index + 0x100U : index;
}
#endif

PGB_FUNC static inline void
ppu_dmg_indexed_mark_line_dirty(self pp, word line)
{
    if (line < PPU_DMG_INDEXED_VISIBLE_LINES) {
        pp->dmg_indexed_dirty_lines[line >> 3] |=
            (r8)(1U << (line & 7U));
    }
}

PGB_FUNC static inline wbool
ppu_dmg_indexed_line_is_dirty(const self pp, word line)
{
    return (pp->dmg_indexed_dirty_lines[line >> 3] &
        (r8)(1U << (line & 7U))) != 0U;
}

PGB_FUNC static inline void
ppu_dmg_indexed_clear_line_dirty(self pp, word line)
{
    pp->dmg_indexed_dirty_lines[line >> 3] &=
        (r8)~(1U << (line & 7U));
}

PGB_FUNC static void
ppu_dmg_indexed_mark_all_dirty(self pp)
{
    memset(pp->dmg_indexed_dirty_lines, 0xFF,
        sizeof(pp->dmg_indexed_dirty_lines));
}

PGB_FUNC static inline void
ppu_dmg_indexed_mark_sprite_rows(self pp, const r8 *sprite)
{
    const int first = (int)sprite[0] - 16;
    const int height = (pp->rLCDC & 4U) ? 16 : 8;

    for (int row = 0; row != height; row++) {
        const int line = first + row;
        if ((line >= 0) && (line < PPU_DMG_INDEXED_VISIBLE_LINES)) {
            ppu_dmg_indexed_mark_line_dirty(pp, (word)line);
        }
    }
}
#endif

PGB_FUNC void ppu_reset(self pp)
{
    USE_STATE;
    
    pp->next_update_n = 3;
    pp->next_update_ticks = 0;
    
    ps->scanX = 0;
    ps->scanY = 0;
    ps->subclk = 0;
    
    ps->posX = 0;
    
    pp->_internal_WY = 0;

    #if PPU_DMG_FAST_RENDERER
    /* The framebuffer is no longer guaranteed to reflect a newly started
     * LCD sequence, even when its tile cache remains valid. */
    ppu_dmg_mark_all_dirty(pp);
    #endif
#if PPU_DMG_DIRTY_SCANLINES
    ppu_dmg_indexed_mark_all_dirty(pp);
    pp->dmg_indexed_oam_valid = 0U;
#endif
    
    pp->rSTAT = (pp->rSTAT & 0xFC) | 2;
}

PGB_FUNC void ppu_initialize(self pp)
{
    pp->is_cgb = 0;
    
    pp->rLYC = 0;
    
    pp->rLCDC = 0;
    pp->rSTAT = 0x80;
    pp->rSCX = 0;
    pp->rSCY = 0;
    pp->rWX = 0;
    pp->rWY = 0;
    
    pp->rOBPI = 0;
    pp->rBGPI = 0;
    
    #if PPU_INTERLACE == 1
    pp->state.interlace = 0b11;
    #elif PPU_INTERLACE == 2
    pp->state.interlace = 0b01;
    #else
    pp->state.interlace = ~0u;
    #endif

    #if PPU_DMG_FAST_RENDERER
    ppu_dmg_reset_derived_cache(pp);
    #endif

#if defined(KNOVGBC_PPU_PALETTE_CACHE)
    ppu_dmg_palette_cache_refresh(pp);
#endif
    
    ppu_reset(pp);
}

PGB_FUNC void ppu_turn_off(self pp)
{
    USE_STATE;
    
    pp->rSTAT &= 0xFC;
    pp->_internal_WY = 0;
    
    ps->scanY = 0;
    ps->scanX = 0;
    ps->subclk = 0;
}

PGB_FUNC static inline __attribute__((optimize("O2"))) const r8* __restrict ppu_resolve_line_tile(const self pp, word idx, word line)
{
    const r8* __restrict ret;
    var LCDC;
    var magic;
    
    // each 8px tile line is two bytes
    line <<= 1;
    COMPILER_VARIABLE_BARRIER(line);
    
    // offset VRAM by selected tile line
    ret = &pp->VRAM[line];
    COMPILER_VARIABLE_BARRIER(ret);
    
    // LCDC = (LCDC & (1 << 4)) ? 0 : (1 << 8);
    // The addition works as XOR, as only that single bit is needed
    LCDC = (pp->rLCDC + (1 << 4)) << (31 - 4) >> 31 << 8;
    COMPILER_VARIABLE_BARRIER(LCDC);
    
    // magic = idx.7 NOR LCDC.4
    // magic = (idx & 0x80) ? 0 : (1 << 8) | garbage;
    // Since LCDC here is only a single bit, it will clear the other garbage
    magic = (idx - (1 << 7));
    magic &= LCDC;
    
    // Add in real index
    magic |= idx;
    
    // Each tile is 16bytes long, offset VRAM by real tile index
    return ret + (magic << 4);
    
    // The non-obfuscated version:
    
    /*
    if(!(pp->rLCDC & (1 << 4)))
    {
        var midx;
        if(idx < 0x80)
            midx = (idx + 0x100);
        else
            midx = idx;
        return &pp->VRAM[(midx << 4) | (line << 1)];
    }
    else
        return &pp->VRAM[(idx << 4) | (line << 1)];
    */
}

PGB_FUNC static inline const r8* __restrict ppu_resolve_line_sprite(const self pp, word idx, word line)
{
    return &pp->VRAM[(idx << 4) | (line << 1)];
}

#if CONFIG_IS_CGB
PGB_FUNC static inline const r8* __restrict ppu_resolve_line_sprite_cgb1(const self pp, word idx, word line)
{
    return &pp->VRAM[((idx << 4) | (line << 1)) | 0x2000];
}
#endif

#if PPU_MODE == 0
#include "ppu/ppu.RGBA8.c"
#elif PPU_MODE == 1 || PPU_MODE == 2 || PPU_MODE == 3
#include "ppu/ppu.1bpp.c"
#elif PPU_MODE == 4
#include "ppu/ppu.RGBI_44.c"
#elif PPU_MODE == 5
#include "ppu/ppu.RGBI_8.c"
#elif PPU_MODE == 6
#include "ppu/ppu.RGB565.c"
#elif PPU_MODE == 7
#include "ppu/ppu.RGB1555.c"
#elif PPU_MODE == 8
#include "ppu/ppu.VDP2_INDEX8.c"
#else
#error Undefined PPU mode
#endif

#if defined(KNOVGBC_PPU_PALETTE_CACHE)
PGB_FUNC static inline void
ppu_dmg_palette_cache_build(pixel_t *destination, var palette_register)
{
    const var invert = CONFIG_PPU_INVERT ? 3U : 0U;

    destination[0] = palette[((palette_register >> 0) & 3U) ^ invert];
    destination[1] = palette[((palette_register >> 2) & 3U) ^ invert];
    destination[2] = palette[((palette_register >> 4) & 3U) ^ invert];
    destination[3] = palette[((palette_register >> 6) & 3U) ^ invert];
}

PGB_FUNC void
ppu_dmg_palette_cache_refresh(self pp)
{
    ppu_dmg_palette_cache_build(pp->dmg_palette_bgp, pp->rBGP);
    ppu_dmg_palette_cache_build(pp->dmg_palette_obp0, pp->rOBP0);
    ppu_dmg_palette_cache_build(pp->dmg_palette_obp1, pp->rOBP1);

#if PPU_DMG_FAST_RENDERER
    /* Background rows are stored in native RGB1555. A palette write leaves
     * raw tile bytes valid but makes every coloured row stale. */
    memset(pp->dmg_bg_valid, 0, sizeof(pp->dmg_bg_valid));
    ppu_dmg_mark_all_dirty(pp);
#endif
}
#endif

/* SH-2 has no variable-count shift instruction.  The original expression
 * `palette >> (index * 2)` therefore became a libgcc call for every pixel.
 * Select the same two bits with at most two fixed shifts instead. */
PGB_FUNC static inline var ppu_dmg_palette_lookup(var palette_register,
    var index)
{
    if(index & 2)
        palette_register >>= 4;
    if(index & 1)
        palette_register >>= 2;
    return palette_register & 3;
}

PGB_FUNC static void ppu_render_tile_line(self pp, pixel_t* __restrict scanline, const r8* __restrict tiledata, word attr)
{
    var tl = tiledata[0];
    var th = tiledata[1];
    
    var is_sprite = attr & 0x100;
    var x_flipped = attr & (1 << 5);
    var bit_mask = x_flipped ? 1 : 0x80;
#if defined(KNOVGBC_PPU_PALETTE_CACHE) && !PPU_IS_MONOCHROME
    const pixel_t *const dmg_palette = !is_sprite ? pp->dmg_palette_bgp :
        ((attr & 0x10) ? pp->dmg_palette_obp1 : pp->dmg_palette_obp0);
#else
    var dmg_palette = !is_sprite ? pp->rBGP :
        ((attr & 0x10) ? pp->rOBP1 : pp->rOBP0);
#endif
    
    #if PPU_IS_MONOCHROME
    #if PPU_IS_DITHER
    var spos = DITHER_INDEX;
    #endif
    var ppos = SCO;
    
    var pm = 0;
    #endif
    
    var px = 0;
    
    var j = 8;
    for(;;)
    {
        --j;
        
        /* Walk a one-bit mask across the tile instead of asking libgcc to
         * shift both tile bytes by a run-time count for every pixel. */
        var bd = ((tl & bit_mask) != 0) |
            (((th & bit_mask) != 0) << 1);
        if(x_flipped)
            bit_mask <<= 1;
        else
            bit_mask >>= 1;

        if(is_sprite && !bd)
        {
            #if PPU_IS_MONOCHROME
            px <<= PPU_IS_BPP;
            pm <<= PPU_IS_BPP;
            #else
            ++scanline;
            #endif
            if(j)
                continue;
            
            break;
        }
        
    #if CONFIG_IS_CGB
        if(IS_CGB)
        {
            var coli = ((attr & 7) << 2) | bd;
            
            #if !CONFIG_PPU_CGB_MONO
            var px16 = !is_sprite ? pp->BGP[coli] : pp->OBP[coli];
            
        #if PPU_MODE != 6 && PPU_MODE != 7
            var comp = (px16 & 0x1F) << 3;
            comp |= comp >> 5;
            px = comp << 16;
            
            comp = ((px16 >> 5) & 0x1F) << 3;
            comp |= comp >> 5;
            px |= comp << 8;
            
            comp = ((px16 >> 10) & 0x1F) << 3;
            comp |= comp >> 5;
            px |= comp << 0;
        #else
            px = px16;
        #endif
            #else
            px = palette
        #if CONFIG_PPU_INVERT
            [bd^3];
        #else
            [bd];
        #endif
            #endif
        }
        else
    #endif
        {
        #if PPU_IS_MONOCHROME
            bd = ppu_dmg_palette_lookup(dmg_palette, bd);
            
        #if CONFIG_PPU_INVERT
            bd ^= 3;
        #endif
            
            #if PPU_IS_DITHER
            px = (px << PPU_IS_BPP) | ditherbuf[bd][spos ^ (j & ((1 << PPU_IS_BPP) - 1))];
            #else
            px = (px << PPU_IS_BPP) | (bd >> (2 - PPU_IS_BPP));
            #endif
            pm = (pm << PPU_IS_BPP) | ((1 << PPU_IS_BPP) - 1);
            
        #else
#if defined(KNOVGBC_PPU_PALETTE_CACHE)
            px = dmg_palette[bd];
#else
            bd = ppu_dmg_palette_lookup(dmg_palette, bd);
            px = palette
        #if CONFIG_PPU_INVERT
            [bd^3];
        #else
            [bd];
        #endif
#endif
            
        #if PPU_MODE == 4
            px |= px << 4;
        #endif
            
        #endif
        }
        
        #if !PPU_IS_MONOCHROME
        *(scanline++) = px;
        #endif
        
        if(!j)
            break;
    }
    
    #if PPU_IS_MONOCHROME
    if(ppos)
    {
        *(scanline) = (*(scanline) & (~pm >> ppos)) | ((px & pm) >> ppos);
        
        ++scanline;
        
        *(scanline) = (*(scanline) & (~(pm << (8 - ppos)))) | ((px & pm) << (8 - ppos));
    }
    else if(pm == ((1 << (PPU_IS_BPP * PPU_IS_IPP)) - 1))
    {
        *(scanline) = px;
    }
    else
    {
        *(scanline) = (*(scanline) & ~pm) | (px & pm);
    }
    #endif
}

#if PPU_DMG_FAST_RENDERER
/* Translate the tile-map byte into the physical 16-byte tile slot in DMG
 * VRAM. This is the same signed/unsigned addressing rule as
 * ppu_resolve_line_tile(), but is local to the compact DMG scanline path. */
PGB_FUNC static inline word
ppu_dmg_tile_slot(word lcdc, word tile)
{
    if (lcdc & 0x10U) {
        return tile;
    }

    return 0x80U + (tile ^ 0x80U);
}

PGB_FUNC static inline r16
ppu_dmg_tile_row_cached(self pp, word tile, word row)
{
    const r8 row_bit = (r8)(1U << row);

    if ((pp->dmg_tile_valid[tile] & row_bit) == 0U) {
        const word offset = (tile << 4) | (row << 1);
        pp->dmg_tile_rows[tile][row] =
            ((r16)pp->VRAM[offset] << 8) | pp->VRAM[offset + 1U];
        pp->dmg_tile_valid[tile] |= row_bit;
        pp->dmg_bg_valid[tile] &= (r8)~row_bit;
    }

    return pp->dmg_tile_rows[tile][row];
}

PGB_FUNC static inline const pixel_t *
ppu_dmg_bg_tile_row_cached(self pp, word tile, word row)
{
    const r8 row_bit = (r8)(1U << row);

    if ((pp->dmg_bg_valid[tile] & row_bit) == 0U) {
        const r16 packed = ppu_dmg_tile_row_cached(pp, tile, row);
        r8 low = (r8)(packed >> 8);
        r8 high = (r8)packed;
        pixel_t *const destination = pp->dmg_bg_tiles[tile][row];
        const pixel_t *const palette = pp->dmg_palette_bgp;

        for (word pixel = 0U; pixel != 8U; pixel++) {
            const word colour = ((low & 0x80U) != 0U) |
                (((high & 0x80U) != 0U) << 1);
            destination[pixel] = palette[colour];
            low <<= 1;
            high <<= 1;
        }

        pp->dmg_bg_valid[tile] |= row_bit;
    }

    return pp->dmg_bg_tiles[tile][row];
}

PGB_FUNC static inline void
ppu_dmg_prepare_sprite_latch(self pp, word slot, word scan_y)
{
    USE_STATE;
    const hilow16_t hilo = ps->latches[slot];
    struct ppu_dmg_sprite_latch *const sprite = &ps->dmg_sprites[slot];
    const word index = hilo.high;

    sprite->x = hilo.low;
    if ((sprite->x == 0U) || (sprite->x >= RENDER_WIDTH) ||
        (index >= 40U)) {
        sprite->x = 0U;
        return;
    }

    {
        const r8 *const oamslice = &pp->OAM[index << 2];
        word line = scan_y + 16U - oamslice[0];
        word tile = oamslice[2];
        const word attr = oamslice[3];

        if (pp->rLCDC & 4U) {
            if (line >= 16U) {
                sprite->x = 0U;
                return;
            }
            tile &= 0xFEU;
            if (attr & 0x40U)
                line = 15U - line;
        }
        else {
            if (line >= 8U) {
                sprite->x = 0U;
                return;
            }
            if (attr & 0x40U)
                line = 7U - line;
        }

        sprite->tile = tile;
        sprite->attr = attr;
        sprite->line = line & 15U;
    }
}

PGB_FUNC static inline void
ppu_dmg_render_sprite_tile(self pp, pixel_t *destination, word tile,
    word row, word attr)
{
    /* An 8x16 object is two adjacent physical 8x8 tiles. The generic path
     * reaches its lower half by adding row 8..15 to the byte offset; split
     * that addressing before consulting the eight-row cache. */
    if (row >= 8U) {
        tile++;
        row &= 7U;
    }

    const r16 packed = ppu_dmg_tile_row_cached(pp, tile, row);
    r8 low = (r8)(packed >> 8);
    r8 high = (r8)packed;
    const pixel_t *const palette = (attr & 0x10U) ?
        pp->dmg_palette_obp1 : pp->dmg_palette_obp0;

    if (attr & 0x20U) {
        for (word pixel = 0U; pixel != 8U; pixel++) {
            const word colour = (low & 1U) | ((high & 1U) << 1);
            if (colour != 0U) {
                destination[pixel] = palette[colour];
            }
            low >>= 1;
            high >>= 1;
        }
    }
    else {
        for (word pixel = 0U; pixel != 8U; pixel++) {
            const word colour = ((low & 0x80U) != 0U) |
                (((high & 0x80U) != 0U) << 1);
            if (colour != 0U) {
                destination[pixel] = palette[colour];
            }
            low <<= 1;
            high <<= 1;
        }
    }
}

PGB_FUNC static void
ppu_dmg_mark_tilemap_row_dirty(self pp, word offset)
{
    const word lcdc = pp->rLCDC;
    const word map_base = offset & 0x1C00U;
    const word map_row = (offset >> 5) & 0x1FU;
    const word bg_base = 0x1800U | ((lcdc & 0x08U) << 7);

    if (map_base == bg_base) {
        for (word line = 0U; line != PPU_DMG_VISIBLE_LINES; line++) {
            if ((((line + pp->rSCY) & 0xFFU) >> 3) == map_row) {
                ppu_dmg_mark_line_dirty(pp, line);
            }
        }
    }

    /* A window map can be visible on many rows and its internal row counter
     * deliberately mirrors PicoGB's scanline model. Prefer a conservative
     * full invalidation here over retaining an old strip at an edge case. */
    if ((lcdc & 0x20U) && (pp->rWY < PPU_DMG_VISIBLE_LINES) &&
        (pp->rWX < RENDER_WIDTH) &&
        (map_base == (0x1800U | ((lcdc & 0x40U) << 4)))) {
        ppu_dmg_mark_all_dirty(pp);
    }
}

void
ppu_dmg_on_vram_write(self pp, word offset)
{
    offset &= 0x1FFFU;

    if (offset < 0x1800U) {
        const word tile = offset >> 4;
        const r8 row_bit = (r8)(1U << ((offset >> 1) & 7U));

        pp->dmg_tile_valid[tile] &= (r8)~row_bit;
        pp->dmg_bg_valid[tile] &= (r8)~row_bit;

        /* A pattern can occur anywhere in either selected map. Tracking all
         * reverse references would cost more than the draw it avoids, so
         * invalidate the viewport safely for pattern-data changes. */
        ppu_dmg_mark_all_dirty(pp);
    }
    else {
        ppu_dmg_mark_tilemap_row_dirty(pp, offset);
    }
}

void
ppu_dmg_on_oam_write(self pp, word offset)
{
    (void)offset;
    /* The generic renderer captures X/index in mode 2 but reads y/tile/attr
     * at draw time. Refresh prepared entries only if an OAM write lands in
     * that small interval, preserving the established PicoGB behaviour. */
    pp->state.dmg_sprite_latches_dirty = 1U;
    ppu_dmg_mark_all_dirty(pp);
}

void
ppu_dmg_on_io_write(self pp, word reg)
{
    switch (reg) {
        case 7U: /* BGP */
        case 8U: /* OBP0 */
        case 9U: /* OBP1 */
            ppu_dmg_palette_cache_refresh(pp);
            break;

        case 0U: /* LCDC */
            pp->state.dmg_sprite_latches_dirty = 1U;
            ppu_dmg_mark_all_dirty(pp);
            break;
        case 2U: /* SCY */
        case 3U: /* SCX */
        case 0xAU: /* WY */
        case 0xBU: /* WX */
            ppu_dmg_mark_all_dirty(pp);
            break;

        default:
            break;
    }
}

PGB_FUNC static void
ppu_dmg_render_scanline(self pp)
{
    USE_STATE;

    const word dst_y = ps->scanY;
    const word lcdc = pp->rLCDC;
    word src_y = (dst_y + pp->rSCY) & 0xFFU;
    word src_x = pp->rSCX;
    word dst_x = 8U - (src_x & 7U);
    pixel_t *const scanline = ps->framebuffer[dst_y];
    const wbool window_enable = (lcdc & 0x20U) &&
        (pp->rWY <= dst_y) && (pp->rWX < RENDER_WIDTH);
    const r8 *map = &pp->VRAM[0x1800U |
        ((src_y >> 3) << 5) | ((lcdc & 0x08U) << 7)];

    src_x >>= 3;
    src_y &= 7U;

    if (lcdc & 1U) {
        const word tile_count = !window_enable ?
            ((RENDER_WIDTH + 7U) >> 3) : ((pp->rWX + 7U) >> 3);

        for (word tile_pos = 0U; tile_pos != tile_count; tile_pos++) {
            const word tile = ppu_dmg_tile_slot(lcdc, map[src_x]);
            const pixel_t *const pixels =
                ppu_dmg_bg_tile_row_cached(pp, tile, src_y);

            picogb_sh2_store_tile8(&scanline[dst_x], pixels);
            dst_x += 8U;
            src_x = (src_x + 1U) & 31U;
        }
    }

    if (window_enable) {
        src_y = pp->_internal_WY;
        src_x = 0U;
        dst_x = pp->rWX + 1U;
        map = &pp->VRAM[0x1800U | ((src_y >> 3) << 5) |
            ((lcdc & 0x40U) << 4)];
        src_y &= 7U;

        while (dst_x < RENDER_WIDTH) {
            const word tile = ppu_dmg_tile_slot(lcdc, map[src_x]);
            const pixel_t *const pixels =
                ppu_dmg_bg_tile_row_cached(pp, tile, src_y);

            picogb_sh2_store_tile8(&scanline[dst_x], pixels);
            dst_x += 8U;
            src_x = (src_x + 1U) & 31U;
        }
    }

    if (lcdc & 2U) {
        if (ps->dmg_sprite_latches_dirty) {
            for (word slot = 0U; slot != ps->dmg_sprite_count; slot++) {
                ppu_dmg_prepare_sprite_latch(pp, slot, dst_y);
            }
            ps->dmg_sprite_latches_dirty = 0U;
        }

        word sprite = ps->dmg_sprite_count;

        while (sprite != 0U) {
            const struct ppu_dmg_sprite_latch *const entry =
                &ps->dmg_sprites[--sprite];

            if ((entry->x != 0U) && (entry->x < RENDER_WIDTH)) {
                ppu_dmg_render_sprite_tile(pp, &scanline[entry->x],
                    entry->tile, entry->line, entry->attr);
            }
        }
    }
}
#endif

#if PPU_DMG_DIRTY_SCANLINES
/* This build deliberately keeps the original 8-bit compositor. A changed
 * tile-map byte only affects scanlines sampling its source row; other writes
 * use the conservative full-viewport fallback. Dirty bits survive the
 * frameskip half until their pixels are actually redrawn. */
void
ppu_dmg_on_vram_write(self pp, word offset)
{
    offset &= 0x1FFFU;
    if (offset < 0x1800U) {
#if defined(KNOVGBC_PPU_TILE_DIRTY_LINES)
        const word tile = offset >> 4;
        const word tile_byte = tile >> 3;
        const r8 tile_bit = (r8)(1U << (tile & 7U));

        for (word line = 0U; line < PPU_DMG_INDEXED_VISIBLE_LINES; line++) {
            if (pp->dmg_indexed_line_tiles[line][tile_byte] & tile_bit) {
                ppu_dmg_indexed_mark_line_dirty(pp, line);
            }
        }
#else
        ppu_dmg_indexed_mark_all_dirty(pp);
#endif
        return;
    }

    const word lcdc = pp->rLCDC;
    const word map_base = offset & 0x1C00U;
    const word map_row = (offset >> 5) & 0x1FU;
    const word bg_base = 0x1800U | ((lcdc & 0x08U) << 7);

    if (map_base == bg_base) {
        /* One source map row spans eight source scanlines. Map only those
         * eight back through SCY, including a possible 255 -> 0 wrap. */
        for (word row = 0U; row != 8U; row++) {
            const word line = (((map_row << 3) + row) - pp->rSCY) & 0xFFU;
            if (line < PPU_DMG_INDEXED_VISIBLE_LINES) {
                ppu_dmg_indexed_mark_line_dirty(pp, line);
            }
        }
    }

    /* The window has its own line counter, which may differ from visible Y
     * after register changes. Keep the original compositor's result safe. */
    if ((lcdc & 0x20U) &&
        (pp->rWY < PPU_DMG_INDEXED_VISIBLE_LINES) &&
        (pp->rWX < RENDER_WIDTH) &&
        (map_base == (0x1800U | ((lcdc & 0x40U) << 4)))) {
        ppu_dmg_indexed_mark_all_dirty(pp);
    }
}

void
ppu_dmg_on_oam_write(self pp, word offset)
{
    if (offset >= 160U) {
        return;
    }

    if (!pp->dmg_indexed_oam_valid) {
        memcpy(pp->dmg_indexed_oam_shadow, pp->OAM,
            sizeof(pp->dmg_indexed_oam_shadow));
        pp->dmg_indexed_oam_valid = 1U;
        ppu_dmg_indexed_mark_all_dirty(pp);
        return;
    }

    /* DMA currently sends offset zero after the complete copy. A direct
     * write to FE00 also takes this path; scanning 40 entries is safe and
     * rare compared with scanning on every ordinary byte write. */
    const word first_sprite = (offset == 0U) ? 0U : (offset >> 2);
    const word end_sprite = (offset == 0U) ? 40U : first_sprite + 1U;

    for (word sprite = first_sprite; sprite != end_sprite; sprite++) {
        const word first_byte = sprite << 2;
        r8 *const previous = &pp->dmg_indexed_oam_shadow[first_byte];
        const r8 *const current = &pp->OAM[first_byte];

        if (previous[0] != current[0] || previous[1] != current[1] ||
            previous[2] != current[2] || previous[3] != current[3]) {
            ppu_dmg_indexed_mark_sprite_rows(pp, previous);
            ppu_dmg_indexed_mark_sprite_rows(pp, current);
            previous[0] = current[0];
            previous[1] = current[1];
            previous[2] = current[2];
            previous[3] = current[3];
        }
    }
}

void
ppu_dmg_on_io_write(self pp, word reg)
{
    switch (reg) {
        case 7U: /* BGP */
        case 8U: /* OBP0 */
        case 9U: /* OBP1 */
#if defined(KNOVGBC_PPU_PALETTE_CACHE)
            ppu_dmg_palette_cache_refresh(pp);
#endif
            ppu_dmg_indexed_mark_all_dirty(pp);
            break;
        case 0U: /* LCDC */
        case 2U: /* SCY */
        case 3U: /* SCX */
        case 0xAU: /* WY */
        case 0xBU: /* WX */
            ppu_dmg_indexed_mark_all_dirty(pp);
            break;
        default:
            break;
    }
}
#endif

PGB_FUNC static void ppu_render_scanline(self pp)
{
#if PPU_DMG_FAST_RENDERER
    ppu_dmg_render_scanline(pp);
#else
    USE_STATE;
    
    var dstY = ps->scanY;
    var srcY = (dstY + pp->rSCY) & 0xFF;
    var srcX = pp->rSCX;
    var dstX = 8 - (srcX & 7);
    
    const r8* __restrict r_line_idx = &pp->VRAM[0x1800 | ((srcY >> 3) << 5) | ((pp->rLCDC & (1 << 3)) << 7)];
    
    pixel_t* __restrict scanline = ps->framebuffer[dstY];

#if PPU_DMG_DIRTY_SCANLINES && defined(KNOVGBC_PPU_TILE_DIRTY_LINES)
    /* Rebuild this line's dependencies only when it is actually redrawn. */
    memset(pp->dmg_indexed_line_tiles[dstY], 0,
        sizeof(pp->dmg_indexed_line_tiles[dstY]));
#endif
    
    const var window_enable = (pp->rLCDC & 0x20) && (pp->rWY <= dstY) && (pp->rWX < RENDER_WIDTH);
    
    var i;
    
    srcX >>= 3;
    srcY &= 7;
    
#if !CONFIG_IS_CGB
    if(pp->rLCDC & 1)
#endif
    {
        var pattern = DITHER_PATTERN | SCO_CALC;
        
        var maxsize;
        if(!window_enable)
            maxsize = ((RENDER_WIDTH + 7) >> 3);
        else
            maxsize = (pp->rWX + 7) >> 3;
        
        for(i = 0; i < maxsize; ++i)
        {
            var idx = r_line_idx[srcX];
#if PPU_DMG_DIRTY_SCANLINES && defined(KNOVGBC_PPU_TILE_DIRTY_LINES)
            ppu_dmg_indexed_note_tile(pp, dstY,
                ppu_dmg_indexed_bg_tile_index(pp, idx));
#endif
            var attr = IS_CGB ? r_line_idx[srcX + 0x2000] : 0;
            
            var tileY = !(attr & (1 << 6)) ? srcY : 7 - srcY;
            
            const r8* __restrict tiledata = ppu_resolve_line_tile(pp, idx, tileY);
            if(attr & (1 << 3))
                tiledata += 0x2000;
            
            ppu_render_tile_line(pp, &scanline[dstX / PPU_IS_IPP], tiledata, attr | pattern);
            
            dstX += 8;
            
            srcX = (srcX + 1) & 31;
        }
    }
    
    if(window_enable)
    //if(0)
    {
        srcY = pp->_internal_WY;
        srcX = 0;
        dstX = pp->rWX + 1;
        
        var pattern = DITHER_PATTERN | SCO_CALC;
        
        
        r_line_idx = &pp->VRAM[0x1800 | ((srcY >> 3) << 5) | ((pp->rLCDC & (1 << 6)) << 4)];
        
        srcY &= 7;
        while(dstX < RENDER_WIDTH)
        {
            var idx = r_line_idx[srcX];
#if PPU_DMG_DIRTY_SCANLINES && defined(KNOVGBC_PPU_TILE_DIRTY_LINES)
            ppu_dmg_indexed_note_tile(pp, dstY,
                ppu_dmg_indexed_bg_tile_index(pp, idx));
#endif
            
            const r8* __restrict tiledata = ppu_resolve_line_tile(pp, idx, srcY);
            ppu_render_tile_line(pp, &scanline[dstX / PPU_IS_IPP], tiledata, pattern);
            
            dstX += 8;
            
            srcX = (srcX + 1) & 31;
        }
    }
    
    if(pp->rLCDC & 2)
    {
        var pattern = 0x100;
        
        i = PPU_N_OBJS;
        while(i)
        {
            --i;
            
            var posX, idx;
            {
                hilow16_t hilo = pp->state.latches[i];
                posX = hilo.low;
                idx = hilo.high;
            }
            if(!posX || posX >= RENDER_WIDTH || idx >= 40)
                continue;
            
            const r8* __restrict oamslice = &pp->OAM[idx << 2];
            
            var y = oamslice[0];
            var objLine = dstY + 16 - y;
            
            var tile = oamslice[2];
            var attr = oamslice[3];
            
            if(!(pp->rLCDC & (1 << 2)))
            {
                if(objLine >= 8)
                    continue;
                
                if(attr & (1 << 6))
                    objLine = 7 - objLine;
            }
            else
            {
                if(objLine >= 16)
                    continue;
                
                tile &= 0xFE;
                
                if(attr & (1 << 6))
                    objLine = 15 - objLine;
            }
            
            objLine &= 15;
            dstX = posX;

#if PPU_DMG_DIRTY_SCANLINES && defined(KNOVGBC_PPU_TILE_DIRTY_LINES)
            ppu_dmg_indexed_note_tile(pp, dstY,
                tile + (objLine >> 3));
#endif
            
            const r8* __restrict tiledata;
            
        #if CONFIG_IS_CGB
            if(!IS_CGB || !(attr & (1 << 3)))
        #endif
                tiledata = ppu_resolve_line_sprite(pp, tile, objLine);
        #if CONFIG_IS_CGB
            else
                tiledata = ppu_resolve_line_sprite_cgb1(pp, tile, objLine);
        #endif
            
            ppu_render_tile_line(pp, &scanline[dstX / PPU_IS_IPP], tiledata, attr | pattern | SCO_CALC | DITHER_PATTERN);
        }
    }
#endif
}

#if defined(KNOVGBC_VDP12_NATIVE)
#if defined(KNOVGBC_VDP12_SOFTWARE_REFERENCE)
/* Host parity builds keep the old framebuffer in addition to the native
 * trace. The Saturn experiment omits it, so G no longer pays both costs. */
#define PPU_RENDER_SCANLINE(pp) do {                                          \
    picogb_vdp12_capture_scanline(pp);                                       \
    ppu_render_scanline(pp);                                                  \
} while (0)
#else
#define PPU_RENDER_SCANLINE(pp) picogb_vdp12_capture_scanline(pp)
#endif
#elif defined(KNOVGBC_SLAVE_PPU_SHADOW)
void
ppu_slave_shadow_render_scanline(self pp)
{
    ppu_render_scanline(pp);
}

/* Keep this hook beside the compositor rather than teaching the Slave about
 * PicoGB's timing machinery. The Master calls it immediately before each
 * scanline it actually draws, so its snapshot matches the renderer's reads. */
#define PPU_RENDER_SCANLINE(pp) do {                                          \
    knovgbc_slave_ppu_shadow_line(pp);                                       \
    ppu_render_scanline(pp);                                                  \
} while (0)
#else
#define PPU_RENDER_SCANLINE(pp) ppu_render_scanline(pp)
#endif

PGB_FUNC static inline word ppu_stat_set_mode(self pp, word mode)
{
    var prev = pp->rSTAT & 3;
    pp->rSTAT = (pp->rSTAT & 0xFC) | (mode & 3);
    return prev;
}

PGB_FUNC static void ppu_update_LYC(self pp, word scanY)
{
    if(scanY == pp->rLYC)
    {
        if(!(pp->rSTAT & 4) && (pp->rSTAT & (1 << 6)))
            pp->IF_SCHED |= 2;
        
        pp->rSTAT |= 4;
    }
    else
    {
        pp->rSTAT &= 0xFB;
    }
}

PGB_FUNC static void ppu_update_newline(self pp, word scanY)
{
    ppu_update_LYC(pp, scanY);
    
    if(scanY < 144)
    {
        ppu_stat_set_mode(pp, 2);
        
        if(pp->rSTAT & (1 << 5))
            pp->IF_SCHED |= 2;
    }
    else if(ppu_stat_set_mode(pp, 1) != 1)
    {
        if(pp->rSTAT & (3 << 4))
            pp->IF_SCHED |= 2;
        
        pp->IF_SCHED |= 1;
        
        pp->_internal_WY = 0;
        
        pp->_redrawed = 255;
    }
}

PGB_FUNC void ppu_turn_on(self pp)
{
    ppu_reset(pp);
    ppu_update_LYC(pp, pp->state.scanY);
}

PGB_FUNC void ppu_on_write_LYC(self pp)
{
    ppu_update_LYC(pp, pp->state.scanY);
}

PGB_FUNC static inline void ppu_trigger_oam_scan(self pp)
{
    USE_STATE;
    
    var scanY = ps->scanY;
    var i, j = 0;
    
#if !CONFIG_IS_CGB
    if(pp->rLCDC & 2)
#endif
    {
        var tresh = scanY + 16;
        var tresl;
        if(pp->rLCDC & 4)
            tresl = tresh - 16;
        else
            tresl = tresh - 8;
            
        
        for(i = 0; i != 40; ++i)
        {
            const r8* __restrict oamslice = &pp->OAM[i << 2];
            
            var y = oamslice[0];
            var x = oamslice[1];
            
            if(tresl < y && tresh >= y)
            {
#if PPU_DMG_FAST_RENDERER
                ps->latches[j].low = x < 168 ? x : 0;
                ps->latches[j].high = i;
#else
                ps->latches[j].low = x < 168 ? x : 0;
                ps->latches[j].high = i;
#endif
                
                if(++j == PPU_N_OBJS)
                    break;
            }
        }
    }
    
 #if PPU_DMG_FAST_RENDERER
    ps->dmg_sprite_count = j;
    /* Keep only X/index during mode 2. The remaining fields are prepared
     * immediately before an actually rendered dirty line, avoiding work on
     * the frameskip half while retaining the generic draw-time semantics. */
    ps->dmg_sprite_latches_dirty = 1U;
 #endif
    while(j != PPU_N_OBJS)
        ps->latches[j++].low = 0;
}

PGB_FUNC static inline void ppu_trigger_scanline_draw(self pp)
{
    USE_STATE;
    
    var scanY = ps->scanY;
    
    #if PPU_DMG_FAST_RENDERER
    /* The dependency tracker only omits a scanline when the exact pixels
     * retained from the last displayed render remain valid.  PPU timing,
     * window accounting and the interlace schedule still run unconditionally.
     */
    wbool should_render = 0U;
    #if PPU_INTERLACE != 0
    if(scanY & 1U)
        should_render = (ps->interlace & 2U) != 0U;
    else
        should_render = (ps->interlace & 1U) != 0U;
    #else
    should_render = 1U;
    #endif

    if(should_render && ppu_dmg_line_is_dirty(pp, scanY))
    {
        PPU_RENDER_SCANLINE(pp);
        ppu_dmg_clear_line_dirty(pp, scanY);
        pp->dmg_frame_changed = 1U;
    }
    #elif PPU_DMG_DIRTY_SCANLINES
    /* Preserve the baseline's frame schedule and generic scanline renderer.
     * The bitmap only removes work when the retained line is still valid. */
    wbool should_render = 0U;
    #if PPU_INTERLACE != 0
    if (scanY & 1U)
        should_render = (ps->interlace & 2U) != 0U;
    else
        should_render = (ps->interlace & 1U) != 0U;
    #else
    should_render = 1U;
    #endif

    if (should_render && ppu_dmg_indexed_line_is_dirty(pp, scanY)) {
        PPU_RENDER_SCANLINE(pp);
        ppu_dmg_indexed_clear_line_dirty(pp, scanY);
    }
    #elif PPU_INTERLACE != 0
    if(scanY & 1)
    {
        if(ps->interlace & 2)
            PPU_RENDER_SCANLINE(pp);
    }
    else
    {
        if(ps->interlace & 1)
            PPU_RENDER_SCANLINE(pp);
    }
    #else
    PPU_RENDER_SCANLINE(pp);
    #endif
    
#if PPU_SCANLINE_UPDATES
    pp->_redrawed = scanY + 1;
#endif
    
    if((pp->rLCDC & 0x20) && (pp->rWY <= scanY) && (pp->rWX < 168) && pp->rWX)
        ++(pp->_internal_WY);
}

PGB_FUNC static inline word ppu_tick_internal_0(self pp)
{
    var scanY = pp->state.scanY;

#if defined(KNOVGBC_VDP12_NATIVE)
    /* This is the first event of the 144-line visual pass. The alternate
     * emulated frame performs timing only and leaves the captured image
     * untouched. Reset here, before line 0 is drawn, not in the frontend
     * after its first line has already been recorded. */
    if ((scanY == 0U) && ((pp->state.interlace & 3U) == 3U)) {
        picogb_vdp12_frame_reset();
    }
#endif

#if defined(KNOVGBC_SLAVE_PPU_SHADOW)
    /* After the 153 -> 0 transition, interlace==3 denotes the only pass for
     * which the MVP renders all 144 lines. Snapshot before line 0 begins. */
    if ((scanY == 0U) && ((pp->state.interlace & 3U) == 3U)) {
        knovgbc_slave_ppu_shadow_frame_begin(pp);
    }
#endif
    
    ppu_update_newline(pp, scanY);
    
#if CONFIG_PPU_ACTION_ON_START
    ppu_trigger_oam_scan(pp);
#endif
    
    return scanY < 144;
}

PGB_FUNC static inline void ppu_tick_internal_1(self pp)
{
    ppu_stat_set_mode(pp, 3);
    
#if !CONFIG_PPU_ACTION_ON_START
    ppu_trigger_oam_scan(pp);
#else
    ppu_trigger_scanline_draw(pp);
#endif
}

PGB_FUNC static inline void ppu_tick_internal_2(self pp)
{
    ppu_stat_set_mode(pp, 0);
    
    if(pp->rSTAT & (1 << 3))
        pp->IF_SCHED |= 2;
    
#if !CONFIG_PPU_ACTION_ON_START
    ppu_trigger_scanline_draw(pp);
#endif
}

PGB_FUNC static inline void ppu_tick_internal_3(self pp)
{
    USE_STATE;
    
    var scanY = ps->scanY;
    
    if((++scanY) == 154)
    {
    #if PPU_INTERLACE != 0
        ps->interlace = ~ps->interlace;
    #endif
        
        scanY = 0;
#if PPU_DMG_FAST_RENDERER
        /* This flag describes the next full rendering pass. Dirty-line bits
         * deliberately survive skipped frames, so an update made there is
         * still drawn on the following presented frame. */
        pp->dmg_frame_changed = 0U;
#endif
    }
    
    ps->scanY = scanY;
}

PGB_FUNC void ppu_tick_internal(self pp, word ncycles, word rem)
{
    var ren = pp->next_update_n;
    
    for(;;)
    {
        ncycles -= rem;
        
        switch(ren)
        {
            case 0: // Start of line / OAM scan
            default:
                if(ppu_tick_internal_0(pp))
                {
                    rem = UPDATE_1 - UPDATE_0;
                    ren = 1;
                }
                else if(pp->state.scanY != 153)
                {
                    rem = UPDATE_3 - UPDATE_0;
                    ren = 3;
                }
                else // WTF
                {
                    rem = 4;
                    ren = 3;
                }
                
                ppu_update_LYC(pp, pp->state.scanY);
                
                break;
            
            case 1: // Displaying
                ppu_tick_internal_1(pp);
                
                rem = UPDATE_2 - UPDATE_1;
                ren = 2;
                break;
            case 2: // HBlank
                ppu_tick_internal_2(pp);
                
                rem = UPDATE_3 - UPDATE_2;
                ren = 3;
                break;
            case 3: // End of line
                if(pp->state.scanY != 153)
                {
                    ren = 0;
                    rem = 0;
                }
                else
                {
                    ren = 0;
                    rem = 456 - 4; // ???
                }
                
                ppu_tick_internal_3(pp);
                break;
        }
        
        if(ncycles < rem) // Not enough cycles to spend to move into a new mode
        {
            pp->next_update_ticks = rem - ncycles;
            pp->next_update_n = ren;
            return;
        }
    }
}
