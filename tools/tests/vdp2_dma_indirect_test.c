/* Compile the production uploader in this TU so its private DMA buffers can
 * be inspected without truncating host pointers to Saturn's 32-bit address. */
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
    TEST_PADDED_STRIDE = 176,
    TEST_UNALIGNED_STRIDE = 161,
    TEST_ROW_BYTES = KNOVGB_WIDTH * sizeof(uint16_t)
};

_Alignas(4) uint16_t test_vdp2_vram[TEST_BITMAP_WIDTH * TEST_BITMAP_HEIGHT];
vdp2_scrn_bitmap_format_t test_bitmap_format;
int test_scroll_x;
int test_scroll_y;
uint32_t test_cache_purge_count;

static uint16_t expected_vram[TEST_BITMAP_WIDTH * TEST_BITMAP_HEIGHT];
static uint16_t padded[KNOVGB_HEIGHT * TEST_PADDED_STRIDE];
static uint16_t tight[KNOVGB_HEIGHT * KNOVGB_WIDTH];
static _Alignas(4) uint16_t unaligned_storage[
    1 + KNOVGB_HEIGHT * TEST_UNALIGNED_STRIDE];

#include "../../src/saturn/platform_video.c"

static scu_dma_level_cfg_t configured;
static scu_dma_level_t configured_level;
static bool configured_valid;
static bool pending_wait;
static const uint16_t *active_source;
static uint32_t active_stride;
static bool seen_rows[KNOVGB_HEIGHT];
static uint32_t rows_seen;
static uint32_t starts;
static uint32_t waits;

static void
fail_text(const char *message)
{
    fprintf(stderr, "vdp2_dma_indirect_test: %s\n", message);
    exit(1);
}

static void
fail_value(const char *stage, uint32_t index, uint32_t actual,
    uint32_t expected)
{
    fprintf(stderr, "%s: index %u got %08X, expected %08X\n",
        stage, index, actual, expected);
    exit(1);
}

static void
expect_u32(const char *stage, uint32_t index, uint32_t actual,
    uint32_t expected)
{
    if (actual != expected) fail_value(stage, index, actual, expected);
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
        fail_text("DMA does not advance destination by two bytes");
    if (cfg->update != SCU_DMA_UPDATE_NONE)
        fail_text("unexpected DMA address update mode");
    if ((uintptr_t)(const void *)dma_table % 2048U != 0U)
        fail_text("144-entry DMA table is not 2048-byte aligned");
    if (sizeof(dma_table) != KNOVGB_HEIGHT * sizeof(scu_dma_xfer_t))
        fail_text("DMA table does not contain exactly 144 descriptors");
    if (cfg->xfer.indirect != dma_table)
        fail_text("DMA config does not point to the production table");
    *handle = (scu_dma_handle_t){ .cfg = *cfg };
}

void
scu_dma_config_set(scu_dma_level_t level, int start_factor,
    const scu_dma_handle_t *handle, void (*callback)(void *))
{
    if ((handle == NULL) || (callback != NULL))
        fail_text("unexpected DMA handle or callback");
    if (level > 2U) fail_text("invalid SCU DMA level");
    if (start_factor != SCU_DMA_START_FACTOR_ENABLE)
        fail_text("DMA start factor is not immediate");
    if (pending_wait) fail_text("DMA reconfigured before completion wait");
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

    const uint32_t staging_address =
        (uint32_t)(uintptr_t)(const void *)dma_staging;
    const uint32_t vram_address =
        (uint32_t)(uintptr_t)(const void *)test_vdp2_vram;
    const scu_dma_xfer_t *const table = configured.xfer.indirect;

    if ((staging_address & SCU_DMA_INDIRECT_TABLE_END) != 0U)
        fail_text("host placed staging in the DMA terminal-bit address range");
    if (table != dma_table) fail_text("DMA table changed before start");
    if ((uintptr_t)(const void *)dma_staging % 4U != 0U)
        fail_text("DMA staging is not long-word aligned");
    if (sizeof(dma_staging) != KNOVGB_HEIGHT * TEST_ROW_BYTES)
        fail_text("DMA staging is not a contiguous 160x144 RGB1555 frame");

    for (uint32_t y = 0; y < KNOVGB_HEIGHT; y++) {
        const scu_dma_xfer_t *const entry = &table[y];
        const uint32_t expected_src = staging_address + y * TEST_ROW_BYTES;
        const uint32_t expected_dst = vram_address +
            ((TEST_BITMAP_Y + y) * TEST_BITMAP_WIDTH + TEST_BITMAP_X) *
            sizeof(uint16_t);
        const uint32_t end = (y == KNOVGB_HEIGHT - 1U) ?
            SCU_DMA_INDIRECT_TABLE_END : 0U;

        expect_u32("descriptor length", y, entry->len, TEST_ROW_BYTES);
        expect_u32("descriptor destination", y, entry->dst, expected_dst);
        expect_u32("descriptor source/END", y, entry->src,
            expected_src | end);
        if (seen_rows[y]) fail_text("DMA copied a row twice");
        seen_rows[y] = true;
        rows_seen++;

        const uint16_t *const stage =
            (const uint16_t *)(const void *)((const uint8_t *)(const void *)
                dma_staging + y * TEST_ROW_BYTES);
        for (uint32_t x = 0; x < KNOVGB_WIDTH; x++) {
            const uint16_t expected =
                active_source[y * active_stride + x] | 0x8000U;
            if (stage[x] != expected)
                fail_value("staging RGB1555", y * KNOVGB_WIDTH + x,
                    stage[x], expected);
        }
        memcpy((void *)&test_vdp2_vram[
            (TEST_BITMAP_Y + y) * TEST_BITMAP_WIDTH + TEST_BITMAP_X],
            stage, TEST_ROW_BYTES);
    }
    configured_valid = false;
    pending_wait = true;
    starts++;
}

