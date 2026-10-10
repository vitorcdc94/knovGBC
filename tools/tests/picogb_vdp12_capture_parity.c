/* Compare the native VDP2+VDP1 capture with kmvpe048's indexed framebuffer.
 * In this host-only build, the production PPU writes both outputs at each
 * scanline. A tiny model applies the captured sprite strips in their recorded
 * order and checks every visible pixel, including mid-frame state changes. */
#define main picogb_indexed8_regression_main
#include "picogb_indexed8_regression.c"
#undef main

#include "picogb_vdp12_frame.h"

static uint8_t native_image[VISIBLE_HEIGHT][VISIBLE_WIDTH];

static bool
compare_capture(const char *name)
{
    const picogb_vdp12_frame_t *const frame = picogb_vdp12_frame_get();

    if (frame->valid != 1U) {
        fprintf(stderr, "%s: capture invalid\n", name);
        return false;
    }
    if (frame->sprite_count > PICOGB_VDP12_MAX_SPRITE_STRIPS) {
        fprintf(stderr, "%s: sprite count exceeded capacity\n", name);
        return false;
    }
    for (unsigned row = 0U; row < PICOGB_VDP12_TILE_ROWS; row++) {
        if (frame->captured_lines[row] != 0xFFU) {
            fprintf(stderr, "%s: row %u captured mask is %02X\n",
                name, row, frame->captured_lines[row]);
            return false;
        }
    }

    for (unsigned y = 0U; y < VISIBLE_HEIGHT; y++) {
        for (unsigned x = 0U; x < VISIBLE_WIDTH; x++) {
            const uint8_t *const tile = frame->bg_tiles[
                (y >> 3U) * PICOGB_VDP12_TILE_COLS + (x >> 3U)];
            const uint8_t packed = tile[((y & 7U) << 2U) +
                ((x & 7U) >> 1U)];
            const uint8_t shade = (x & 1U) ?
                (packed & 15U) : (packed >> 4U);
            if (shade < 1U || shade > 4U) {
                fprintf(stderr,
                    "%s: invalid BG shade %u at (%u,%u)\n",
                    name, shade, x, y);
                return false;
            }
            native_image[y][x] = (uint8_t)(shade - 1U);
        }
    }

    for (unsigned i = 0U; i < frame->sprite_count; i++) {
        const picogb_vdp12_sprite_strip_t *const strip =
            &frame->sprite_strips[i];
        if (strip->y >= VISIBLE_HEIGHT) {
            fprintf(stderr, "%s: invalid sprite y=%u\n", name, strip->y);
            return false;
        }
        for (unsigned p = 0U; p < 8U; p++) {
            const int x = (int)strip->x + (int)p;
            const uint8_t packed = strip->pixels[p >> 1U];
            const uint8_t shade = (p & 1U) ?
                (packed & 15U) : (packed >> 4U);
            if (shade > 4U) {
                fprintf(stderr,
                    "%s: invalid sprite shade %u in strip %u\n",
                    name, shade, i);
                return false;
            }
            if (shade != 0U && x >= 0 && x < (int)VISIBLE_WIDTH) {
                native_image[strip->y][x] = (uint8_t)(shade - 1U);
            }
        }
    }

    if (!guards_valid()) {
        fprintf(stderr, "%s: software framebuffer guard corrupted\n", name);
        return false;
    }
    for (unsigned y = 0U; y < VISIBLE_HEIGHT; y++) {
        for (unsigned x = 0U; x < VISIBLE_WIDTH; x++) {
            const uint8_t software = fixture.lines[y][x + 8U];
            const uint8_t native = native_image[y][x];
            if (software != native) {
                fprintf(stderr,
                    "%s: mismatch at (%u,%u): 048=%u VDP12=%u\n",
                    name, x, y, software, native);
                return false;
            }
        }
    }
    printf("%s: %ux%u pixels equal, %u sprite strips\n",
        name, VISIBLE_WIDTH, VISIBLE_HEIGHT, frame->sprite_count);
    return true;
}

static bool
run_fixture_variant(enum variant variant, const char *name)
{
    struct digest digest;
    if (!run_variant(variant, &digest)) return false;
    return compare_capture(name);
}

