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
#pragma once

#include "types.h"

#define PPU_N_OBJS 10

/* This specialised renderer is deliberately DMG/RGB1555-only.  Other PicoGB
 * configurations retain the upstream scanline renderer without structural
 * changes, which keeps the future CGB path isolated from this MVP work. */
#if defined(KNOVGBC_PPU_FULL_OPT) && !defined(KNOVGBC_PPU_PALETTE_CACHE)
#error "KNOVGBC_PPU_FULL_OPT requires KNOVGBC_PPU_PALETTE_CACHE"
#endif

#if defined(KNOVGBC_PPU_FULL_OPT) && !CONFIG_IS_CGB && (PPU_MODE == 7)
#define PPU_DMG_FAST_RENDERER 1
#define PPU_DMG_TILE_COUNT 384
#define PPU_DMG_TILE_ROWS 8
#define PPU_DMG_VISIBLE_LINES 144
#else
#define PPU_DMG_FAST_RENDERER 0
#endif

/* The indexed VDP2 experiment keeps the original compositor. Only its
 * unchanged scanline output may be reused when no dependency was written. */
#if defined(KNOVGBC_PPU_DIRTY_SCANLINES) && !CONFIG_IS_CGB && (PPU_MODE == 8)
#define PPU_DMG_DIRTY_SCANLINES 1
#define PPU_DMG_INDEXED_VISIBLE_LINES 144
#else
#define PPU_DMG_DIRTY_SCANLINES 0
#endif

#if defined(KNOVGBC_PPU_TILE_DIRTY_LINES) && !PPU_DMG_DIRTY_SCANLINES
#error "Selective tile dirtiness requires the indexed DMG dirty-scanline renderer"
#endif

#if PPU_DMG_FAST_RENDERER
/* Keep the decoded per-line sprite state explicit rather than relying on a
 * compiler-specific anonymous-struct type in the hot renderer. */
struct ppu_dmg_sprite_latch
{
    r8 x;
    r8 tile;
    r8 attr;
    r8 line;
};
#endif

struct ppu_state_t
{
    pixel_t* __restrict * __restrict framebuffer;
    void* _dummy1;
    void* _dummy2;
    void* _dummy3;
    
    var scanX;
    var scanY;
    var subclk;
    var interlace;
    
    var posX;
    var _dummy5;
    var latchY;
    var latchX;
    
    hilow16_t latches[PPU_N_OBJS];

#if PPU_DMG_FAST_RENDERER
    /* OAM is still scanned in hardware order and captures its selected
     * X/index pairs. The remaining decoded fields are prepared once only for
     * a dirty scanline that will actually be rendered. */
    struct ppu_dmg_sprite_latch dmg_sprites[PPU_N_OBJS];
    r8 dmg_sprite_count;
    r8 dmg_sprite_latches_dirty;
#endif
};

struct ppu_t
{
    const r8* __restrict VRAM;
    const r8* __restrict OAM;
    
    var next_update_ticks;
    var next_update_n;
    
    var is_cgb;
    var _redrawed;
    var rOBPI;
    var rBGPI;
    
    var rOBP0;
    var rOBP1;
    var rBGP;
    var rLYC;
    
    var rLCDC;
    var rSTAT;
    var rSCX;
    var rSCY;
    
    var rWX;
    var rWY;
    var IF_SCHED;
    var _internal_WY;
    
