/*
 * Synthetic differential regression for the DMG PPU optimisation bundle.
 *
 * The test deliberately goes through mch_memory_dispatch_write() for every
 * visual mutation.  This catches regressions in the direct-memory path as
 * well as cache invalidation, instead of merely calling PPU helpers by hand.
 * Compile it once with the reference PPU and once with KNOVGBC_PPU_FULL_OPT;
 * stdout must match byte-for-byte. KNOVGBC_PPU_DIRTY_REGRESSION also uses this
 * fixture to compare isolated mode-8 dependencies with the 048 renderer.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fabric.h"
#include "microcode.h"
#include "microcode_dispatch.h"
#include "mi.h"
#include "ppu.h"

enum {
    VISIBLE_WIDTH = 160,
    VISIBLE_HEIGHT = 144,
    FRAME_STRIDE = 176,
    FRAME_GUARD = 2,
    FRAME_PIXELS = FRAME_GUARD + (VISIBLE_HEIGHT * FRAME_STRIDE) + FRAME_GUARD,
#if PPU_MODE == 8
    FRAME_GUARD_VALUE = 0xA5
#else
    FRAME_GUARD_VALUE = 0x5AA5
#endif
};

struct ppu_fixture {
    struct mb_state mb;
    struct mi_dispatch memory;
    struct ppu_t ppu;
    struct pgf_userdata_t fabric;

    r8 rom[0x8000];
    r8 wram[0x2000];
    r8 vram[0x2000];
    r8 sram[0x2000];
    r8 hram[0x100];
    r8 oam[0x100];

    pixel_t pixels[FRAME_PIXELS];
    pixel_t *lines[VISIBLE_HEIGHT];
};

#if defined(KNOVGBC_PPU_FULL_OPT) || defined(KNOVGBC_PPU_DIRTY_SCANLINES)
static void fixture_vram_write(void *userdata, word offset)
{
    struct pgf_userdata_t *const fabric = userdata;
    ppu_dmg_on_vram_write(fabric->ppu, offset);
}

static void fixture_oam_write(void *userdata, word offset)
{
    struct pgf_userdata_t *const fabric = userdata;
    ppu_dmg_on_oam_write(fabric->ppu, offset);
}
#endif

static uint64_t fnv_byte(uint64_t hash, r8 value)
{
    return (hash ^ value) * UINT64_C(1099511628211);
}

static uint64_t fnv_u16(uint64_t hash, r16 value)
{
    hash = fnv_byte(hash, (r8)value);
    return fnv_byte(hash, (r8)(value >> 8));
}

static uint64_t framebuffer_hash(const struct ppu_fixture *fixture)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    word y;
    word x;

    for (y = 0U; y != VISIBLE_HEIGHT; y++) {
        for (x = 0U; x != VISIBLE_WIDTH; x++) {
            hash = fnv_u16(hash, fixture->lines[y][x + 8U]);
        }
    }
    return hash;
}

static int guards_valid(const struct ppu_fixture *fixture)
{
    return fixture->pixels[0] == FRAME_GUARD_VALUE &&
        fixture->pixels[1] == FRAME_GUARD_VALUE &&
        fixture->pixels[FRAME_GUARD + (VISIBLE_HEIGHT * FRAME_STRIDE)] ==
            FRAME_GUARD_VALUE &&
        fixture->pixels[FRAME_GUARD + (VISIBLE_HEIGHT * FRAME_STRIDE) + 1U] ==
            FRAME_GUARD_VALUE;
}

static void write_byte(struct ppu_fixture *fixture, word address, word value)
{
    mch_memory_dispatch_write(&fixture->mb, address, value);
}

/* Advance only through the PPU: this makes scanline, window and sprite
 * dependencies deterministic without having to construct a whole ROM. */
static int run_to_vblank(struct ppu_fixture *fixture)
{
    word guard;

    fixture->ppu._redrawed = 0U;
    for (guard = 0U; guard != 200U; guard++) {
        (void)ppu_tick(&fixture->ppu, 456U);
        if (fixture->ppu._redrawed != 0U) {
            fixture->ppu._redrawed = 0U;
            return 1;
        }
    }

    return 0;
}

static int run_three_frames(struct ppu_fixture *fixture)
{
    return run_to_vblank(fixture) && run_to_vblank(fixture) &&
        run_to_vblank(fixture);
}

