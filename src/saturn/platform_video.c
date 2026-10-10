#include "platform.h"
#if defined(KNOVGBC_VDP12_NATIVE)
#include "platform_video_vdp12.h"
#endif

#include <yaul.h>

#include <stdint.h>

/* RGB1555 occupies A0+A1; the experimental indexed bitmap fits in A0. */
#define BITMAP_WIDTH  512U
#define BITMAP_HEIGHT 256U
#define BITMAP_X      ((BITMAP_WIDTH - KNOVGB_WIDTH) / 2U)
#define BITMAP_Y      ((BITMAP_HEIGHT - KNOVGB_HEIGHT) / 2U)

/* The zoom builds enlarge the existing 160x144 bitmap with NBG0 coordinate
 * increments. At 165/256 source pixels per TV dot, all 144 source rows span
 * the 224-line display; the image occupies TV columns 36..283. The 23 source
 * columns to its left are reserved for a narrow performance readout. */
#if defined(KNOVGBC_VDP2_ZOOM)
#define ZOOM_SCROLL_X 153U
#define ZOOM_SCROLL_Y BITMAP_Y
#define ZOOM_COORD_STEP FIX16(165.0 / 256.0)
#define HUD_X      156U
#define HUD_Y       60U
#define HUD_WIDTH   20U
#define HUD_HEIGHT  56U
#define HUD_SCALE    1U
#define HUD_ADVANCE  4U
#define HUD_LINE     8U
#define HUD_TEXT_CHARS 5U
_Static_assert(HUD_X + HUD_WIDTH <= BITMAP_X,
    "Zoom HUD must not overlap the Game Boy image");
_Static_assert(ZOOM_SCROLL_Y + (223U * 165U / 256U) ==
    BITMAP_Y + KNOVGB_HEIGHT - 1U,
    "The bottom TV row must display the last Game Boy row");
#else
/* The VDP2 scroll exposes bitmap (96,16) as screen (0,0). The Game Boy image
 * begins at bitmap y=56, leaving forty visible lines for diagnostic text. */
#define HUD_X      104U
#define HUD_Y       18U
#define HUD_WIDTH  144U
#define HUD_HEIGHT  36U
#define HUD_SCALE    2U
#define HUD_ADVANCE  8U
#define HUD_LINE    12U
#define HUD_TEXT_CHARS 17U
#endif

#if defined(KNOVGBC_VDP2_INDEXED8)
static volatile uint8_t *const bitmap =
    (volatile uint8_t *)VDP2_VRAM_ADDR(VDP2_VRAM_BANK_A0, 0);
static volatile uint16_t *const palette_cram =
    (volatile uint16_t *)VDP2_CRAM_ADDR(0);
_Static_assert((VDP2_SCRN_DISP_NBG0 & 0x0100U) != 0U,
    "Indexed pixel zero requires NBG0 transparency disabled");
#define HUD_FOREGROUND 3U
#define HUD_BACKGROUND 0U
#else
static volatile uint16_t *const bitmap =
    (volatile uint16_t *)VDP2_VRAM_ADDR(VDP2_VRAM_BANK_A0, 0);
#define HUD_FOREGROUND 0xFFFFU
#define HUD_BACKGROUND 0x8000U
#endif

/* GCC may otherwise assume that a uint32_t lvalue can never refer to the
 * uint16_t framebuffer.  This explicit alias type keeps the aligned pair
 * load/store defined even when Yaul's specs enable strict aliasing. */
typedef uint32_t rgb1555_pair_t __attribute__((__may_alias__));

#if defined(KNOVGBC_VDP2_DMA) && defined(KNOVGBC_VDP2_DMA_DIRECT)
#error "Select only one VDP2 DMA presentation experiment"
#endif

#if defined(KNOVGBC_VDP2_DIRTY_LINES) && \
    (defined(KNOVGBC_VDP2_DMA) || defined(KNOVGBC_VDP2_DMA_DIRECT))
#error "The DMA and dirty-line presentation experiments are separate builds"
#endif

#if defined(KNOVGBC_VDP2_DMA) || defined(KNOVGBC_VDP2_DMA_DIRECT)
/* Keep the existing NBG0 bitmap and HUD layout. The SCU copies one compact
 * source row to each destination row. The
 * descriptor table must be aligned to the next power of two above its total
 * size (144 * 12 = 1728 bytes). */
