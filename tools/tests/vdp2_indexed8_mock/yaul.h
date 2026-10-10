#pragma once

/* Reuse the DMA mock's register and SCU surface, but keep its RGB tests
 * untouched. Rename its no-op display setters so this test can inspect the
 * bank/cycle setup and the opacity bit supplied to NBG0. */
#define vdp2_vram_usage_set vdp2_vram_usage_set_discarded
#define vdp2_vram_cycp_set vdp2_vram_cycp_set_discarded
#define vdp2_scrn_display_set vdp2_scrn_display_set_discarded
#include "../vdp2_dma_mock/yaul.h"
#undef vdp2_vram_usage_set
#undef vdp2_vram_cycp_set
#undef vdp2_scrn_display_set

/* Give otherwise identical no-op mock constants distinct values. */
#undef VDP2_VRAM_USAGE_TYPE_CPD_BPD
#undef VDP2_VRAM_USAGE_TYPE_NONE
#undef VDP2_VRAM_CYCP_CHPNDR_NBG0
#undef VDP2_VRAM_CYCP_CPU_RW
#define VDP2_VRAM_USAGE_TYPE_CPD_BPD 1
#define VDP2_VRAM_USAGE_TYPE_NONE 2
#define VDP2_VRAM_CYCP_CHPNDR_NBG0 1
#define VDP2_VRAM_CYCP_CPU_RW 2

#define VDP2_SCRN_CCC_PALETTE_256 1

extern uint16_t test_vdp2_cram[2048U];
extern vdp2_vram_usage_t test_vram_usage;
extern vdp2_vram_cycp_t test_vram_cycles;
extern int test_display_flags;

static inline void
vdp2_vram_usage_set(const vdp2_vram_usage_t *value)
{
    test_vram_usage = *value;
}

static inline void
vdp2_vram_cycp_set(const vdp2_vram_cycp_t *value)
{
    test_vram_cycles = *value;
}

static inline void
vdp2_scrn_display_set(int flags)
{
    test_display_flags = flags;
}

#define VDP2_CRAM_ADDR(index) \
    ((uintptr_t)(void *)&test_vdp2_cram[(index)])

#if defined(KNOVGBC_VDP2_ZOOM)
/* The normal DMA mock uses integer FIX16 values. The zoom contract needs
 * the fractional coordinate increment to remain observable on the host. */
#undef FIX16
#define FIX16(value) ((int)((double)(value) * 65536.0 + 0.5))
extern int test_zoom_x;
extern int test_zoom_y;
static inline void
vdp2_scrn_reduction_x_set(int screen, int increment)
{
    (void)screen;
    test_zoom_x = increment;
}
static inline void
vdp2_scrn_reduction_y_set(int screen, int increment)
{
    (void)screen;
    test_zoom_y = increment;
}
#endif
