/* Compile the production presenter here so the host can inspect SCU DMA
 * descriptors before its 32-bit Saturn addresses truncate host pointers. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yaul.h"
#include "platform.h"

enum {
    TEST_BITMAP_WIDTH = 512,
    TEST_BITMAP_HEIGHT = 256,
    TEST_BITMAP_X = (TEST_BITMAP_WIDTH - KNOVGB_WIDTH) / 2,
    TEST_BITMAP_Y = (TEST_BITMAP_HEIGHT - KNOVGB_HEIGHT) / 2,
    TEST_BITMAP_BYTES = TEST_BITMAP_WIDTH * TEST_BITMAP_HEIGHT,
    TEST_PADDED_STRIDE = 176,
    TEST_ODD_STRIDE = 161,
    TEST_ROW_BYTES = KNOVGB_WIDTH
};

/* The shared Yaul mock declares this symbol as uint16_t. The test checks
 * every byte, including a guard bank after the 128 KiB indexed bitmap. */
_Alignas(4) uint16_t test_vdp2_vram[TEST_BITMAP_BYTES];
uint16_t test_vdp2_cram[2048U];
vdp2_scrn_bitmap_format_t test_bitmap_format;
vdp2_vram_usage_t test_vram_usage;
vdp2_vram_cycp_t test_vram_cycles;
int test_display_flags;
int test_scroll_x;
int test_scroll_y;
#if defined(KNOVGBC_VDP2_ZOOM)
int test_zoom_x;
int test_zoom_y;
#endif
uint32_t test_cache_purge_count;

static uint8_t expected_vram[sizeof(test_vdp2_vram)];
static _Alignas(4) uint8_t padded[KNOVGB_HEIGHT * TEST_PADDED_STRIDE];
static _Alignas(4) uint8_t tight[KNOVGB_HEIGHT * KNOVGB_WIDTH];
static _Alignas(4) uint8_t unaligned_storage[
    1U + KNOVGB_HEIGHT * TEST_ODD_STRIDE];
static uint8_t source_snapshot[KNOVGB_HEIGHT * TEST_PADDED_STRIDE];

#include "../../src/saturn/platform_video.c"

static scu_dma_level_cfg_t configured;
static scu_dma_level_t configured_level;
static bool configured_valid;
static bool pending_wait;
static const uint8_t *active_source;
static uint32_t active_stride;
static uint32_t rows_seen;
static uint32_t starts;
static uint32_t waits;

static uint8_t *
bitmap_bytes(void)
{
    return (uint8_t *)(void *)test_vdp2_vram;
}

static void
fail_text(const char *message)
{
    fprintf(stderr, "vdp2_dma_indexed8_test: %s\n", message);
    exit(1);
}

static void
expect_u32(const char *stage, uint32_t index, uint32_t actual,
    uint32_t expected)
{
    if (actual == expected) return;
    fprintf(stderr, "%s: index %u got %08X, expected %08X\n",
        stage, index, actual, expected);
    exit(1);
}

void
scu_dma_config_buffer(scu_dma_handle_t *handle,
    const scu_dma_level_cfg_t *cfg)
{
    if ((handle == NULL) || (cfg == NULL)) fail_text("null DMA config");
    if (cfg->mode != SCU_DMA_MODE_INDIRECT)
        fail_text("DMA mode is not indirect");
    if (cfg->space != SCU_DMA_SPACE_BUS_B)
        fail_text("DMA destination is not B-bus");
    if (cfg->stride != SCU_DMA_STRIDE_2_BYTES)
        fail_text("DMA destination stride is not two bytes");
    if (cfg->update != SCU_DMA_UPDATE_NONE)
        fail_text("unexpected DMA address update mode");
    if ((uintptr_t)(const void *)dma_table % 2048U != 0U)
        fail_text("DMA table is not 2048-byte aligned");
    if (sizeof(dma_table) != KNOVGB_HEIGHT * sizeof(scu_dma_xfer_t))
        fail_text("DMA table does not contain 144 descriptors");
    if (cfg->xfer.indirect != dma_table)
        fail_text("DMA config does not point to production table");
    *handle = (scu_dma_handle_t){ .cfg = *cfg };
}