#if defined(KNOVGBC_VDP2_INDEXED8)
#define DMA_ROW_BYTES (KNOVGB_WIDTH * sizeof(uint8_t))
#else
#define DMA_ROW_BYTES (KNOVGB_WIDTH * sizeof(uint16_t))
#endif
#define DMA_LEVEL     2U
#if defined(KNOVGBC_VDP2_DMA)
static uint16_t dma_staging[KNOVGB_HEIGHT][KNOVGB_WIDTH]
    __attribute__((aligned(4)));
#else
/* In the direct experiment these point into PicoGB's 176-pixel-stride frame.
 * The descriptors need rewriting only if a caller changes source or stride. */
#if defined(KNOVGBC_VDP2_INDEXED8)
static const uint8_t *dma_source;
#else
static const uint16_t *dma_source;
#endif
static uint32_t dma_source_stride;
#endif
static scu_dma_xfer_t dma_table[KNOVGB_HEIGHT]
    __attribute__((aligned(2048)));
static scu_dma_handle_t dma_handle;

_Static_assert(sizeof(scu_dma_xfer_t) == 12U,
    "Unexpected SCU DMA descriptor size");
_Static_assert(sizeof(dma_table) <= 2048U,
    "The SCU DMA table needs a larger alignment");

static void
dma_present_init(void)
{
    for (uint32_t y = 0; y < KNOVGB_HEIGHT; y++) {
        dma_table[y].len = DMA_ROW_BYTES;
        dma_table[y].dst = (uint32_t)(uintptr_t)(bitmap +
            ((BITMAP_Y + y) * BITMAP_WIDTH) + BITMAP_X);
#if defined(KNOVGBC_VDP2_DMA)
        dma_table[y].src = (uint32_t)(uintptr_t)dma_staging[y];
#else
        dma_table[y].src = 0U;
#endif
    }
#if defined(KNOVGBC_VDP2_DMA)
    dma_table[KNOVGB_HEIGHT - 1U].src |= SCU_DMA_INDIRECT_TABLE_END;
#else
    dma_source = NULL;
    dma_source_stride = 0U;
#endif

    const scu_dma_level_cfg_t cfg = {
        .space = SCU_DMA_SPACE_BUS_B,
        .mode = SCU_DMA_MODE_INDIRECT,
        .stride = SCU_DMA_STRIDE_2_BYTES,
        .update = SCU_DMA_UPDATE_NONE,
        .xfer.indirect = dma_table
    };
    scu_dma_config_buffer(&dma_handle, &cfg);
}

#if defined(KNOVGBC_VDP2_DMA_DIRECT)
/* Direct DMA preserves PicoGB's RGB1555 pixels, including a clear bit 15.
 * N0TPON must stay set so VDP2 does not treat those pixels as transparent. */
_Static_assert((VDP2_SCRN_DISP_NBG0 & 0x0100U) != 0U,
    "Direct RGB1555 DMA requires NBG0 transparency disabled");

static void
#if defined(KNOVGBC_VDP2_INDEXED8)
dma_present_direct_source_set(const uint8_t *pixels, uint32_t stride_pixels)
#else
dma_present_direct_source_set(const uint16_t *pixels, uint32_t stride_pixels)
#endif
{
    if (pixels == dma_source && stride_pixels == dma_source_stride) return;

    for (uint32_t y = 0; y < KNOVGB_HEIGHT; y++) {
        dma_table[y].src = (uint32_t)(uintptr_t)(pixels + y * stride_pixels);
    }
    dma_table[KNOVGB_HEIGHT - 1U].src |= SCU_DMA_INDIRECT_TABLE_END;
    dma_source = pixels;
    dma_source_stride = stride_pixels;
}
#endif
#endif

#if defined(KNOVGBC_VDP2_DIRTY_LINES)
/* This is the last image actually sent to VDP2, not the PPU's mutable frame.
 * A skipped visual frame must not replace it.  Keep the copy in Work RAM;
 * reading VDP2 VRAM to discover changed rows would cost the bus we save. */
static uint16_t last_presented[KNOVGB_HEIGHT][KNOVGB_WIDTH]
    __attribute__((aligned(4)));