void
scu_dma_level_wait(scu_dma_level_t level)
{
    if (pending_wait) {
        if (level != configured_level) fail_text("waited on the wrong DMA level");
        pending_wait = false;
        waits++;
    }
}

void
scu_dma_level_end_set(scu_dma_level_t level,
    void (*callback)(void *), void *work)
{
    if ((level > 2U) || (callback != NULL) || (work != NULL))
        fail_text("unexpected DMA completion callback");
}

static void
check_vram(const char *stage)
{
    for (uint32_t i = 0; i < TEST_BITMAP_WIDTH * TEST_BITMAP_HEIGHT; i++) {
        if (test_vdp2_vram[i] != expected_vram[i])
            fail_value(stage, i, test_vdp2_vram[i], expected_vram[i]);
    }
}

static void
reference_present(const uint16_t *source, uint32_t stride)
{
    for (uint32_t y = 0; y < KNOVGB_HEIGHT; y++) {
        for (uint32_t x = 0; x < KNOVGB_WIDTH; x++) {
            expected_vram[(TEST_BITMAP_Y + y) * TEST_BITMAP_WIDTH +
                TEST_BITMAP_X + x] = source[y * stride + x] | 0x8000U;
        }
    }
}

static void
fill_source(uint16_t *source, uint32_t stride, uint32_t seed)
{
    for (uint32_t y = 0; y < KNOVGB_HEIGHT; y++) {
        for (uint32_t x = 0; x < stride; x++) {
            source[y * stride + x] = (uint16_t)(
                (seed * 53U + y * 97U + x * 41U + (x * y * 13U)) & 0xFFFFU);
        }
    }
}

static void
present_and_check(const char *stage, const uint16_t *source,
    uint32_t stride, bool wrapper)
{
    active_source = source;
    active_stride = stride;
    rows_seen = 0;
    memset(seen_rows, 0, sizeof(seen_rows));
    const uint32_t starts_before = starts;

    if (wrapper) saturn_platform_present(source);
    else saturn_platform_present_strided(source, stride);

    if (starts != starts_before + 1U)
        fail_text("presentation did not use exactly one indirect DMA launch");
    if ((rows_seen != KNOVGB_HEIGHT) || pending_wait)
        fail_text("DMA did not finish all 144 rows before returning");
    reference_present(source, stride);
    check_vram(stage);
    active_source = NULL;
}

int
main(void)
{
    fill_source(padded, TEST_PADDED_STRIDE, 1U);
    fill_source(tight, KNOVGB_WIDTH, 2U);
    fill_source(unaligned_storage + 1U, TEST_UNALIGNED_STRIDE, 3U);

    saturn_platform_video_init();
    for (uint32_t i = 0; i < TEST_BITMAP_WIDTH * TEST_BITMAP_HEIGHT; i++)
        expected_vram[i] = 0x8000U;
    check_vram("initial clear");
    expect_u32("bitmap format", 0, test_bitmap_format.bitmap_size,
        VDP2_SCRN_BITMAP_SIZE_512X256);
    expect_u32("bitmap color", 0, test_bitmap_format.ccc,
        VDP2_SCRN_CCC_RGB_32768);
    if (test_bitmap_format.bitmap_base !=
        (uintptr_t)(void *)test_vdp2_vram)
        fail_text("VDP2 bitmap base moved");
    expect_u32("scroll x", 0, test_scroll_x, 96U);
    expect_u32("scroll y", 0, test_scroll_y, 16U);

    saturn_platform_hud_draw("T21", "G17", "P06");
    memcpy(expected_vram, test_vdp2_vram, sizeof(expected_vram));
    /* Border and HUD sentinels expose DMA overruns as full-VRAM mismatches. */
    for (uint32_t y = 0; y < KNOVGB_HEIGHT; y++) {
        const uint32_t row = (TEST_BITMAP_Y + y) * TEST_BITMAP_WIDTH;
        expected_vram[row + TEST_BITMAP_X - 1U] =
            test_vdp2_vram[row + TEST_BITMAP_X - 1U] =
            (uint16_t)(0x9000U | y);
        expected_vram[row + TEST_BITMAP_X + KNOVGB_WIDTH] =
            test_vdp2_vram[row + TEST_BITMAP_X + KNOVGB_WIDTH] =
            (uint16_t)(0xA000U | y);
    }
    present_and_check("padded source", padded, TEST_PADDED_STRIDE, false);

    padded[10U * TEST_PADDED_STRIDE + KNOVGB_WIDTH] ^= 0x3A71U;
    present_and_check("padding-only source change", padded,
        TEST_PADDED_STRIDE, false);
    padded[20U * TEST_PADDED_STRIDE + 20U] ^= 0x8000U;
    present_and_check("source high bit", padded, TEST_PADDED_STRIDE, false);

    present_and_check("tight wrapper", tight, KNOVGB_WIDTH, true);
    if (((uintptr_t)(unaligned_storage + 1U) & 3U) != 2U)
        fail_text("unaligned fixture is not two-byte aligned");
    present_and_check("unaligned source", unaligned_storage + 1U,
        TEST_UNALIGNED_STRIDE, false);

    const uint32_t before_invalid = starts;
    saturn_platform_present_strided(NULL, TEST_PADDED_STRIDE);
    saturn_platform_present_strided(padded, KNOVGB_WIDTH - 1U);
    expect_u32("invalid calls started DMA", 0, starts, before_invalid);
    check_vram("invalid calls");
    expect_u32("DMA launch/wait count", 0, waits, starts);

    puts("vdp2_dma_indirect_test: PASS");
    return 0;
}
