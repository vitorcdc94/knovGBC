#include <stdint.h>

/*
 * This target deliberately calls no Yaul API.  It runs after the complete
 * Yaul runtime has initialised, then takes control of VDP2 with polling only.
 * A magenta/cyan alternation proves both the custom bootstrap and __sys_init
 * ran successfully on real hardware.
 */

#define VDP1_TVMR       0x25D00000UL
#define VDP1_FBCR       0x25D00002UL
#define VDP1_PTMR       0x25D00004UL
#define VDP2_VRAM       0x25E00000UL
#define VDP2_TVMD       0x25F80000UL
#define VDP2_RAMCTL     0x25F8000EUL
#define VDP2_CYCA0L     0x25F80010UL
#define VDP2_CYCA0U     0x25F80012UL
#define VDP2_CYCA1L     0x25F80014UL
#define VDP2_CYCA1U     0x25F80016UL
#define VDP2_BGON       0x25F80020UL
#define VDP2_BKTAU      0x25F800ACUL
#define VDP2_BKTAL      0x25F800AEUL
#define SCU_IMS         0x25FE00A0UL
#define BIOS_SCU_MASK_VALUE 0x06000348UL

static inline void
write16(uint32_t address, uint16_t value)
{
    *(volatile uint16_t *)(uintptr_t)address = value;
}

static inline void
write32(uint32_t address, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)address = value;
}

static inline void
mask_all_interrupts(void)
{
    /* Keep Yaul's low-HWRAM mask mirror coherent with the hardware register.
     * Later mask-change callbacks must not accidentally re-enable an IRQ. */
    write32(BIOS_SCU_MASK_VALUE, 0x0000BFFFUL);
    write32(SCU_IMS, 0x0000BFFFUL);
}

/*
 * Yaul releases the CPU interrupt mask just before invoking user_init().
 * This probe intentionally keeps both interrupt controllers masked so it can
 * validate the complete initialization path independently of BIOS callbacks.
 */
void user_init(void) __attribute__((used));

void
user_init(void)
{
    __asm__ volatile(
        "mov #0xF0, r0\n\t"
        "ldc r0, sr\n\t"
        :
        :
        : "r0", "memory");

    mask_all_interrupts();
}

static void
delay(void)
{
    for (volatile uint32_t i = 0; i < 0x00300000UL; i++) {
    }
}

int
main(void)
{
    /* Keep interrupt delivery disabled: this probe tests bootstrap + Yaul
     * initialisation, not the higher-level callback infrastructure yet. */
    mask_all_interrupts();

    write16(VDP1_PTMR, 0);
    write16(VDP1_FBCR, 0);
    write16(VDP1_TVMR, 0);

    write16(VDP2_TVMD, 0);
    write16(VDP2_RAMCTL, 0x2300);
    write16(VDP2_CYCA0L, 0x0E44);
    write16(VDP2_CYCA0U, 0xEEEE);
    write16(VDP2_CYCA1L, 0x1E55);
    write16(VDP2_CYCA1U, 0xEEEE);
    write16(VDP2_BGON, 0);
    write16(VDP2_BKTAU, 0x0003);
    write16(VDP2_BKTAL, 0xFFFF);
    write16(VDP2_TVMD, 0x8010);

    volatile uint16_t *const backdrop =
      (volatile uint16_t *)(uintptr_t)(VDP2_VRAM + 0x0007FFFEUL);

    for (;;) {
        *backdrop = 0xFC1F; /* Magenta. */
        delay();
        *backdrop = 0xFFE0; /* Cyan. */
        delay();
    }
}
