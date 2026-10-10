/* Exercise the production VDP2 uploader against a full-copy reference. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

enum {
    BITMAP_WIDTH = 512,
    BITMAP_HEIGHT = 256,
    BITMAP_X = (BITMAP_WIDTH - KNOVGB_WIDTH) / 2,
    BITMAP_Y = (BITMAP_HEIGHT - KNOVGB_HEIGHT) / 2,
    PADDED_STRIDE = 176,
    UNALIGNED_STRIDE = 161
};

uint16_t test_vdp2_vram[BITMAP_WIDTH * BITMAP_HEIGHT];
static uint16_t expected_vram[BITMAP_WIDTH * BITMAP_HEIGHT];
static uint16_t padded[KNOVGB_HEIGHT * PADDED_STRIDE];
static uint16_t tight[KNOVGB_HEIGHT * KNOVGB_WIDTH];
static _Alignas(4) uint16_t unaligned_storage[
    1 + KNOVGB_HEIGHT * UNALIGNED_STRIDE];

static void
fail(const char *stage, uint32_t index, uint16_t actual, uint16_t expected)
{
    fprintf(stderr, "%s: bitmap[%u] got %04X, expected %04X\n",
        stage, index, actual, expected);
    exit(1);
}

static void
expected_clear(void)
{
    for (uint32_t i = 0; i < BITMAP_WIDTH * BITMAP_HEIGHT; i++) {
        expected_vram[i] = 0x8000U;
    }
}

static void
reference_present(const uint16_t *source, uint32_t stride)
{
    for (uint32_t y = 0; y < KNOVGB_HEIGHT; y++) {
        for (uint32_t x = 0; x < KNOVGB_WIDTH; x++) {
            expected_vram[(BITMAP_Y + y) * BITMAP_WIDTH + BITMAP_X + x] =
                source[y * stride + x] | 0x8000U;
        }
    }
}

static void
check_bitmap(const char *stage)
{
    for (uint32_t i = 0; i < BITMAP_WIDTH * BITMAP_HEIGHT; i++) {
        if (test_vdp2_vram[i] != expected_vram[i]) {
            fail(stage, i, test_vdp2_vram[i], expected_vram[i]);
        }
    }
}

static void
present_and_check(const char *stage, const uint16_t *source,
    uint32_t stride)
{
    if (stride == KNOVGB_WIDTH) {
        saturn_platform_present(source);
    }
    else {
        saturn_platform_present_strided(source, stride);
    }
    reference_present(source, stride);
    check_bitmap(stage);
}

static void
fill_source(uint16_t *source, uint32_t stride, uint32_t seed)
{
    for (uint32_t y = 0; y < KNOVGB_HEIGHT; y++) {
        for (uint32_t x = 0; x < stride; x++) {
            source[y * stride + x] = (uint16_t)(
                (seed * 53U + y * 97U + x * 41U) & 0x7FFFU);
        }
    }
}

static void
test_no_redundant_writes(void)
{
    const uint32_t same = (BITMAP_Y + 9U) * BITMAP_WIDTH + BITMAP_X;
    const uint32_t changed = (BITMAP_Y + 10U) * BITMAP_WIDTH + BITMAP_X;
    const uint32_t skipped = (BITMAP_Y + 11U) * BITMAP_WIDTH + BITMAP_X;

    saturn_platform_video_init();
    saturn_platform_present_strided(padded, PADDED_STRIDE);

    /* A VDP2 sentinel distinguishes no write from an identical rewrite. */
    test_vdp2_vram[same] = 0x1234U;
    saturn_platform_present_strided(padded, PADDED_STRIDE);
    if (test_vdp2_vram[same] != 0x1234U) {
        fail("identical row was rewritten", same, test_vdp2_vram[same],
            0x1234U);
    }

    /* A logical frame changed, but frameskip kept it off the display.  The
     * final source equals the last displayed image, so this row must stay
     * untouched despite the intermediate mutation. */
    padded[11U * PADDED_STRIDE] ^= 0x3333U;
    padded[11U * PADDED_STRIDE] ^= 0x3333U;
    test_vdp2_vram[skipped] = 0x3456U;
    saturn_platform_present_strided(padded, PADDED_STRIDE);
    if (test_vdp2_vram[skipped] != 0x3456U) {
        fail("skipped logical frame replaced presented image", skipped,
            test_vdp2_vram[skipped], 0x3456U);
    }

    test_vdp2_vram[changed] = 0x5678U;
    padded[10U * PADDED_STRIDE + 80U] ^= 0x0010U;
    saturn_platform_present_strided(padded, PADDED_STRIDE);
    if (test_vdp2_vram[same] != 0x1234U) {
        fail("unmodified row was rewritten", same, test_vdp2_vram[same],
            0x1234U);
    }
    if (test_vdp2_vram[changed] !=
        (uint16_t)(padded[10U * PADDED_STRIDE] | 0x8000U)) {
        fail("changed row was not uploaded", changed,
            test_vdp2_vram[changed],
            padded[10U * PADDED_STRIDE] | 0x8000U);
    }

    /* Reinitialization must invalidate the remembered image. */
    saturn_platform_video_init();
    saturn_platform_present_strided(padded, PADDED_STRIDE);
    expected_clear();
    reference_present(padded, PADDED_STRIDE);
    check_bitmap("reinitialization");
}

