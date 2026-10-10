#include <yaul.h>

/*
 * Stage 0 has one purpose: make a visible hardware write from main() without
 * relying on Yaul's display synchronisation or any peripheral.  If this
 * image does not alternate red and blue after the licence screen, execution
 * is failing before or at the handoff into the user program.
 */
#define BACKDROP_ADDRESS VDP2_VRAM_ADDR(VDP2_VRAM_BANK_A0, 0x01FFFE)

static void
delay(void)
{
    for (volatile uint32_t count = 0; count < 3000000U; count++) {
    }
}

int
main(void)
{
    volatile vdp2_ioregs_t *const vdp2_ioregs =
        (volatile vdp2_ioregs_t *)VDP2_IOREG_BASE;
    volatile uint16_t *const backdrop = (volatile uint16_t *)BACKDROP_ADDRESS;

    /* Give the SH-2 CPU a VRAM slot in both A banks before the write. */
    vdp2_ioregs->cyca0l = 0xEEEE;
    vdp2_ioregs->cyca0u = 0xEEEE;
    vdp2_ioregs->cyca1l = 0xEEEE;
    vdp2_ioregs->cyca1u = 0xEEEE;

    /* No NBG/VDP1 layer: only the VDP2 back-screen colour is enabled. */
    vdp2_ioregs->bgon = 0x0000;
    vdp2_ioregs->bktau = 0x0000;
    vdp2_ioregs->bktal = (uint16_t)(BACKDROP_ADDRESS >> 1);
    vdp2_ioregs->tvmd = 0x8010;

    for (;;) {
        *backdrop = 0x801F; /* Opaque red. */
        delay();
        *backdrop = 0xFC00; /* Opaque blue. */
        delay();
    }
}