static bool last_presented_valid;

static inline bool
presented_row_matches(const uint16_t *source, const uint16_t *previous)
{
    if (((uintptr_t)source & (sizeof(uint32_t) - 1U)) == 0U) {
        const rgb1555_pair_t *const source_pairs =
            (const rgb1555_pair_t *)(const void *)source;
        const rgb1555_pair_t *const previous_pairs =
            (const rgb1555_pair_t *)(const void *)previous;

        for (uint32_t pair = 0; pair < (KNOVGB_WIDTH / 2U); pair++) {
            if (source_pairs[pair] != previous_pairs[pair]) return false;
        }
    }
    else {
        for (uint32_t x = 0; x < KNOVGB_WIDTH; x++) {
            if (source[x] != previous[x]) return false;
        }
    }
    return true;
}
#endif

#if !defined(KNOVGBC_VDP2_INDEXED8)
_Static_assert((KNOVGB_WIDTH & 1U) == 0U,
    "The RGB1555 pair uploader requires an even source width");
_Static_assert(sizeof(rgb1555_pair_t) == sizeof(uint32_t),
    "Unexpected RGB1555 pair size");

static inline uint32_t
rgb1555_pair_pack(uint16_t first, uint16_t second)
{
    /* A long write must leave the first source pixel at the lower VDP2 VRAM
     * address. The SH-2 is big-endian, but keeping the little-endian branch
     * makes that byte-order assumption explicit and prevents this helper from
     * silently reversing pairs if it is ever compiled by a host-side test. */
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return ((uint32_t)first << 16) | (uint32_t)second | 0x80008000UL;
#elif defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    return (uint32_t)first | ((uint32_t)second << 16) | 0x80008000UL;
#else
#error "A known target byte order is required for RGB1555 pair uploads"
#endif
}
#endif

void
saturn_platform_video_init(void)
{
#if defined(KNOVGBC_VDP2_DIRTY_LINES)
    last_presented_valid = false;
#endif
    const vdp2_scrn_bitmap_format_t bitmap_format = {
        .scroll_screen = VDP2_SCRN_NBG0,
#if defined(KNOVGBC_VDP2_INDEXED8)
        .ccc = VDP2_SCRN_CCC_PALETTE_256,
#else
        .ccc = VDP2_SCRN_CCC_RGB_32768,
#endif
        .bitmap_size = VDP2_SCRN_BITMAP_SIZE_512X256,
        .palette_base = 0,
        .bitmap_base = VDP2_VRAM_ADDR(VDP2_VRAM_BANK_A0, 0)
    };
    const vdp2_vram_usage_t vram_usage = {
        .a0 = VDP2_VRAM_USAGE_TYPE_CPD_BPD,
#if defined(KNOVGBC_VDP2_INDEXED8)
        .a1 = VDP2_VRAM_USAGE_TYPE_NONE,
#else
        .a1 = VDP2_VRAM_USAGE_TYPE_CPD_BPD,
#endif
        .b0 = VDP2_VRAM_USAGE_TYPE_NONE,
        .b1 = VDP2_VRAM_USAGE_TYPE_NONE
    };
    const vdp2_vram_cycp_t vram_cycles = {
        .pt = {
            { .t0 = VDP2_VRAM_CYCP_CHPNDR_NBG0,
              .t1 = VDP2_VRAM_CYCP_CHPNDR_NBG0,
              .t2 = VDP2_VRAM_CYCP_CHPNDR_NBG0,
              .t3 = VDP2_VRAM_CYCP_CHPNDR_NBG0,
              .t4 = VDP2_VRAM_CYCP_CPU_RW,
              .t5 = VDP2_VRAM_CYCP_CPU_RW,
              .t6 = VDP2_VRAM_CYCP_CPU_RW,
              .t7 = VDP2_VRAM_CYCP_CPU_RW },
#if defined(KNOVGBC_VDP2_INDEXED8)
            { .raw = 0xFFFFFFFF },
#else
            { .t0 = VDP2_VRAM_CYCP_CHPNDR_NBG0,
              .t1 = VDP2_VRAM_CYCP_CHPNDR_NBG0,
              .t2 = VDP2_VRAM_CYCP_CHPNDR_NBG0,
              .t3 = VDP2_VRAM_CYCP_CHPNDR_NBG0,
              .t4 = VDP2_VRAM_CYCP_CPU_RW,
              .t5 = VDP2_VRAM_CYCP_CPU_RW,
              .t6 = VDP2_VRAM_CYCP_CPU_RW,
              .t7 = VDP2_VRAM_CYCP_CPU_RW },
#endif
            { .raw = 0xFFFFFFFF },
            { .raw = 0xFFFFFFFF }
        }
    };

    vdp2_tvmd_display_clear();
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE,
        VDP2_TVMD_HORZ_NORMAL_A, VDP2_TVMD_VERT_224);
    vdp2_vram_usage_set(&vram_usage);
    vdp2_vram_cycp_set(&vram_cycles);
    vdp2_scrn_bitmap_format_set(&bitmap_format);
    vdp2_scrn_priority_set(VDP2_SCRN_NBG0, 7);