static int fixture_init(struct ppu_fixture *fixture)
{
    word y;

    memset(fixture, 0, sizeof(*fixture));
    for (y = 0U; y != FRAME_PIXELS; y++) {
        fixture->pixels[y] = 0U;
    }
    fixture->pixels[0] = FRAME_GUARD_VALUE;
    fixture->pixels[1] = FRAME_GUARD_VALUE;
    fixture->pixels[FRAME_GUARD + (VISIBLE_HEIGHT * FRAME_STRIDE)] =
        FRAME_GUARD_VALUE;
    fixture->pixels[FRAME_GUARD + (VISIBLE_HEIGHT * FRAME_STRIDE) + 1U] =
        FRAME_GUARD_VALUE;

    for (y = 0U; y != VISIBLE_HEIGHT; y++) {
        fixture->lines[y] = &fixture->pixels[FRAME_GUARD +
            (y * FRAME_STRIDE)];
    }

    fixture->memory.ROM = fixture->rom;
    fixture->memory.WRAM = fixture->wram;
    fixture->memory.VRAM = fixture->vram;
    fixture->memory.SRAM = fixture->sram;
    fixture->memory.HRAM = fixture->hram;
    fixture->memory.OAM = fixture->oam;
    fixture->memory.BANK_ROM = 1U;
    fixture->memory.BANK_WRAM = 1U;
    fixture->memory.BANK_VRAM = 0U;
    fixture->memory.BANK_SRAM = 0U;
    fixture->memory.N_ROM = 2U;
    fixture->memory.N_SRAM = 1U;
    fixture->memory.dispatch_ROM = pgf_cb_ROM_Dummy;
    fixture->memory.dispatch_IO = pgf_cb_IO_;

    fixture->mb.mi = &fixture->memory;
    micache_invalidate(&fixture->mb.micache);

    fixture->fabric.mb = &fixture->mb;
    fixture->fabric.ppu = &fixture->ppu;
    fixture->fabric.apu = NULL;
    fixture->memory.userdata = &fixture->fabric;
#if defined(KNOVGBC_PPU_FULL_OPT) || defined(KNOVGBC_PPU_DIRTY_SCANLINES)
    fixture->memory.dispatch_VRAM_write = fixture_vram_write;
    fixture->memory.dispatch_OAM_write = fixture_oam_write;
#endif

    ppu_initialize(&fixture->ppu);
    fixture->ppu.VRAM = fixture->vram;
    fixture->ppu.OAM = fixture->oam;
    fixture->ppu.state.framebuffer = fixture->lines;
    fixture->ppu.state.scanY = 0U;
    fixture->ppu.next_update_n = 0U;
    fixture->ppu.next_update_ticks = 0U;
    fixture->ppu.state.interlace = ~0U;

    /* LCD on, BG/window/sprites enabled, 8x16 sprites, unsigned tiles,
     * BG map 9800 and window map 9C00. */
    fixture->ppu.rLCDC = 0xF7U;
    fixture->ppu.rSTAT = 0x85U;
    fixture->ppu.rSCX = 3U;
    fixture->ppu.rSCY = 5U;
    fixture->ppu.rWX = 70U;
    fixture->ppu.rWY = 40U;
    fixture->ppu.rBGP = 0xE4U;
    fixture->ppu.rOBP0 = 0xD2U;
    fixture->ppu.rOBP1 = 0x39U;
#if defined(KNOVGBC_PPU_PALETTE_CACHE)
    ppu_dmg_palette_cache_refresh(&fixture->ppu);
#endif

    return guards_valid(fixture);
}

static void install_visual_fixture(struct ppu_fixture *fixture)
{
    word tile;
    word row;
    word offset;

    /* All 384 physical tile slots matter when FF40 chooses signed data. */
    for (tile = 0U; tile != 384U; tile++) {
        for (row = 0U; row != 8U; row++) {
            const word address = 0x8000U + (tile << 4) + (row << 1);
            const word low = (tile * 17U) ^ (row * 29U) ^ 0x5AU;
            const word high = (tile * 11U) ^ (row * 43U) ^ 0xA5U;
            write_byte(fixture, address, low);
            write_byte(fixture, address + 1U, high);
        }
    }

    for (offset = 0U; offset != 0x400U; offset++) {
        write_byte(fixture, 0x9800U + offset, (offset * 7U) & 0xFFU);
        write_byte(fixture, 0x9C00U + offset, (offset * 13U) & 0xFFU);
    }

    /* Eleven candidates verify that the line scanner retains only the first
     * ten, while flips, palettes and edge X positions exercise composition. */
    for (tile = 0U; tile != 11U; tile++) {
        const word base = 0xFE00U + (tile << 2);
        const word x = (tile == 0U) ? 0U :
            (tile == 1U) ? 1U : (tile == 10U) ? 168U : (8U + tile * 14U);
        write_byte(fixture, base + 0U, 16U + (tile & 7U));
        write_byte(fixture, base + 1U, x);
        write_byte(fixture, base + 2U, 4U + tile);
        write_byte(fixture, base + 3U,
            ((tile & 1U) ? 0x20U : 0U) |
            ((tile & 2U) ? 0x40U : 0U) |
            ((tile & 4U) ? 0x10U : 0U));
    }
}

