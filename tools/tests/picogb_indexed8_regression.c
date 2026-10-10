/* Compare PicoGB's indexed DMG output with its established RGB1555 output.
 * Each executable runs identical PPU writes; indexed pixels are translated
 * through the fixed Saturn CRAM shades before the image hashes are printed.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fabric.h"
#include "microcode.h"
#include "microcode_dispatch.h"
#include "mi.h"
#include "ppu.h"

#define VISIBLE_WIDTH 160U
#define VISIBLE_HEIGHT 144U
#define FRAME_STRIDE 176U
#define SPLIT_LINE 12U
#define GUARD 0xA5U

#if defined(KNOVGBC_VDP2_INDEXED8)
_Static_assert(PPU_MODE == 8 && sizeof(pixel_t) == 1,
    "indexed regression requires the Saturn eight-bit PPU");
#else
_Static_assert(PPU_MODE == 7 && sizeof(pixel_t) == 2,
    "reference regression requires the Saturn RGB1555 PPU");
#endif

static const uint16_t rgb1555_shades[4] = {
    0x0000U, 0x294AU, 0x56B5U, 0x7FFFU
};

struct fixture {
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
    pixel_t pixels[2 + VISIBLE_HEIGHT * FRAME_STRIDE + 2];
    pixel_t *lines[VISIBLE_HEIGHT];
};

struct digest {
    uint64_t full;
    uint64_t top;
    uint64_t bottom;
};

enum variant {
    BASE,
    PALETTES,
    OBP_ONLY,
    VRAM_CHANGED,
    SPRITES_OFF,
    PALETTES_MID_FRAME,
    VARIANT_COUNT
};

static struct fixture fixture;

static void write_byte(word address, word value)
{
    mch_memory_dispatch_write(&fixture.mb, address, value);
}

static bool guards_valid(void)
{
    const size_t trailing = 2U + VISIBLE_HEIGHT * FRAME_STRIDE;
    return fixture.pixels[0] == GUARD && fixture.pixels[1] == GUARD &&
        fixture.pixels[trailing] == GUARD &&
        fixture.pixels[trailing + 1U] == GUARD;
}

static void fixture_init(void)
{
    memset(&fixture, 0, sizeof(fixture));
    fixture.pixels[0] = fixture.pixels[1] = GUARD;
    fixture.pixels[2U + VISIBLE_HEIGHT * FRAME_STRIDE] = GUARD;
    fixture.pixels[3U + VISIBLE_HEIGHT * FRAME_STRIDE] = GUARD;

    for (word y = 0; y < VISIBLE_HEIGHT; y++) {
        fixture.lines[y] = &fixture.pixels[2U + y * FRAME_STRIDE];
    }

    fixture.memory.ROM = fixture.rom;
    fixture.memory.WRAM = fixture.wram;
    fixture.memory.VRAM = fixture.vram;
    fixture.memory.SRAM = fixture.sram;
    fixture.memory.HRAM = fixture.hram;
    fixture.memory.OAM = fixture.oam;
    fixture.memory.BANK_ROM = 1U;
    fixture.memory.BANK_WRAM = 1U;
    fixture.memory.BANK_VRAM = 0U;
    fixture.memory.BANK_SRAM = 0U;
    fixture.memory.N_ROM = 2U;
    fixture.memory.N_SRAM = 1U;
    fixture.memory.dispatch_ROM = pgf_cb_ROM_Dummy;
    fixture.memory.dispatch_IO = pgf_cb_IO_;
    fixture.memory.userdata = &fixture.fabric;

    fixture.mb.mi = &fixture.memory;
    micache_invalidate(&fixture.mb.micache);
    fixture.fabric.mb = &fixture.mb;
    fixture.fabric.ppu = &fixture.ppu;
    fixture.fabric.apu = NULL;

    ppu_initialize(&fixture.ppu);
    fixture.ppu.VRAM = fixture.vram;
    fixture.ppu.OAM = fixture.oam;
    fixture.ppu.state.framebuffer = fixture.lines;
    fixture.ppu.state.scanY = 0U;
    fixture.ppu.next_update_n = 0U;
    fixture.ppu.next_update_ticks = 0U;
    fixture.ppu.state.interlace = 3U;

    /* BG, window and 8x16 sprites all take part in the output. */
    fixture.ppu.rLCDC = 0xF7U;
    fixture.ppu.rSTAT = 0x85U;
    fixture.ppu.rSCX = 3U;
    fixture.ppu.rSCY = 5U;
    fixture.ppu.rWX = 70U;
    fixture.ppu.rWY = 40U;
    fixture.ppu.rBGP = 0xE4U;
    fixture.ppu.rOBP0 = 0xD2U;
    fixture.ppu.rOBP1 = 0x39U;
    ppu_dmg_palette_cache_refresh(&fixture.ppu);
}