int
main(void)
{
    fill_source(padded, PADDED_STRIDE, 1U);
    fill_source(tight, KNOVGB_WIDTH, 2U);

    saturn_platform_video_init();
    expected_clear();
    check_bitmap("clear");
    present_and_check("first padded frame", padded, PADDED_STRIDE);
    present_and_check("identical frame", padded, PADDED_STRIDE);

    /* Padding and guard pixels do not belong to the visible Game Boy image. */
    padded[1U * PADDED_STRIDE + KNOVGB_WIDTH] ^= 0x4321U;
    present_and_check("padding only", padded, PADDED_STRIDE);

    padded[0] ^= 0x0011U;
    present_and_check("top-left pixel", padded, PADDED_STRIDE);
    padded[(KNOVGB_HEIGHT - 1U) * PADDED_STRIDE + KNOVGB_WIDTH - 1U]
        ^= 0x0101U;
    present_and_check("bottom-right pixel", padded, PADDED_STRIDE);

    /* These Game Boy rows straddle the VDP2 A0/A1 bitmap-bank boundary. */
    padded[71U * PADDED_STRIDE + 79U] ^= 0x0100U;
    present_and_check("bank A0 edge", padded, PADDED_STRIDE);
    padded[72U * PADDED_STRIDE + 80U] ^= 0x1000U;
    present_and_check("bank A1 start", padded, PADDED_STRIDE);
    padded[72U * PADDED_STRIDE + 80U] ^= 0x1000U;
    present_and_check("reverted change", padded, PADDED_STRIDE);

    /* The core may change the same source twice before a frameskipped upload. */
    padded[33U * PADDED_STRIDE + 33U] ^= 0x1111U;
    padded[44U * PADDED_STRIDE + 44U] ^= 0x2222U;
    present_and_check("visual frameskip", padded, PADDED_STRIDE);
    padded[44U * PADDED_STRIDE + 44U] ^= 0x2222U;
    present_and_check("frame after frameskip", padded, PADDED_STRIDE);

    padded[20U * PADDED_STRIDE + 20U] ^= 0x8000U;
    present_and_check("source high-bit only", padded, PADDED_STRIDE);

    saturn_platform_present_strided(NULL, PADDED_STRIDE);
    saturn_platform_present_strided(padded, KNOVGB_WIDTH - 1U);
    check_bitmap("invalid calls");

    present_and_check("tight present wrapper", tight, KNOVGB_WIDTH);
    fill_source(unaligned_storage + 1U, UNALIGNED_STRIDE, 3U);
    if (((uintptr_t)(unaligned_storage + 1U) & 3U) != 2U) {
        fprintf(stderr, "test source did not get 2-byte alignment\n");
        return 1;
    }
    present_and_check("unaligned source", unaligned_storage + 1U,
        UNALIGNED_STRIDE);
    present_and_check("identical unaligned source", unaligned_storage + 1U,
        UNALIGNED_STRIDE);

    test_no_redundant_writes();
    puts("vdp2_dirty_lines_test: PASS");
    return 0;
}
