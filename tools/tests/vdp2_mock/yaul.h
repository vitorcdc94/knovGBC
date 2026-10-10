#pragma once

/* Minimal VDP2 surface for testing the actual platform_video.c on a host. */
#include <stdint.h>
#include <stddef.h>

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
#define VDP2_SCRN_DISP_NBG0 0
#define FIX16(value) ((int)(value))

typedef struct {
    int scroll_screen;
    int ccc;
    int bitmap_size;
    uintptr_t palette_base;
    uintptr_t bitmap_base;
} vdp2_scrn_bitmap_format_t;

typedef struct {
    int a0;
    int a1;
    int b0;
    int b1;
} vdp2_vram_usage_t;

typedef union {
    struct {
        uint32_t t0, t1, t2, t3, t4, t5, t6, t7;
    };
    uint32_t raw;
} test_vdp2_cycle_t;

typedef struct {
    test_vdp2_cycle_t pt[4];
} vdp2_vram_cycp_t;

static inline void vdp2_tvmd_display_clear(void) {}
static inline void vdp2_tvmd_display_res_set(int a, int b, int c)
    { (void)a; (void)b; (void)c; }
static inline void vdp2_vram_usage_set(const vdp2_vram_usage_t *value)
    { (void)value; }
static inline void vdp2_vram_cycp_set(const vdp2_vram_cycp_t *value)
    { (void)value; }
static inline void vdp2_scrn_bitmap_format_set(
    const vdp2_scrn_bitmap_format_t *value) { (void)value; }
static inline void vdp2_scrn_priority_set(int a, int b)
    { (void)a; (void)b; }
static inline void vdp2_scrn_scroll_x_set(int a, int b)
    { (void)a; (void)b; }
static inline void vdp2_scrn_scroll_y_set(int a, int b)
    { (void)a; (void)b; }
static inline void vdp2_scrn_display_set(int a) { (void)a; }
static inline void vdp2_sync(void) {}
static inline void vdp2_sync_wait(void) {}
static inline void vdp2_tvmd_display_set(void) {}
static inline void vdp2_tvmd_vblank_in_next_wait(int a) { (void)a; }
