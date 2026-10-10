#include <stdint.h>

#include <cpu/intc.h>

/*
 * This program deliberately does not call Yaul's __sys_init().  It recreates
 * only its deterministic sequence, leaving a visible direct-VDP2 checkpoint
 * before every call.  A last visible colour therefore identifies the first
 * subsystem that failed to return on real hardware.
 */

#define VDP1_TVMR       0x25D00000UL
#define VDP1_FBCR       0x25D00002UL
#define VDP1_PTMR       0x25D00004UL
#define VDP2_VRAM_BASE  0x25E00000UL
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

extern uint32_t __bss_start[];
extern uint32_t __bss_end[];

extern void user_early_init(void);
extern void __atexit_init(void);
extern void __mm_init(void);
extern void __cpu_init(void);
extern void __scu_init(void);
extern void __vdp_init(void);
extern void cpu_cache_purge(void);

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

static void
delay(void)
{
    for (volatile uint32_t i = 0; i < 0x00600000UL; i++) {
    }
}

static void
video_setup(void)
{
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
}

static void
marker(uint16_t color)
{
    *(volatile uint16_t *)(uintptr_t)(VDP2_VRAM_BASE + 0x0007FFFEUL) = color;
    delay();
}

static void
bss_clear(void)
{
    volatile uint32_t *cursor = __bss_start;

    while (cursor < __bss_end) {
        *cursor++ = 0;
    }
}

void bootstrap_trace(void)
  __attribute__((noreturn, noinline, used, externally_visible));

void
bootstrap_trace(void)
{
    video_setup();

    /* White: bare entry works and BSS clearing follows. */
    marker(0xFFFF);
    bss_clear();

    /* Cyan: the self-contained callback layer follows. */
    marker(0xFFE0);
    user_early_init();

    /* Blue: atexit follows. */
    marker(0xFC00);
    __atexit_init();

    /* Yellow: memory allocator initialisation follows. */
    marker(0x83FF);
    __mm_init();

    /* Magenta: CPU initialisation follows. */
    marker(0xFC1F);
    __cpu_init();

    /* Red: SCU initialisation follows. */
    marker(0x801F);
    __scu_init();

    /* Orange: VDP initialisation follows; that stage blanks TVMD itself. */
    marker(0x821F);
    __vdp_init();

    /* Purple: the normal startup path purges the CPU cache after VDP. */
    video_setup();
    marker(0xF81F);
    cpu_cache_purge();

    /* White: the next instruction is the same CPU-interrupt handoff that
     * __sys_init() performs before user_init(). If an interrupt reaches a
     * bad vector in this tiny window, white is the last stable marker. */
    video_setup();
    marker(0xFFFF);
    cpu_intc_mask_set(0);

    /* This is deliberately inlined rather than delegated to another C
     * function: it mirrors bootstrap_probe_main's user_init() immediately
     * after the handoff, while restoring both the CPU mask and Yaul's SCU
     * mask mirror before any other application code executes. */
    __asm__ volatile(
        "mov #0xF0, r0\n\t"
        "ldc r0, sr\n\t"
        :
        :
        : "r0", "memory");
    *(volatile uint32_t *)(uintptr_t)BIOS_SCU_MASK_VALUE = 0x0000BFFFUL;
    *(volatile uint32_t *)(uintptr_t)SCU_IMS = 0x0000BFFFUL;

    /* Green: every non-empty Yaul startup stage and the first interrupt
     * handoff returned safely. */
    write32(SCU_IMS, 0x0000BFFFUL);
    video_setup();
    for (;;) {
        marker(0x83E0);
    }
}
