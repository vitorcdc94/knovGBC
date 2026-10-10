#include <yaul.h>

/*
 * A deliberately small Saroo/Yaul execution probe.  It has no SameBoy code,
 * CD access, controller stack or sound driver.  A stable blue test card after
 * the Sega licence screen proves that the BIOS has transferred control to the
 * SH-2 program and that the basic VDP2 path is usable.
 */
#define DIAG_WIDTH  512U
#define DIAG_HEIGHT 256U

int
main(void)
{
    const vdp2_scrn_bitmap_format_t bitmap_format = {
        .scroll_screen = VDP2_SCRN_NBG0,
        .ccc = VDP2_SCRN_CCC_RGB_32768,
        .bitmap_size = VDP2_SCRN_BITMAP_SIZE_512X256,
        .palette_base = 0,
        .bitmap_base = VDP2_VRAM_ADDR(VDP2_VRAM_BANK_A0, 0)
    };
    const vdp2_vram_usage_t vram_usage = {
        .a0 = VDP2_VRAM_USAGE_TYPE_CPD_BPD,
        .a1 = VDP2_VRAM_USAGE_TYPE_CPD_BPD,
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
            { .t0 = VDP2_VRAM_CYCP_CHPNDR_NBG0,
              .t1 = VDP2_VRAM_CYCP_CHPNDR_NBG0,
              .t2 = VDP2_VRAM_CYCP_CHPNDR_NBG0,
              .t3 = VDP2_VRAM_CYCP_CHPNDR_NBG0,
              .t4 = VDP2_VRAM_CYCP_CPU_RW,
              .t5 = VDP2_VRAM_CYCP_CPU_RW,
              .t6 = VDP2_VRAM_CYCP_CPU_RW,
              .t7 = VDP2_VRAM_CYCP_CPU_RW },
            { .raw = 0xFFFFFFFF },
            { .raw = 0xFFFFFFFF }
        }
    };
    volatile uint16_t *const bitmap =
        (volatile uint16_t *)VDP2_VRAM_ADDR(VDP2_VRAM_BANK_A0, 0);

    vdp2_tvmd_display_clear();
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE,
        VDP2_TVMD_HORZ_NORMAL_A, VDP2_TVMD_VERT_224);
    vdp2_vram_usage_set(&vram_usage);
    vdp2_vram_cycp_set(&vram_cycles);
    vdp2_scrn_bitmap_format_set(&bitmap_format);
    vdp2_scrn_priority_set(VDP2_SCRN_NBG0, 7);
    vdp2_scrn_display_set(VDP2_SCRN_DISP_NBG0);

    for (uint32_t index = 0; index < DIAG_WIDTH * DIAG_HEIGHT; index++) {
        /* Opaque blue, with a lighter band every 32 lines. */
        bitmap[index] = ((index / DIAG_WIDTH) & 0x20U) ? 0xFC00 : 0xFE10;
    }

    vdp2_tvmd_display_set();
    vdp2_sync();
    vdp2_sync_wait();

    for (;;) {
        /* The screen intentionally stays unchanged; no interrupts required. */
    }
}