#if defined(KNOVGBC_VDP2_ZOOM)
    vdp2_scrn_scroll_x_set(VDP2_SCRN_NBG0, FIX16(ZOOM_SCROLL_X));
    vdp2_scrn_scroll_y_set(VDP2_SCRN_NBG0, FIX16(ZOOM_SCROLL_Y));
    vdp2_scrn_reduction_x_set(VDP2_SCRN_NBG0, ZOOM_COORD_STEP);
    vdp2_scrn_reduction_y_set(VDP2_SCRN_NBG0, ZOOM_COORD_STEP);
#else
    vdp2_scrn_scroll_x_set(VDP2_SCRN_NBG0, FIX16(96.0));
    vdp2_scrn_scroll_y_set(VDP2_SCRN_NBG0, FIX16(16.0));
#endif
    vdp2_scrn_display_set(VDP2_SCRN_DISP_NBG0);
    vdp2_sync();
    vdp2_sync_wait();

#if defined(KNOVGBC_VDP2_INDEXED8)
    /* PicoGB has already applied BGP/OBP when choosing these final shades.
     * Diagnostic marker indices 4..8 retain their old colours. CRAM uses
     * 16-bit RGB555 entries even when the bitmap is 8-bit indexed. */
    static const uint16_t colours[] = {
        0x8000U, 0xA94AU, 0xD6B5U, 0xFFFFU,
        0x801FU, 0xFC00U, 0x83FFU, 0xFC1FU, 0xFFE0U
    };
    for (uint32_t index = 0; index < sizeof(colours) / sizeof(colours[0]);
         index++) {
        palette_cram[index] = colours[index];
    }
    volatile uint32_t *const bitmap_words =
        (volatile uint32_t *)(uintptr_t)bitmap;
    for (uint32_t index = 0; index <
         (BITMAP_WIDTH * BITMAP_HEIGHT / sizeof(uint32_t)); index++) {
        bitmap_words[index] = 0U;
    }
#else
    for (uint32_t index = 0; index < BITMAP_WIDTH * BITMAP_HEIGHT; index++) {
        bitmap[index] = 0x8000;
    }
#endif

#if defined(KNOVGBC_VDP2_DMA) || defined(KNOVGBC_VDP2_DMA_DIRECT)
    dma_present_init();
#endif

    vdp2_tvmd_display_set();
    vdp2_sync();
    vdp2_sync_wait();
}

#if !defined(KNOVGBC_VDP2_INDEXED8)
void
saturn_platform_present(const uint16_t *pixels)
{
    saturn_platform_present_strided(pixels, KNOVGB_WIDTH);
}