void
scu_dma_config_set(scu_dma_level_t level, int start_factor,
    const scu_dma_handle_t *handle, void (*callback)(void *))
{
    if ((handle == NULL) || (callback != NULL))
        fail_text("unexpected DMA handle or callback");
    if (level != 2U) fail_text("unexpected SCU DMA level");
    if (start_factor != SCU_DMA_START_FACTOR_ENABLE)
        fail_text("DMA start factor is not immediate");
    if (pending_wait) fail_text("DMA reconfigured before previous wait");
    configured = handle->cfg;
    configured_level = level;
    configured_valid = true;
}

void
scu_dma_level_fast_start(scu_dma_level_t level)
{
    if (!configured_valid || (level != configured_level) || pending_wait)
        fail_text("DMA started without a valid configuration");
    if (active_source == NULL)
        fail_text("DMA started outside an expected presentation");
    if (test_cache_purge_count != 0U)
        fail_text("presentation unexpectedly purged the CPU cache");
    if (configured.xfer.indirect != dma_table)
        fail_text("DMA table changed before start");

    const uint32_t source_address =
        (uint32_t)(uintptr_t)(const void *)active_source;
    const uint32_t vram_address =
        (uint32_t)(uintptr_t)(const void *)bitmap_bytes();
    if ((source_address & SCU_DMA_INDIRECT_TABLE_END) != 0U)
        fail_text("host placed source in terminal-bit address range");

    rows_seen = 0U;
    for (uint32_t y = 0U; y < KNOVGB_HEIGHT; y++) {
        const scu_dma_xfer_t *const entry = &dma_table[y];
        const uint32_t expected_src = source_address + y * active_stride;
        const uint32_t expected_dst = vram_address +
            (TEST_BITMAP_Y + y) * TEST_BITMAP_WIDTH + TEST_BITMAP_X;
        const uint32_t end = (y == KNOVGB_HEIGHT - 1U) ?
            SCU_DMA_INDIRECT_TABLE_END : 0U;

        expect_u32("descriptor length", y, entry->len, TEST_ROW_BYTES);
        expect_u32("descriptor destination", y, entry->dst, expected_dst);
        expect_u32("descriptor source/END", y, entry->src,
            expected_src | end);
        memcpy(bitmap_bytes() +
            (TEST_BITMAP_Y + y) * TEST_BITMAP_WIDTH + TEST_BITMAP_X,
            active_source + y * active_stride, TEST_ROW_BYTES);
        rows_seen++;
    }

    configured_valid = false;
    pending_wait = true;
    starts++;
}

void
scu_dma_level_wait(scu_dma_level_t level)
{
    if (!pending_wait || (level != configured_level))
        fail_text("presentation did not wait for matching DMA transfer");
    pending_wait = false;
    waits++;
}

void
scu_dma_level_end_set(scu_dma_level_t level,
    void (*callback)(void *), void *work)
{
    if ((level != 2U) || (callback != NULL) || (work != NULL))
        fail_text("unexpected DMA completion callback");
}

static void
check_vram(const char *stage)
{
    const uint8_t *const actual = bitmap_bytes();
    for (uint32_t i = 0U; i < sizeof(expected_vram); i++) {
        if (actual[i] != expected_vram[i])
            expect_u32(stage, i, actual[i], expected_vram[i]);
    }
}

static void
check_palette(void)
{
    static const uint16_t expected[9] = {
        0x8000U, 0xA94AU, 0xD6B5U, 0xFFFFU,
        0x801FU, 0xFC00U, 0x83FFU, 0xFC1FU, 0xFFE0U
    };
    for (uint32_t i = 0U; i < 9U; i++)
        expect_u32("CRAM palette", i, test_vdp2_cram[i], expected[i]);
}

static void
reference_present(const uint8_t *source, uint32_t stride)
{
    for (uint32_t y = 0U; y < KNOVGB_HEIGHT; y++) {
        for (uint32_t x = 0U; x < KNOVGB_WIDTH; x++) {
            expected_vram[(TEST_BITMAP_Y + y) * TEST_BITMAP_WIDTH +
                TEST_BITMAP_X + x] = source[y * stride + x];
        }
    }
}

static void
fill_source(uint8_t *source, uint32_t stride, uint32_t seed)
{
    for (uint32_t y = 0U; y < KNOVGB_HEIGHT; y++) {
        for (uint32_t x = 0U; x < stride; x++) {
            source[y * stride + x] =
                (uint8_t)((seed + y * 3U + x * 5U + x * y) & 3U);
        }
    }
}