static void mutate_visual_fixture(struct ppu_fixture *fixture)
{
    word index;

    /* Tile pattern and both tile maps. */
    write_byte(fixture, 0x8052U, 0xFFU);
    write_byte(fixture, 0x8053U, 0x00U);
    write_byte(fixture, 0x9821U, 0x05U);
    write_byte(fixture, 0x9C42U, 0x92U);

    /* The normal IO route must invalidate palette/scroll/window/LCDC state. */
    write_byte(fixture, 0xFF47U, 0x1BU);
    write_byte(fixture, 0xFF48U, 0xE4U);
    write_byte(fixture, 0xFF49U, 0xC9U);
    write_byte(fixture, 0xFF42U, 0xFFU);
    write_byte(fixture, 0xFF43U, 0x07U);
    write_byte(fixture, 0xFF4AU, 0x10U);
    write_byte(fixture, 0xFF4BU, 0x07U);
    /* Signed tile addressing plus the alternate BG map. */
    write_byte(fixture, 0xFF40U, 0xE7U);

    /* Direct OAM write followed by a complete FF46 transfer from WRAM. */
    write_byte(fixture, 0xFE03U, 0x70U);
    for (index = 0U; index != 0xA0U; index++) {
        write_byte(fixture, 0xC000U + index,
            ((index * 37U) + 19U) & 0xFFU);
    }
    /* Restore a visible, varied sprite prefix in the DMA source. */
    for (index = 0U; index != 10U; index++) {
        const word base = index << 2;
        fixture->wram[base + 0U] = 16U + (index & 7U);
        fixture->wram[base + 1U] = 8U + index * 15U;
        fixture->wram[base + 2U] = 0x10U + index;
        fixture->wram[base + 3U] = (index & 1U) ? 0x60U : 0x10U;
    }
    write_byte(fixture, 0xFF46U, 0xC0U);
}

#if defined(KNOVGBC_PPU_DIRTY_REGRESSION)
/* Each case changes one dependency in isolation. Comparing only the final
 * frame after a bundle of writes can hide a missing invalidation, because a
 * later LCDC/palette/OAM write may conservatively dirty the whole screen. */
static int report_dirty_case(struct ppu_fixture *fixture, const char *name)
{
    if (!run_three_frames(fixture) || !guards_valid(fixture)) {
        fprintf(stderr, "%s: frame failed or corrupted guard\n", name);
        return 0;
    }

    printf("ppu-dirty %s=%016llx\n", name,
        (unsigned long long)framebuffer_hash(fixture));
    return 1;
}