void
saturn_platform_present_strided(const uint16_t *pixels,
    uint32_t stride_pixels)
{
    if ((pixels == NULL) || (stride_pixels < KNOVGB_WIDTH)) {
        return;
    }

#if defined(KNOVGBC_VDP2_DMA_DIRECT)
    /* The actual PicoGB frame begins at x=8 and has a 176-pixel stride, so
     * every source row is long-word aligned. Keep the regular CPU uploader
     * as a defensive fallback for other callers with odd alignment/stride. */
    if (((uintptr_t)pixels & 3U) == 0U &&
        (stride_pixels & 1U) == 0U) {
        dma_present_direct_source_set(pixels, stride_pixels);
        __asm__ __volatile__("" ::: "memory");
        scu_dma_config_set(DMA_LEVEL, SCU_DMA_START_FACTOR_ENABLE,
            &dma_handle, NULL);
        scu_dma_level_end_set(DMA_LEVEL, NULL, NULL);
        scu_dma_level_fast_start(DMA_LEVEL);
        scu_dma_level_wait(DMA_LEVEL);
        return;
    }
#endif

#if defined(KNOVGBC_VDP2_DMA)
    for (uint32_t y = 0; y < KNOVGB_HEIGHT; y++) {
        const uint16_t *const source = pixels + (y * stride_pixels);
        rgb1555_pair_t *const staged_pairs =
            (rgb1555_pair_t *)(void *)dma_staging[y];

        if (((uintptr_t)source & (sizeof(uint32_t) - 1U)) == 0U) {
            const rgb1555_pair_t *const source_pairs =
                (const rgb1555_pair_t *)(const void *)source;
            for (uint32_t pair = 0; pair < (KNOVGB_WIDTH / 2U); pair++) {
                staged_pairs[pair] = source_pairs[pair] | 0x80008000UL;
            }
        }
        else {
            for (uint32_t pair = 0; pair < (KNOVGB_WIDTH / 2U); pair++) {
                const uint32_t x = pair * 2U;
                staged_pairs[pair] =
                    rgb1555_pair_pack(source[x], source[x + 1U]);
            }
        }
    }

    /* The SH-2 cache is write-through: staging stores already reach HWRAM,
     * which the SCU reads. A global purge here would evict the CPU's hot code
     * on every visual frame. Keep stores before DMA even with -O3/LTO, and
     * do not reuse staging until the synchronous transfer finishes. Level 2
     * is available because this program commits VDP2 registers only during
     * video_init, before frame presentation starts. */
    __asm__ __volatile__("" ::: "memory");
    scu_dma_config_set(DMA_LEVEL, SCU_DMA_START_FACTOR_ENABLE,
        &dma_handle, NULL);
    scu_dma_level_end_set(DMA_LEVEL, NULL, NULL);
    scu_dma_level_fast_start(DMA_LEVEL);
    scu_dma_level_wait(DMA_LEVEL);
    return;
#endif

    for (uint32_t y = 0; y < KNOVGB_HEIGHT; y++) {
        volatile uint16_t *const destination = bitmap +
            ((BITMAP_Y + y) * BITMAP_WIDTH) + BITMAP_X;
        const uint16_t *const source = pixels + (y * stride_pixels);
#if defined(KNOVGBC_VDP2_DIRTY_LINES)
        uint16_t *const previous = last_presented[y];

        if (last_presented_valid && presented_row_matches(source, previous)) {
            continue;
        }
#endif

        /* BITMAP_WIDTH is 512 pixels and BITMAP_X is even, so every normal
         * row destination is long-word aligned. Use one 32-bit VDP2 write for
         * each two source pixels: the source itself only needs its natural
         * uint16_t alignment because the pair is assembled from two word
         * reads. Retain the word path as a defensive fallback if a future
         * layout changes that destination alignment. */
        if (((uintptr_t)destination & (sizeof(uint32_t) - 1U)) == 0U) {
            volatile rgb1555_pair_t *const destination_pairs =
                (volatile rgb1555_pair_t *)(uintptr_t)destination;

            if (((uintptr_t)source & (sizeof(uint32_t) - 1U)) == 0U) {
                const rgb1555_pair_t *const source_pairs =
                    (const rgb1555_pair_t *)(const void *)source;

                for (uint32_t pair = 0;
                     pair < (KNOVGB_WIDTH / 2U); pair++) {
#if defined(KNOVGBC_VDP2_DIRTY_LINES)
                    const rgb1555_pair_t pixels_pair = source_pairs[pair];
                    destination_pairs[pair] = pixels_pair | 0x80008000UL;
                    ((rgb1555_pair_t *)(void *)previous)[pair] = pixels_pair;
#else
                    destination_pairs[pair] =
                        source_pairs[pair] | 0x80008000UL;
#endif
                }
            }
            else {
                for (uint32_t pair = 0;
                     pair < (KNOVGB_WIDTH / 2U); pair++) {
                    const uint32_t x = pair * 2U;
#if defined(KNOVGBC_VDP2_DIRTY_LINES)
                    const uint16_t first = source[x];
                    const uint16_t second = source[x + 1U];
                    destination_pairs[pair] = rgb1555_pair_pack(first, second);
                    previous[x] = first;
                    previous[x + 1U] = second;
#else
                    destination_pairs[pair] =
                        rgb1555_pair_pack(source[x], source[x + 1U]);
#endif
                }
            }
        }
        else {
            for (uint32_t x = 0; x < KNOVGB_WIDTH; x++) {
#if defined(KNOVGBC_VDP2_DIRTY_LINES)
                const uint16_t pixel = source[x];
                destination[x] = pixel | 0x8000U;
                previous[x] = pixel;
#else
                destination[x] = source[x] | 0x8000U;
#endif
            }
        }
    }
#if defined(KNOVGBC_VDP2_DIRTY_LINES)
    last_presented_valid = true;
#endif
}
#else
void
saturn_platform_present_indexed8_strided(const uint8_t *pixels,
    uint32_t stride_pixels)
{
    if ((pixels == NULL) || (stride_pixels < KNOVGB_WIDTH)) return;

#if defined(KNOVGBC_VDP2_DMA_DIRECT)
    if (((uintptr_t)pixels & 3U) == 0U &&
        (stride_pixels & 3U) == 0U) {
        dma_present_direct_source_set(pixels, stride_pixels);
        __asm__ __volatile__("" ::: "memory");
        scu_dma_config_set(DMA_LEVEL, SCU_DMA_START_FACTOR_ENABLE,
            &dma_handle, NULL);
        scu_dma_level_end_set(DMA_LEVEL, NULL, NULL);
        scu_dma_level_fast_start(DMA_LEVEL);
        scu_dma_level_wait(DMA_LEVEL);
        return;
    }
#endif

    /* Defensive path for an unusual caller. VDP2 VRAM receives word stores,
     * never byte stores, even though each bitmap pixel occupies one byte. */
    for (uint32_t y = 0; y < KNOVGB_HEIGHT; y++) {
        volatile uint16_t *const destination =
            (volatile uint16_t *)(uintptr_t)(bitmap +
                ((BITMAP_Y + y) * BITMAP_WIDTH) + BITMAP_X);
        const uint8_t *const source = pixels + (y * stride_pixels);
        for (uint32_t x = 0; x < KNOVGB_WIDTH / 2U; x++) {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
            destination[x] = ((uint16_t)source[x * 2U] << 8) |
                source[x * 2U + 1U];
#elif defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
            destination[x] = source[x * 2U] |
                ((uint16_t)source[x * 2U + 1U] << 8);
#else
#error "A known target byte order is required for indexed bitmap uploads"
#endif
        }
    }
}
#endif