static void install_picture(void)
{
    for (word tile = 0; tile < 384U; tile++) {
        for (word row = 0; row < 8U; row++) {
            const word address = 0x8000U + (tile << 4) + (row << 1);
            write_byte(address, (tile * 17U) ^ (row * 29U) ^ 0x5AU);
            write_byte(address + 1U, (tile * 11U) ^ (row * 43U) ^ 0xA5U);
        }
    }

    for (word offset = 0; offset < 0x400U; offset++) {
        write_byte(0x9800U + offset, (offset * 7U) & 0xFFU);
        write_byte(0x9C00U + offset, (offset * 13U) & 0xFFU);
    }

    /* Edge X values, flips and both object palettes are represented. */
    for (word sprite = 0; sprite < 11U; sprite++) {
        const word base = 0xFE00U + (sprite << 2);
        const word x = sprite == 0U ? 0U : sprite == 1U ? 1U :
            sprite == 10U ? 168U : 8U + sprite * 14U;
        write_byte(base, 16U + (sprite & 7U));
        write_byte(base + 1U, x);
        write_byte(base + 2U, 4U + sprite);
        write_byte(base + 3U,
            ((sprite & 1U) ? 0x20U : 0U) |
            ((sprite & 2U) ? 0x40U : 0U) |
            ((sprite & 4U) ? 0x10U : 0U));
    }
}

static void change_palettes(void)
{
    write_byte(0xFF47U, 0x1BU);
    write_byte(0xFF48U, 0xE4U);
    write_byte(0xFF49U, 0xC9U);
}

static bool run_to_vblank(void)
{
    for (word line = 0; line < 200U; line++) {
        (void)ppu_tick(&fixture.ppu, 456U);
        if (fixture.ppu._redrawed != 0U) {
            return guards_valid();
        }
    }
    return false;
}

static uint64_t hash_u16(uint64_t hash, uint16_t pixel)
{
    hash = (hash ^ (uint8_t)pixel) * UINT64_C(1099511628211);
    return (hash ^ (uint8_t)(pixel >> 8)) * UINT64_C(1099511628211);
}

static bool digest_frame(struct digest *digest)
{
    const uint64_t initial = UINT64_C(14695981039346656037);
    digest->full = digest->top = digest->bottom = initial;

    for (word y = 0; y < VISIBLE_HEIGHT; y++) {
        for (word x = 0; x < VISIBLE_WIDTH; x++) {
            const pixel_t pixel = fixture.lines[y][x + 8U];
            uint16_t rgb;
#if defined(KNOVGBC_VDP2_INDEXED8)
            if (pixel >= 4U) {
                fprintf(stderr, "index out of range at (%u,%u): %u\n",
                    x, y, (unsigned)pixel);
                return false;
            }
            rgb = rgb1555_shades[pixel];
#else
            rgb = pixel;
            if (rgb != rgb1555_shades[0] && rgb != rgb1555_shades[1] &&
                rgb != rgb1555_shades[2] && rgb != rgb1555_shades[3]) {
                fprintf(stderr, "unexpected RGB1555 at (%u,%u): %04x\n",
                    x, y, rgb);
                return false;
            }
#endif
            digest->full = hash_u16(digest->full, rgb);
            if (y < SPLIT_LINE) {
                digest->top = hash_u16(digest->top, rgb);
            }
            else {
                digest->bottom = hash_u16(digest->bottom, rgb);
            }
        }
    }
    return true;
}

static bool run_variant(enum variant variant, struct digest *digest)
{
    fixture_init();
    install_picture();

    switch (variant) {
        case PALETTES:
            change_palettes();
            break;
        case OBP_ONLY:
            write_byte(0xFF48U, 0xE4U);
            write_byte(0xFF49U, 0xC9U);
            break;
        case VRAM_CHANGED:
            for (word row = 0; row < 8U; row++) {
                write_byte(0x8000U + row * 2U, 0xFFU);
                write_byte(0x8001U + row * 2U, 0xFFU);
            }
            write_byte(0x9821U, 0x05U);
            write_byte(0x9C42U, 0x92U);
            break;
        case SPRITES_OFF:
            write_byte(0xFF40U, fixture.ppu.rLCDC & ~2U);
            break;
        default:
            break;
    }

    if (variant == PALETTES_MID_FRAME) {
        for (word line = 0; line < SPLIT_LINE; line++) {
            (void)ppu_tick(&fixture.ppu, 456U);
        }
        if (fixture.ppu.state.scanY != SPLIT_LINE ||
            fixture.ppu._redrawed != 0U) {
            fprintf(stderr, "palette write did not land at scanline %u\n",
                SPLIT_LINE);
            return false;
        }
        change_palettes();
    }

    if (!run_to_vblank() || !digest_frame(digest)) {
        fprintf(stderr, "frame failed for variant %u\n", (unsigned)variant);
        return false;
    }
    return true;
}

int main(void)
{
    static const char *const names[VARIANT_COUNT] = {
        "base", "palettes", "obp", "vram", "no_sprites", "mid_frame"
    };
    struct digest result[VARIANT_COUNT];

    for (enum variant variant = BASE; variant < VARIANT_COUNT; variant++) {
        if (!run_variant(variant, &result[variant])) {
            return 1;
        }
    }

    if (result[BASE].full == result[PALETTES].full ||
        result[BASE].full == result[OBP_ONLY].full ||
        result[BASE].full == result[VRAM_CHANGED].full ||
        result[BASE].full == result[SPRITES_OFF].full ||
        result[PALETTES_MID_FRAME].top != result[BASE].top ||
        result[PALETTES_MID_FRAME].bottom != result[PALETTES].bottom) {
        fprintf(stderr, "fixture failed to exercise palette, VRAM or sprite changes\n");
        return 2;
    }

    for (enum variant variant = BASE; variant < VARIANT_COUNT; variant++) {
        printf("%s full=%016llx top=%016llx bottom=%016llx\n",
            names[variant],
            (unsigned long long)result[variant].full,
            (unsigned long long)result[variant].top,
            (unsigned long long)result[variant].bottom);
    }
    return 0;
}