static void
present_and_check(const char *stage, const uint8_t *source,
    uint32_t stride)
{
    const size_t source_bytes = (size_t)KNOVGB_HEIGHT * stride;
    memcpy(source_snapshot, source, source_bytes);
    active_source = source;
    active_stride = stride;
    const uint32_t starts_before = starts;
    const uint32_t waits_before = waits;

    saturn_platform_present_indexed8_strided(source, stride);

    if ((starts != starts_before + 1U) || (waits != waits_before + 1U))
        fail_text("presentation did not launch and wait for exactly one DMA");
    if ((rows_seen != KNOVGB_HEIGHT) || pending_wait)
        fail_text("DMA did not finish all 144 rows before returning");
    if (memcmp(source_snapshot, source, source_bytes) != 0)
        fail_text("presentation modified the emulator framebuffer");
    reference_present(source, stride);
    check_vram(stage);
    active_source = NULL;
}

static void
fallback_and_check(const uint8_t *source, uint32_t stride)
{
    const size_t source_bytes = (size_t)KNOVGB_HEIGHT * stride;
    memcpy(source_snapshot, source, source_bytes);
    const uint32_t starts_before = starts;
    const uint32_t waits_before = waits;

    saturn_platform_present_indexed8_strided(source, stride);

    if ((starts != starts_before) || (waits != waits_before))
        fail_text("unaligned source unexpectedly launched DMA");
    if (memcmp(source_snapshot, source, source_bytes) != 0)
        fail_text("fallback modified the emulator framebuffer");
    reference_present(source, stride);
    check_vram("unaligned CPU fallback");
}