static const uint8_t *
glyph_rows(char character)
{
    static const uint8_t blank[5] = {0, 0, 0, 0, 0};
    static const uint8_t dot[5] = {0, 0, 0, 0, 2};
    static const uint8_t digits[10][5] = {
        {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7},
        {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7},
        {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7},
        {7, 4, 7, 5, 7}, {7, 1, 2, 2, 2},
        {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}
    };
    static const uint8_t letter_c[5] = {7, 4, 4, 4, 7};
    static const uint8_t letter_g[5] = {7, 4, 5, 5, 7};
    static const uint8_t letter_l[5] = {4, 4, 4, 4, 7};
    static const uint8_t letter_m[5] = {5, 7, 7, 5, 5};
    static const uint8_t letter_p[5] = {6, 5, 6, 4, 4};
    static const uint8_t letter_r[5] = {6, 5, 6, 5, 5};
#if defined(KNOVGBC_SLAVE_PPU_SHADOW) || defined(KNOVGBC_OPCODE_PROFILE)
    static const uint8_t letter_s[5] = {7, 4, 7, 1, 7};
#endif
    static const uint8_t letter_t[5] = {7, 2, 2, 2, 2};
    static const uint8_t letter_u[5] = {5, 5, 5, 5, 7};
    static const uint8_t letter_v[5] = {5, 5, 5, 5, 2};
    static const uint8_t letter_w[5] = {5, 5, 5, 7, 5};
#if defined(KNOVGBC_OPCODE_PROFILE)
    /* The normal performance HUD needs only a small alphabet. The separate
     * profiling image uses these extra glyphs for hexadecimal opcodes and
     * memory-region labels; they do not enter the baseline executable. */
    static const uint8_t letter_a[5] = {2, 5, 7, 5, 5};
    static const uint8_t letter_b[5] = {6, 5, 6, 5, 6};
    static const uint8_t letter_d[5] = {6, 5, 5, 5, 6};
    static const uint8_t letter_e[5] = {7, 4, 6, 4, 7};
    static const uint8_t letter_f[5] = {7, 4, 6, 4, 4};
    static const uint8_t letter_h[5] = {5, 5, 7, 5, 5};
    static const uint8_t letter_i[5] = {7, 2, 2, 2, 7};
    static const uint8_t letter_j[5] = {1, 1, 1, 5, 2};
    static const uint8_t letter_k[5] = {5, 5, 6, 5, 5};
    static const uint8_t letter_n[5] = {5, 7, 7, 5, 5};
    static const uint8_t letter_o[5] = {7, 5, 5, 5, 7};
    static const uint8_t letter_x[5] = {5, 5, 2, 5, 5};
    static const uint8_t letter_y[5] = {5, 5, 2, 2, 2};
#endif

    if ((character >= '0') && (character <= '9')) {
        return digits[(uint32_t)(character - '0')];
    }

    switch (character) {
        case '.': return dot;
        case 'C': return letter_c;
        case 'G': return letter_g;
        case 'L': return letter_l;
        case 'M': return letter_m;
        case 'P': return letter_p;
        case 'R': return letter_r;
#if defined(KNOVGBC_SLAVE_PPU_SHADOW) || defined(KNOVGBC_OPCODE_PROFILE)
        case 'S': return letter_s;
#endif
        case 'T': return letter_t;
        case 'U': return letter_u;
        case 'V': return letter_v;
        case 'W': return letter_w;
#if defined(KNOVGBC_OPCODE_PROFILE)
        case 'A': return letter_a;
        case 'B': return letter_b;
        case 'D': return letter_d;
        case 'E': return letter_e;
        case 'F': return letter_f;
        case 'H': return letter_h;
        case 'I': return letter_i;
        case 'J': return letter_j;
        case 'K': return letter_k;
        case 'N': return letter_n;
        case 'O': return letter_o;
        case 'X': return letter_x;
        case 'Y': return letter_y;
#endif
        default:  return blank;
    }
}