static bool
run_midframe_case(const char *name, unsigned modification)
{
    struct digest digest;
    fixture_init();
    install_picture();

    for (unsigned line = 0U; line < SPLIT_LINE; line++) {
        (void)ppu_tick(&fixture.ppu, 456U);
    }
    if (fixture.ppu.state.scanY != SPLIT_LINE ||
        fixture.ppu._redrawed != 0U) {
        fprintf(stderr, "%s: modification missed line %u\n",
            name, SPLIT_LINE);
        return false;
    }

    if (modification == 0U) {
        write_byte(0xFF42U, 0x2BU); /* SCY */
        write_byte(0xFF43U, 0x79U); /* SCX */
        write_byte(0xFF4AU, 0x08U); /* WY */
        write_byte(0xFF4BU, 0x31U); /* WX */
    }
    else {
        for (unsigned row = 0U; row < 8U; row++) {
            write_byte(0x8000U + row * 2U, 0xFFU);
            write_byte(0x8001U + row * 2U, 0x00U);
        }
        write_byte(0x9800U, 0x00U);
        write_byte(0xFE01U, 0x34U); /* sprite X */
        write_byte(0xFE03U, 0x70U); /* flip / palette */
    }

    if (!run_to_vblank() || !digest_frame(&digest)) {
        fprintf(stderr, "%s: frame did not complete\n", name);
        return false;
    }
    return compare_capture(name);
}

static bool
run_signed_tiles_and_overlap(void)
{
    struct digest digest;
    fixture_init();
    install_picture();
    write_byte(0xFF40U, (fixture.ppu.rLCDC & ~0x30U));
    for (unsigned i = 0U; i < 3U; i++) {
        const word base = 0xFE00U + (i << 2U);
        write_byte(base, 0x30U);
        write_byte(base + 1U, 0x38U);
        write_byte(base + 2U, 4U + i);
        write_byte(base + 3U, (i == 1U) ? 0x30U :
            (i == 2U) ? 0xC0U : 0U);
    }
    if (!run_to_vblank() || !digest_frame(&digest)) return false;
    return compare_capture("signed_tiles_overlap");
}

static bool
run_sprite_edges(void)
{
    struct digest digest;
    fixture_init();
    install_picture();
    const word positions[4] = { 0U, 1U, 168U, 167U };
    for (unsigned i = 0U; i < 4U; i++) {
        const word base = 0xFE00U + (i << 2U);
        write_byte(base, 48U); /* visible on scanline 32 */
        write_byte(base + 1U, positions[i]);
        write_byte(base + 2U, 4U + i);
        write_byte(base + 3U, 0U);
    }
    if (!run_to_vblank() || !digest_frame(&digest) ||
        !compare_capture("sprite_edges")) return false;

    bool left = false;
    bool right = false;
    const picogb_vdp12_frame_t *const frame = picogb_vdp12_frame_get();
    for (unsigned i = 0U; i < frame->sprite_count; i++) {
        const picogb_vdp12_sprite_strip_t *const strip =
            &frame->sprite_strips[i];
        if (strip->y != 32U) continue;
        if (strip->oam_index == 0U || strip->oam_index == 2U) {
            fprintf(stderr, "sprite_edges: off-screen OAM %u was captured\n",
                strip->oam_index);
            return false;
        }
        if (strip->oam_index == 1U && strip->x == -7) left = true;
        if (strip->oam_index == 3U && strip->x == 159) right = true;
    }
    if (!left || !right) {
        fprintf(stderr,
            "sprite_edges: clipped strips absent (left=%u right=%u)\n",
            left, right);
        return false;
    }
    return true;
}

static bool
run_partial_frame_invalid(void)
{
    picogb_vdp12_frame_reset();
    if (picogb_vdp12_frame_get()->valid != 0U) {
        fputs("partial frame incorrectly marked valid\n", stderr);
        return false;
    }
    puts("partial_frame: correctly invalid");
    return true;
}

int
main(void)
{
    static const char *const names[VARIANT_COUNT] = {
        "base", "palettes", "obp", "vram", "no_sprites", "mid_frame_palette"
    };

    for (enum variant variant = BASE; variant < VARIANT_COUNT; variant++) {
        if (!run_fixture_variant(variant, names[variant])) return 1;
    }
    if (!run_midframe_case("mid_frame_scroll_window", 0U) ||
        !run_midframe_case("mid_frame_vram_oam", 1U) ||
        !run_signed_tiles_and_overlap() || !run_sprite_edges() ||
        !run_partial_frame_invalid()) return 1;
    puts("picogb_vdp12_capture_parity: PASS");
    return 0;
}