static int run_dirty_scanline_cases(struct ppu_fixture *fixture)
{
    word index;
    word guard;

    if (!report_dirty_case(fixture, "initial") ||
        !report_dirty_case(fixture, "idle")) {
        return 0;
    }

    write_byte(fixture, 0x9800U, 0x31U);
    if (!report_dirty_case(fixture, "bg-map")) return 0;

    write_byte(fixture, 0x9C00U, 0x52U);
    if (!report_dirty_case(fixture, "window-map")) return 0;

    write_byte(fixture, 0x800AU, 0xFFU);
    if (!report_dirty_case(fixture, "tile-pattern")) return 0;

    write_byte(fixture, 0xFE05U, 8U);
    if (!report_dirty_case(fixture, "direct-oam")) return 0;

    write_byte(fixture, 0xFE04U, 64U);
    if (!report_dirty_case(fixture, "direct-oam-y")) return 0;

    for (index = 0U; index != 0xA0U; index++) {
        fixture->wram[index] = fixture->oam[index];
    }
    /* The first OAM byte stays identical. The DMA notifier must still find
     * and invalidate an object changed later in the 160-byte transfer. */
    fixture->wram[4U] = 96U;
    fixture->wram[5U] = 24U;
    if (fixture->wram[0U] != fixture->oam[0U] ||
        fixture->wram[4U] == fixture->oam[4U]) {
        fprintf(stderr, "OAM DMA fixture did not isolate a later object\n");
        return 0;
    }
    write_byte(fixture, 0xFF46U, 0xC0U);
    if (!report_dirty_case(fixture, "oam-dma")) return 0;

    write_byte(fixture, 0xFF47U, 0x1BU);
    if (!report_dirty_case(fixture, "bg-palette")) return 0;

    write_byte(fixture, 0xFF48U, 0xE4U);
    if (!report_dirty_case(fixture, "object-palette")) return 0;

    write_byte(fixture, 0xFF43U, 7U);
    if (!report_dirty_case(fixture, "scroll-x")) return 0;

    write_byte(fixture, 0xFF42U, 8U);
    if (!report_dirty_case(fixture, "scroll-y")) return 0;

    write_byte(fixture, 0xFF4BU, 32U);
    if (!report_dirty_case(fixture, "window-x")) return 0;

    write_byte(fixture, 0xFF4AU, 50U);
    if (!report_dirty_case(fixture, "window-y")) return 0;

    write_byte(fixture, 0xFF40U, 0xFFU);
    if (!report_dirty_case(fixture, "lcdc-map")) return 0;

    /* Repeat object movement with 8-pixel height, then restore 16 pixels.
     * The two footprints must be invalidated using the active LCDC size. */
    write_byte(fixture, 0xFF40U, 0xFBU);
    if (!report_dirty_case(fixture, "sprite-height-8")) return 0;
    write_byte(fixture, 0xFE04U, 40U);
    if (!report_dirty_case(fixture, "oam-y-height-8")) return 0;
    write_byte(fixture, 0xFF40U, 0xFFU);
    if (!report_dirty_case(fixture, "sprite-height-16")) return 0;

    /* A write after scanline 72 has already been drawn must be remembered for
     * the next visual pass, including across the intervening frameskip pass. */
    for (guard = 0U; guard != 400U &&
        (fixture->ppu.state.scanY != 72U ||
         (fixture->ppu.state.interlace & 3U) != 3U); guard++) {
        (void)ppu_tick(&fixture->ppu, 456U);
    }
    if (guard == 400U) {
        fprintf(stderr, "mid-frame scanline was not reached\n");
        return 0;
    }
    /* New sprite rows 16..31 were drawn before this write; old rows 24..39
     * may also have been drawn. The following visual pass must erase/repaint
     * both footprints rather than preserving the previous image. */
    write_byte(fixture, 0xFE04U, 32U);
    if (!report_dirty_case(fixture, "mid-frame-oam-y")) return 0;

    for (guard = 0U; guard != 400U &&
        (fixture->ppu.state.scanY != 72U ||
         (fixture->ppu.state.interlace & 3U) != 3U); guard++) {
        (void)ppu_tick(&fixture->ppu, 456U);
    }
    if (guard == 400U) {
        fprintf(stderr, "second mid-frame scanline was not reached\n");
        return 0;
    }
    write_byte(fixture, 0x9C20U, 0x71U);
    if (!report_dirty_case(fixture, "mid-frame-map")) return 0;

    return report_dirty_case(fixture, "final-idle");
}
#endif

int main(void)
{
    struct ppu_fixture fixture;
    uint64_t before;
    uint64_t after;
    uint64_t idle;

    if (!fixture_init(&fixture)) {
        fprintf(stderr, "fixture guard setup failed\n");
        return 1;
    }

    install_visual_fixture(&fixture);
#if defined(KNOVGBC_PPU_DIRTY_REGRESSION)
    return run_dirty_scanline_cases(&fixture) ? 0 : 2;
#endif
    if (!run_three_frames(&fixture) || !guards_valid(&fixture)) {
        fprintf(stderr, "initial PPU frame failed or corrupted guard\n");
        return 2;
    }
    before = framebuffer_hash(&fixture);

    mutate_visual_fixture(&fixture);
    if (!run_three_frames(&fixture) || !guards_valid(&fixture)) {
        fprintf(stderr, "mutated PPU frame failed or corrupted guard\n");
        return 3;
    }
    after = framebuffer_hash(&fixture);

    /* A complete rendering opportunity with no visual writes must retain the
     * same pixels. The optimized path additionally proves it requested no
     * unnecessary framebuffer upload. */
    if (!run_three_frames(&fixture) || !guards_valid(&fixture)) {
        fprintf(stderr, "idle PPU frame failed or corrupted guard\n");
        return 4;
    }
    idle = framebuffer_hash(&fixture);
    if (idle != after) {
        fprintf(stderr, "idle PPU frame changed pixels\n");
        return 5;
    }
#if defined(KNOVGBC_PPU_FULL_OPT)
    if (fixture.ppu.dmg_frame_changed != 0U) {
        fprintf(stderr, "unchanged render still marked the frame dirty\n");
        return 6;
    }
#endif

    printf("ppu-regression before=%016llx after=%016llx idle=%016llx\n",
        (unsigned long long)before, (unsigned long long)after,
        (unsigned long long)idle);
    return 0;
}