static void
hud_text_draw(uint32_t y, const char *text)
{
    if (text == NULL) {
        return;
    }

    for (uint32_t index = 0;
         (index < HUD_TEXT_CHARS) && (text[index] != '\0'); index++) {
        const uint8_t *const rows = glyph_rows(text[index]);
        const uint32_t glyph_x = HUD_X + (index * HUD_ADVANCE);

#if defined(KNOVGBC_VDP2_ZOOM) && defined(KNOVGBC_VDP2_INDEXED8)
        /* Indexed VDP2 VRAM must never receive byte or unaligned word stores.
         * Each three-pixel glyph row plus its spacer is two aligned pairs. */
        for (uint32_t row = 0; row < 5U; row++) {
            const uint8_t bits = rows[row];
            const uint16_t first = (bits & 4U) ? HUD_FOREGROUND : HUD_BACKGROUND;
            const uint16_t second = (bits & 2U) ? HUD_FOREGROUND : HUD_BACKGROUND;
            const uint16_t third = (bits & 1U) ? HUD_FOREGROUND : HUD_BACKGROUND;
            volatile uint16_t *const pairs =
                (volatile uint16_t *)(uintptr_t)(bitmap +
                    ((y + row) * BITMAP_WIDTH) + glyph_x);
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
            pairs[0] = (first << 8) | second;
            pairs[1] = (third << 8) | HUD_BACKGROUND;
#elif defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
            pairs[0] = first | (second << 8);
            pairs[1] = third | (HUD_BACKGROUND << 8);
#else
#error "A known target byte order is required for indexed HUD pairs"
#endif
        }
#else
        for (uint32_t row = 0; row < 5U; row++) {
            for (uint32_t column = 0; column < 3U; column++) {
                if ((rows[row] & (1U << (2U - column))) == 0U) {
                    continue;
                }
                for (uint32_t dy = 0; dy < HUD_SCALE; dy++) {
                    for (uint32_t dx = 0; dx < HUD_SCALE; dx++) {
#if defined(KNOVGBC_VDP2_INDEXED8)
                        /* Each doubled glyph pixel is written as a word so
                         * VDP2 VRAM is never addressed with byte stores. */
                        if (dx == 0U) {
                            volatile uint16_t *const pair =
                                (volatile uint16_t *)(uintptr_t)(bitmap +
                                    ((y + (row * HUD_SCALE) + dy) *
                                     BITMAP_WIDTH) + glyph_x +
                                    (column * HUD_SCALE));
                            *pair = (HUD_FOREGROUND << 8) | HUD_FOREGROUND;
                        }
#else
                        bitmap[((y + (row * HUD_SCALE) + dy) *
                            BITMAP_WIDTH) + glyph_x +
                            (column * HUD_SCALE) + dx] = HUD_FOREGROUND;
#endif
                    }
                }
            }
        }
#endif
    }
}