    r16 OBP[4*8];
    r16 BGP[4*8];

#if defined(KNOVGBC_PPU_PALETTE_CACHE)
    /* The three DMG palette registers change through I/O, whereas their
     * decoded colours are used for every rendered pixel. */
    pixel_t dmg_palette_bgp[4];
    pixel_t dmg_palette_obp0[4];
    pixel_t dmg_palette_obp1[4];
#endif

#if PPU_DMG_FAST_RENDERER
    /* Cached raw 2bpp tile rows serve sprites; the BGP-coloured cache lets
     * the background/window path copy eight native RGB1555 pixels at once.
     * Validity is per tile row, so a write to VRAM invalidates exactly the
     * affected derived data. */
    r16 dmg_tile_rows[PPU_DMG_TILE_COUNT][PPU_DMG_TILE_ROWS];
    r8 dmg_tile_valid[PPU_DMG_TILE_COUNT];
    r8 dmg_bg_valid[PPU_DMG_TILE_COUNT];
    pixel_t dmg_bg_tiles[PPU_DMG_TILE_COUNT][PPU_DMG_TILE_ROWS][8]
        __attribute__((aligned(4)));

    /* A conservative dependency map: register/OAM/tile-data writes dirty the
     * full viewport; selected tile-map writes can dirtify only the affected
     * source rows. Untouched rows retain their already-rendered pixels. */
    r8 dmg_dirty_lines[(PPU_DMG_VISIBLE_LINES + 7) / 8];
    r8 dmg_frame_changed;
#endif

#if PPU_DMG_DIRTY_SCANLINES
    r8 dmg_indexed_dirty_lines[(PPU_DMG_INDEXED_VISIBLE_LINES + 7) / 8];
#if defined(KNOVGBC_PPU_TILE_DIRTY_LINES)
    /* For each already drawn line, remember which of the 384 DMG pattern
     * tiles contributed pixels. A changed tile then dirties only its users.
     * Lines never drawn or invalidated for another reason are already dirty. */
    r8 dmg_indexed_line_tiles[PPU_DMG_INDEXED_VISIBLE_LINES][384 / 8];
#endif
    /* OAM DMA can repeat an identical 160-byte table every frame. Keep the
     * last observed entries so only changed sprite Y ranges become dirty. */
    r8 dmg_indexed_oam_shadow[160];
    r8 dmg_indexed_oam_valid;
#endif
    
    struct ppu_state_t state;
};

typedef struct ppu_t ppu_t;


void ppu_reset(ppu_t* __restrict pp);
void ppu_initialize(ppu_t* __restrict pp);
void ppu_turn_off(ppu_t* __restrict pp);
void ppu_turn_on(ppu_t* __restrict pp);

void ppu_on_write_LYC(ppu_t* __restrict pp);

#if defined(KNOVGBC_SLAVE_PPU_SHADOW)
/* The Slave shadow target reuses the exact generic compositor one scanline at
 * a time. It does not call ppu_tick(), so timing and interrupts remain owned
 * by the Master. */
void ppu_slave_shadow_render_scanline(ppu_t* __restrict pp);
void knovgbc_slave_ppu_shadow_frame_begin(const ppu_t* __restrict pp);
void knovgbc_slave_ppu_shadow_line(const ppu_t* __restrict pp);
#endif

#if defined(KNOVGBC_PPU_PALETTE_CACHE)
void ppu_dmg_palette_cache_refresh(ppu_t* __restrict pp);
#endif

#if PPU_DMG_FAST_RENDERER || PPU_DMG_DIRTY_SCANLINES
void ppu_dmg_on_vram_write(ppu_t* __restrict pp, word offset);
void ppu_dmg_on_oam_write(ppu_t* __restrict pp, word offset);
void ppu_dmg_on_io_write(ppu_t* __restrict pp, word reg);
#endif

void ppu_tick_internal(ppu_t* __restrict pp, word ncycles, word rem);


PGB_FUNC static inline word ppu_tick(ppu_t* __restrict pp, word ncycles)
{
    var rem = pp->next_update_ticks;
    
    if(ncycles < rem) // Less cycles than to cause an event
    {
        // Just subtract and return
        pp->next_update_ticks = rem - ncycles;
        return 0;
    }
    
    ppu_tick_internal(pp, ncycles, rem);
    
    var ret = pp->IF_SCHED;
    if(ret)
        pp->IF_SCHED = 0;
    
    return ret;
}
