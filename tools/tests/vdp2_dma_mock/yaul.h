#pragma once

/* Host-only Yaul surface for the indirect VDP2 DMA contract. */
#include <stddef.h>
#include <stdint.h>

extern uint16_t test_vdp2_vram[512U * 256U];

#define VDP2_VRAM_BANK_A0 0
#define VDP2_VRAM_ADDR(bank, offset) \
    ((uintptr_t)(void *)test_vdp2_vram + (uintptr_t)(offset))

#define VDP2_SCRN_NBG0 0
#define VDP2_SCRN_CCC_RGB_32768 0
#define VDP2_SCRN_BITMAP_SIZE_512X256 0
#define VDP2_VRAM_USAGE_TYPE_CPD_BPD 0
#define VDP2_VRAM_USAGE_TYPE_NONE 0
#define VDP2_VRAM_CYCP_CHPNDR_NBG0 0
#define VDP2_VRAM_CYCP_CPU_RW 0
#define VDP2_TVMD_INTERLACE_NONE 0
#define VDP2_TVMD_HORZ_NORMAL_A 0
#define VDP2_TVMD_VERT_224 0
#define VDP2_SCRN_DISP_NBG0 0x0101
#define FIX16(value) ((int)(value))

typedef struct {
    int scroll_screen;
    int ccc;
    int bitmap_size;
    uintptr_t palette_base;
    uintptr_t bitmap_base;
} vdp2_scrn_bitmap_format_t;

typedef struct { int a0, a1, b0, b1; } vdp2_vram_usage_t;

typedef union {
    struct { uint32_t t0, t1, t2, t3, t4, t5, t6, t7; };
    uint32_t raw;
} test_vdp2_cycle_t;

typedef struct { test_vdp2_cycle_t pt[4]; } vdp2_vram_cycp_t;

extern vdp2_scrn_bitmap_format_t test_bitmap_format;
extern int test_scroll_x;
extern int test_scroll_y;
extern uint32_t test_cache_purge_count;

static inline void vdp2_tvmd_display_clear(void) {}
static inline void vdp2_tvmd_display_res_set(int a, int b, int c)
    { (void)a; (void)b; (void)c; }
static inline void vdp2_vram_usage_set(const vdp2_vram_usage_t *value)
    { (void)value; }
static inline void vdp2_vram_cycp_set(const vdp2_vram_cycp_t *value)
    { (void)value; }
static inline void vdp2_scrn_bitmap_format_set(
    const vdp2_scrn_bitmap_format_t *value) { test_bitmap_format = *value; }
static inline void vdp2_scrn_priority_set(int a, int b)
    { (void)a; (void)b; }
static inline void vdp2_scrn_scroll_x_set(int a, int b)
    { (void)a; test_scroll_x = b; }
static inline void vdp2_scrn_scroll_y_set(int a, int b)
    { (void)a; test_scroll_y = b; }
static inline void vdp2_scrn_display_set(int a) { (void)a; }
static inline void vdp2_sync(void) {}
static inline void vdp2_sync_wait(void) {}
static inline void vdp2_tvmd_display_set(void) {}
static inline void vdp2_tvmd_vblank_in_next_wait(int a) { (void)a; }

/* Match libYaul's 32-bit descriptor layout, including the terminal src bit. */
#define SCU_DMA_INDIRECT_TABLE_END 0x80000000UL
#define SCU_DMA_MODE_INDIRECT 1
#define SCU_DMA_SPACE_BUS_B 1
#define SCU_DMA_STRIDE_0_BYTES 0
#define SCU_DMA_STRIDE_2_BYTES 1
#define SCU_DMA_STRIDE_4_BYTES 2
#define SCU_DMA_UPDATE_NONE 0
#define SCU_DMA_UPDATE_RUP 0x00010000UL
#define SCU_DMA_UPDATE_WUP 0x00000100UL
#define SCU_DMA_START_FACTOR_ENABLE 7

typedef uint32_t scu_dma_level_t;
typedef int scu_dma_space_t;
typedef int scu_dma_mode_t;
typedef int scu_dma_stride_t;
typedef uint32_t scu_dma_update_t;
typedef int scu_dma_start_factor_t;
typedef struct { uint32_t len, dst, src; } scu_dma_xfer_t;
typedef union {
    scu_dma_xfer_t *indirect;
    scu_dma_xfer_t direct;
} scu_dma_xfer_type_t;
typedef struct {
    int space;
    int mode;
    int stride;
    uint32_t update;
    scu_dma_xfer_type_t xfer;
} scu_dma_level_cfg_t;
typedef struct { scu_dma_level_cfg_t cfg; } scu_dma_handle_t;

void scu_dma_config_buffer(scu_dma_handle_t *handle,
    const scu_dma_level_cfg_t *cfg);
void scu_dma_config_set(scu_dma_level_t level, int start_factor,
    const scu_dma_handle_t *handle, void (*callback)(void *));
void scu_dma_level_fast_start(scu_dma_level_t level);
void scu_dma_level_wait(scu_dma_level_t level);
void scu_dma_level_end_set(scu_dma_level_t level,
    void (*callback)(void *), void *work);

static inline void cpu_cache_purge(void) { test_cache_purge_count++; }
static inline void cpu_cache_area_purge(void *address, uint32_t len)
    { (void)address; (void)len; test_cache_purge_count++; }