static void
check_hud_region(void)
{
    const uint8_t *const actual = bitmap_bytes();
    bool white_found = false;
#if defined(KNOVGBC_VDP2_ZOOM)
    for (uint32_t y = HUD_Y; y < HUD_Y + HUD_HEIGHT; y++) {
        for (uint32_t x = HUD_X; x < HUD_X + HUD_WIDTH; x++) {
            const uint8_t color = actual[y * TEST_BITMAP_WIDTH + x];
            if (color == 3U) white_found = true;
            else if (color != 0U)
                fail_text("zoom HUD wrote an unexpected palette index");
        }
    }
    if (!white_found) fail_text("zoom HUD text is invisible");
#if defined(KNOVGBC_NO_FRAMESKIP)
    for (uint32_t line = 0U; line < 6U; line++) {
#else
    for (uint32_t line = 0U; line < 7U; line++) {
#endif
        expect_u32("stacked HUD field", line,
            actual[(HUD_Y + line * HUD_LINE) * TEST_BITMAP_WIDTH + HUD_X],
            3U);
    }
#else
    for (uint32_t y = 18U; y < 54U; y++) {
        for (uint32_t x = 104U; x < 248U; x++) {
            const uint8_t color = actual[y * TEST_BITMAP_WIDTH + x];
            if (color == 3U) white_found = true;
            else if (color != 0U)
                fail_text("HUD wrote a non-background, non-white index");
        }
    }
    if (!white_found) fail_text("HUD text is invisible");
    expect_u32("HUD top-left T", 0,
        actual[18U * TEST_BITMAP_WIDTH + 104U], 3U);
#endif
}

#if defined(KNOVGBC_VDP2_ZOOM)
static void
check_zoom_text(const char *const fields[], uint32_t field_count)
{
    const uint8_t *const actual = bitmap_bytes();
    for (uint32_t line = 0U; line < field_count; line++) {
        for (uint32_t character = 0U; character < 5U; character++) {
            const uint8_t *const rows = glyph_rows(fields[line][character]);
            for (uint32_t row = 0U; row < 5U; row++) {
                for (uint32_t column = 0U; column < 4U; column++) {
                    uint32_t expected = 0U;
                    if ((column < 3U) &&
                        ((rows[row] & (1U << (2U - column))) != 0U)) {
                        expected = 3U;
                    }
                    const uint32_t index =
                        (HUD_Y + line * HUD_LINE + row) *
                            TEST_BITMAP_WIDTH + HUD_X +
                            character * HUD_ADVANCE + column;
                    expect_u32("zoom HUD glyph", index, actual[index],
                        expected);
                }
            }
        }
    }
    for (uint32_t line = field_count; line < 7U; line++) {
        for (uint32_t row = 0U; row < 5U; row++) {
            for (uint32_t column = 0U; column < 5U * HUD_ADVANCE; column++) {
                const uint32_t index =
                    (HUD_Y + line * HUD_LINE + row) * TEST_BITMAP_WIDTH +
                    HUD_X + column;
                expect_u32("unused zoom HUD row", index, actual[index], 0U);
            }
        }
    }
}
#endif

int
main(void)
{
    fill_source(padded, TEST_PADDED_STRIDE, 1U);
    fill_source(tight, KNOVGB_WIDTH, 2U);
    fill_source(unaligned_storage + 1U, TEST_ODD_STRIDE, 3U);

    memset(test_vdp2_vram, 0xA5, sizeof(test_vdp2_vram));
    memset(test_vdp2_cram, 0x5A, sizeof(test_vdp2_cram));
    memset(expected_vram, 0xA5, sizeof(expected_vram));
    saturn_platform_video_init();
    memset(expected_vram, 0U, TEST_BITMAP_BYTES);
    check_vram("initial indexed clear and guard bank");
    check_palette();
    expect_u32("bitmap format", 0, test_bitmap_format.bitmap_size,
        VDP2_SCRN_BITMAP_SIZE_512X256);
    expect_u32("bitmap color mode", 0, test_bitmap_format.ccc,
        VDP2_SCRN_CCC_PALETTE_256);
    expect_u32("palette bank", 0, (uint32_t)test_bitmap_format.palette_base,
        0U);
    if (test_bitmap_format.bitmap_base !=
        (uintptr_t)(void *)test_vdp2_vram)
        fail_text("VDP2 bitmap base moved");
    expect_u32("VRAM bank A0", 0, test_vram_usage.a0,
        VDP2_VRAM_USAGE_TYPE_CPD_BPD);
    expect_u32("VRAM bank A1", 0, test_vram_usage.a1,
        VDP2_VRAM_USAGE_TYPE_NONE);
    expect_u32("VRAM bank B0", 0, test_vram_usage.b0,
        VDP2_VRAM_USAGE_TYPE_NONE);
    expect_u32("VRAM bank B1", 0, test_vram_usage.b1,
        VDP2_VRAM_USAGE_TYPE_NONE);
    const uint32_t a0_cycles[8] = {
        test_vram_cycles.pt[0].t0, test_vram_cycles.pt[0].t1,
        test_vram_cycles.pt[0].t2, test_vram_cycles.pt[0].t3,
        test_vram_cycles.pt[0].t4, test_vram_cycles.pt[0].t5,
        test_vram_cycles.pt[0].t6, test_vram_cycles.pt[0].t7
    };
    for (uint32_t slot = 0U; slot < 8U; slot++) {
        expect_u32("A0 fetch/write cycles", slot,
            a0_cycles[slot],
            slot < 4U ? VDP2_VRAM_CYCP_CHPNDR_NBG0 :
                VDP2_VRAM_CYCP_CPU_RW);
    }
    expect_u32("unused A1 cycles", 0, test_vram_cycles.pt[1].raw,
        0xFFFFFFFFU);
    expect_u32("opaque NBG0 display", 0, test_display_flags,
        VDP2_SCRN_DISP_NBG0);
#if defined(KNOVGBC_VDP2_ZOOM)
    expect_u32("zoom scroll x", 0, test_scroll_x, FIX16(153.0));
    expect_u32("zoom scroll y", 0, test_scroll_y, FIX16(56.0));
    expect_u32("horizontal increment", 0, test_zoom_x,
        FIX16(165.0 / 256.0));
    expect_u32("vertical increment", 0, test_zoom_y,
        FIX16(165.0 / 256.0));

    uint32_t first_game_column = 320U;
    uint32_t last_game_column = 320U;
    for (uint32_t x = 0U; x < 320U; x++) {
        const uint32_t source_x = 153U + x * 165U / 256U;
        if (source_x >= TEST_BITMAP_X &&
            source_x < TEST_BITMAP_X + KNOVGB_WIDTH) {
            if (first_game_column == 320U) first_game_column = x;
            last_game_column = x;
        }
    }
    expect_u32("first game TV column", 0, first_game_column, 36U);
    expect_u32("last game TV column", 0, last_game_column, 283U);
    expect_u32("first game TV row", 0, 56U + 0U * 165U / 256U,
        TEST_BITMAP_Y);
    expect_u32("last game TV row", 0, 56U + 223U * 165U / 256U,
        TEST_BITMAP_Y + KNOVGB_HEIGHT - 1U);
#else
    expect_u32("scroll x", 0, test_scroll_x, 96U);
    expect_u32("scroll y", 0, test_scroll_y, 16U);
#endif

#if defined(KNOVGBC_VDP2_ZOOM)
#if defined(KNOVGBC_NO_FRAMESKIP)
    saturn_platform_hud_draw("L59.7", "T0017 G0016",
        "C0000 W0000 P0001");
#else
    saturn_platform_hud_draw("L59.7 V29.9", "T0017 G0016",
        "C0000 W0000 P0001");
#endif
#else
    saturn_platform_hud_draw("T21", "G16", "P06");
#endif
    check_hud_region();
#if defined(KNOVGBC_VDP2_ZOOM)
#if defined(KNOVGBC_HUD_3_DIGITS)
#if defined(KNOVGBC_NO_FRAMESKIP)
    const char *const compact_fields[6] = {
        "L59.7", "T017", "G016", "C000", "W000", "P001"
    };
    check_zoom_text(compact_fields, 6U);
    saturn_platform_hud_draw("L59.7", "T1000 G0016",
        "C0000 W0000 P0001");
    const char *const saturated_fields[6] = {
        "L59.7", "T999", "G016", "C000", "W000", "P001"
    };
    check_zoom_text(saturated_fields, 6U);
#else
    const char *const compact_fields[7] = {
        "L59.7", "V29.9", "T017", "G016", "C000", "W000", "P001"
    };
    check_zoom_text(compact_fields, 7U);
    saturn_platform_hud_draw("L59.7 V29.9", "T1000 G0016",
        "C0000 W0000 P0001");
    const char *const saturated_fields[7] = {
        "L59.7", "V29.9", "T999", "G016", "C000", "W000", "P001"
    };
    check_zoom_text(saturated_fields, 7U);
#endif
#else
    const char *const zoom_fields[7] = {
        "L59.7", "V29.9", "T0017", "G0016", "C0000", "W0000",
        "P0001"
    };
    check_zoom_text(zoom_fields, 7U);
#endif
#endif
    /* The HUD must stay within its reserved rectangle, away from the image. */
    for (uint32_t y = 0U; y < TEST_BITMAP_HEIGHT; y++) {
        for (uint32_t x = 0U; x < TEST_BITMAP_WIDTH; x++) {
            const uint32_t index = y * TEST_BITMAP_WIDTH + x;
            if ((y >= HUD_Y) && (y < HUD_Y + HUD_HEIGHT) &&
                (x >= HUD_X) && (x < HUD_X + HUD_WIDTH)) continue;
            expect_u32("HUD border", index, bitmap_bytes()[index],
                expected_vram[index]);
        }
    }
    memcpy(expected_vram, bitmap_bytes(), TEST_BITMAP_BYTES);
    for (uint32_t y = 0U; y < KNOVGB_HEIGHT; y++) {
        const uint32_t row = (TEST_BITMAP_Y + y) * TEST_BITMAP_WIDTH;
        expected_vram[row + TEST_BITMAP_X - 1U] =
            bitmap_bytes()[row + TEST_BITMAP_X - 1U] =
                (uint8_t)(0x40U | (y & 0x3FU));
        expected_vram[row + TEST_BITMAP_X + KNOVGB_WIDTH] =
            bitmap_bytes()[row + TEST_BITMAP_X + KNOVGB_WIDTH] =
                (uint8_t)(0x80U | (y & 0x3FU));
    }

    present_and_check("padded direct source", padded, TEST_PADDED_STRIDE);
    padded[10U * TEST_PADDED_STRIDE + KNOVGB_WIDTH] ^= 3U;
    present_and_check("padding-only change", padded, TEST_PADDED_STRIDE);
    padded[20U * TEST_PADDED_STRIDE + 20U] = 0U;
    padded[21U * TEST_PADDED_STRIDE + 21U] = 3U;
    present_and_check("palette endpoints", padded, TEST_PADDED_STRIDE);
    present_and_check("tight direct source", tight, KNOVGB_WIDTH);

    if (((uintptr_t)(const void *)(unaligned_storage + 1U) & 3U) != 1U)
        fail_text("unaligned fixture is not byte-unaligned");
    fallback_and_check(unaligned_storage + 1U, TEST_ODD_STRIDE);

    const uint32_t before_invalid = starts;
    saturn_platform_present_indexed8_strided(NULL, TEST_PADDED_STRIDE);
    saturn_platform_present_indexed8_strided(padded, KNOVGB_WIDTH - 1U);
    expect_u32("invalid calls started DMA", 0, starts, before_invalid);
    check_vram("invalid calls");
    expect_u32("DMA launch/wait count", 0, waits, starts);
    check_palette();

    puts("vdp2_dma_indexed8_test: PASS");
    return 0;
}