void
saturn_platform_hud_draw(const char *line_1, const char *line_2,
    const char *line_3)
{
#if defined(KNOVGBC_VDP12_NATIVE)
    saturn_vdp12_hud_draw(line_1, line_2, line_3);
    return;
#endif
    for (uint32_t y = 0; y < HUD_HEIGHT; y++) {
#if defined(KNOVGBC_VDP2_INDEXED8)
        volatile uint16_t *const destination =
            (volatile uint16_t *)(uintptr_t)(bitmap +
                ((HUD_Y + y) * BITMAP_WIDTH) + HUD_X);
        for (uint32_t x = 0; x < HUD_WIDTH / 2U; x++) {
            destination[x] = 0U;
        }
#else
        volatile uint16_t *const destination =
            bitmap + ((HUD_Y + y) * BITMAP_WIDTH) + HUD_X;
        for (uint32_t x = 0; x < HUD_WIDTH; x++) {
            destination[x] = HUD_BACKGROUND;
        }
#endif
    }

#if defined(KNOVGBC_VDP2_ZOOM)
    /* The unchanged profiler still supplies its ordinary space-separated
     * fields. Stack each five-character field inside the left black bar. */
    const char *const lines[3] = { line_1, line_2, line_3 };
    uint32_t text_y = HUD_Y;
    for (uint32_t line = 0; line < 3U; line++) {
        const char *source = lines[line];
        if (source == NULL) continue;

        while ((*source != '\0') &&
            (text_y + (5U * HUD_SCALE) <= HUD_Y + HUD_HEIGHT)) {
            while (*source == ' ') source++;
            if (*source == '\0') break;

            char field[HUD_TEXT_CHARS + 1U];
            uint32_t length = 0U;
            while ((*source != '\0') && (*source != ' ')) {
                if (length < HUD_TEXT_CHARS) field[length++] = *source;
                source++;
            }
            field[length] = '\0';
#if defined(KNOVGBC_HUD_3_DIGITS)
            /* Only the visual label becomes shorter; the profiler retains
             * its full measurement. Saturate at 999 instead of wrapping if
             * a pathological frame ever exceeds three decimal digits. */
            if ((length == 5U) &&
                (field[1] >= '0') && (field[1] <= '9') &&
                (field[2] >= '0') && (field[2] <= '9') &&
                (field[3] >= '0') && (field[3] <= '9') &&
                (field[4] >= '0') && (field[4] <= '9')) {
                if (field[1] == '0') {
                    field[1] = field[2];
                    field[2] = field[3];
                    field[3] = field[4];
                }
                else {
                    field[1] = '9';
                    field[2] = '9';
                    field[3] = '9';
                }
                field[4] = '\0';
            }
#endif
            hud_text_draw(text_y, field);
            text_y += HUD_LINE;
        }
    }
#else
    hud_text_draw(HUD_Y, line_1);
    hud_text_draw(HUD_Y + HUD_LINE, line_2);
    hud_text_draw(HUD_Y + (2U * HUD_LINE), line_3);
#endif
}

void
saturn_platform_wait_vblank(void)
{
    vdp2_tvmd_vblank_in_next_wait(1);
}
