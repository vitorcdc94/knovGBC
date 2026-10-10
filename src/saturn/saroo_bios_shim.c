/*
 * SAROO compatibility shim for the Saturn system-program callback area.
 *
 * Yaul normally delegates interrupt registration and SCU interrupt masking to
 * small routines installed by the Saturn BIOS in low H-WRAM. Some SAROO boot
 * paths leave those callback pointers unset. Calling one then jumps to an
 * invalid address before main() is reached.
 *
 * user_early_init() runs after BSS is cleared but before Yaul initialises the
 * CPU, SCU and VDP. This file supplies the small subset of system-program
 * services used by this application and Yaul, without relying on the resident
 * BIOS implementation. This is intentional: on the affected SAROO path the
 * callbacks can look non-null yet still point to an unusable system-program
 * state.
 */

#include <stdint.h>

#include <cpu/intc.h>
#include <cpu/registers.h>
#include <scu/ic.h>
#include <scu/map.h>

#define BIOS_WORK_RAM_BASE        0x06000000UL
#define BIOS_SCU_IHR_SET          0x06000300UL
#define BIOS_SCU_IHR_GET          0x06000304UL
#define BIOS_CPU_IHR_SET          0x06000310UL
#define BIOS_CPU_IHR_GET          0x06000314UL
#define BIOS_CLOCK_SET            0x06000320UL
#define BIOS_CLOCK_GET            0x06000324UL
#define BIOS_SCU_PRIORITY_SET     0x06000280UL
#define BIOS_SCU_MASK_SET         0x06000340UL
#define BIOS_SCU_MASK_CHANGE      0x06000344UL
#define BIOS_SCU_MASK_VALUE       0x06000348UL
#define BIOS_DEFAULT_HANDLER      0x0600083CUL
#define BIOS_SCU_PRIORITY_TABLE   0x06000A80UL

#define SCU_VECTOR_FIRST          0x40U
#define SCU_VECTOR_LAST           0x4DU
#define SCU_HANDLER_COUNT         (SCU_VECTOR_LAST - SCU_VECTOR_FIRST + 1U)

typedef void (*interrupt_handler_t)(void);

static interrupt_handler_t scu_handlers[SCU_HANDLER_COUNT];

/*
 * Default System Program priorities for vectors 0x40..0x5F. Each word stores
 * the temporary SH-2 SR mask in the upper half and the signed SCU IMS mask in
 * the lower half. The original BIOS dispatcher applies this around every
 * registered callback; omitting it permits VBlank/DMA interrupt re-entry.
 */
static const uint32_t default_scu_priorities[32] = {
    0x00F0FFFFUL, 0x00E0FFFEUL, 0x00D0FFFCUL, 0x00C0FFF8UL,
    0x00B0FFF0UL, 0x00A0FFE0UL, 0x0090FFC0UL, 0x0080FF80UL,
    0x0080FF80UL, 0x0070FE00UL, 0x0070FE00UL, 0x0070FE00UL,
    0x0070FE00UL, 0x0070FE00UL, 0x0070FE00UL, 0x0070FE00UL,
    0x0070FE00UL, 0x0070FE00UL, 0x0070FE00UL, 0x0070FE00UL,
    0x0070FE00UL, 0x0070FE00UL, 0x0070FE00UL, 0x0070FE00UL,
    0x0070FE00UL, 0x0070FE00UL, 0x0070FE00UL, 0x0070FE00UL,
    0x0070FE00UL, 0x0070FE00UL, 0x0070FE00UL, 0x0070FE00UL
};

static volatile uint32_t *
callback_word(uint32_t address)
{
    return (volatile uint32_t *)address;
}

static volatile uint32_t *
master_vector_table(void)
{
    return (volatile uint32_t *)BIOS_WORK_RAM_BASE;
}

static volatile uint32_t *
scu_priority_table(void)
{
    return (volatile uint32_t *)BIOS_SCU_PRIORITY_TABLE;
}

static inline void
cpu_sr_set_barrier(uint32_t value)
{
    /* Yaul's inline setter has no memory clobber. The dispatcher requires the
     * SR transition to stay ordered with the two IMS writes. */
    __asm__ volatile("ldc %0, sr" : : "r"(value) : "memory");
}

static inline void
scu_hardware_mask_write(uint32_t mask)
{
#if defined(SAROO_SHIM_QUARANTINE_IRQS)
    (void)mask;
    *(volatile uint32_t *)SCU(IMS) = SCU_IC_MASK_ALL;
#else
    *(volatile uint32_t *)SCU(IMS) = mask;
#endif
}

static void __interrupt_handler
empty_interrupt(void)
{
}

static void
bios_cpu_ihr_set(uint32_t vector, interrupt_handler_t handler)
{
    /* The slave table is 0x400 bytes after the master table, so Yaul's
     * slave-vector offset works with this single base address as well. */
    /* The master table occupies vectors 0x000..0x0FF and the slave table
     * occupies 0x100..0x1FF. Keep the entire slave range available rather
     * than accepting only the few vectors used by the current Yaul startup. */
    if (vector >= 0x200U) {
        return;
    }

    master_vector_table()[vector] = (uint32_t)(uintptr_t)
      ((handler != NULL) ? handler : empty_interrupt);
}

static interrupt_handler_t
bios_cpu_ihr_get(uint32_t vector)
{
    if (vector >= 0x200U) {
        return NULL;
    }

    return (interrupt_handler_t)(uintptr_t)master_vector_table()[vector];
}

static void __attribute__((noinline))
scu_dispatch(uint32_t index)
{
    volatile uint32_t *const mask_mirror =
      callback_word(BIOS_SCU_MASK_VALUE);
    const uint32_t saved_mask = *mask_mirror;
    const uint32_t priority = scu_priority_table()[index];
    const uint32_t priority_mask =
      (uint32_t)(int32_t)(int16_t)(priority & 0xFFFFU);
    const uint32_t temporary_mask = saved_mask | priority_mask;
    interrupt_handler_t const handler = scu_handlers[index];

    /* Match the Saturn System Program's dispatcher. The callback itself is a
     * normal C function (RTS); only the outer trampoline performs RTE. */
    cpu_sr_set_barrier((priority >> 16) & 0x00F0U);
    *mask_mirror = temporary_mask;
    scu_hardware_mask_write(temporary_mask);

    if (handler != NULL) {
        handler();
    }

    /* Close interrupt delivery before restoring shared state. RTE in the
     * trampoline restores the SR that the SH-2 saved on interrupt entry. */
    cpu_sr_set_barrier(0x000000F0UL);
    *mask_mirror = saved_mask;
    scu_hardware_mask_write(saved_mask);
}

#define DEFINE_SCU_TRAMPOLINE(index) \
    static void __interrupt_handler scu_trampoline_##index(void) \
    { \
        scu_dispatch(index); \
    }

DEFINE_SCU_TRAMPOLINE(0)
DEFINE_SCU_TRAMPOLINE(1)
DEFINE_SCU_TRAMPOLINE(2)
DEFINE_SCU_TRAMPOLINE(3)
DEFINE_SCU_TRAMPOLINE(4)
DEFINE_SCU_TRAMPOLINE(5)
DEFINE_SCU_TRAMPOLINE(6)
DEFINE_SCU_TRAMPOLINE(7)
DEFINE_SCU_TRAMPOLINE(8)
DEFINE_SCU_TRAMPOLINE(9)
DEFINE_SCU_TRAMPOLINE(10)
DEFINE_SCU_TRAMPOLINE(11)
DEFINE_SCU_TRAMPOLINE(12)
DEFINE_SCU_TRAMPOLINE(13)

static interrupt_handler_t const scu_trampolines[SCU_HANDLER_COUNT] = {
    scu_trampoline_0,  scu_trampoline_1,  scu_trampoline_2,
    scu_trampoline_3,  scu_trampoline_4,  scu_trampoline_5,
    scu_trampoline_6,  scu_trampoline_7,  scu_trampoline_8,
    scu_trampoline_9,  scu_trampoline_10, scu_trampoline_11,
    scu_trampoline_12, scu_trampoline_13
};

static void
bios_scu_ihr_set(uint32_t vector, interrupt_handler_t handler)
{
    if ((vector < SCU_VECTOR_FIRST) || (vector > SCU_VECTOR_LAST)) {
        return;
    }

    const uint8_t saved_cpu_mask = cpu_intc_mask_get();
    cpu_intc_mask_set(15);
    scu_handlers[vector - SCU_VECTOR_FIRST] = handler;
    cpu_intc_mask_set(saved_cpu_mask);
}

static interrupt_handler_t
bios_scu_ihr_get(uint32_t vector)
{
    if ((vector < SCU_VECTOR_FIRST) || (vector > SCU_VECTOR_LAST)) {
        return NULL;
    }

    const uint8_t saved_cpu_mask = cpu_intc_mask_get();
    cpu_intc_mask_set(15);
    interrupt_handler_t const handler =
      scu_handlers[vector - SCU_VECTOR_FIRST];
    cpu_intc_mask_set(saved_cpu_mask);

    return handler;
}

static void
bios_scu_mask_set(uint32_t mask)
{
    const uint8_t saved_cpu_mask = cpu_intc_mask_get();
    cpu_intc_mask_set(15);
    *callback_word(BIOS_SCU_MASK_VALUE) = mask;
    scu_hardware_mask_write(mask);
    cpu_intc_mask_set(saved_cpu_mask);
}

static void
bios_scu_mask_change(uint32_t and_mask, uint32_t or_mask)
{
    const uint8_t saved_cpu_mask = cpu_intc_mask_get();
    cpu_intc_mask_set(15);

    const uint32_t mask =
      (*callback_word(BIOS_SCU_MASK_VALUE) & and_mask) | or_mask;
    *callback_word(BIOS_SCU_MASK_VALUE) = mask;
    scu_hardware_mask_write(mask);

    cpu_intc_mask_set(saved_cpu_mask);
}

static void
bios_scu_priority_set(const scu_ic_priority_t *table)
{
    volatile uint32_t * const destination = scu_priority_table();
    const uint32_t * const source = (const uint32_t *)table;

    if (source == NULL) {
        return;
    }

    const uint8_t saved_cpu_mask = cpu_intc_mask_get();
    cpu_intc_mask_set(15);
    for (uint32_t i = 0; i < 32U; i++) {
        destination[i] = source[i];
    }
    cpu_intc_mask_set(saved_cpu_mask);
}

static void
bios_clock_set(uint8_t mode)
{
    /* The current renderer selects 320-pixel normal-A mode, which uses the
     * default 26 MHz setting. Record the requested value so Yaul's state
     * remains coherent; switching the hardware clock is not needed here. */
    *callback_word(BIOS_CLOCK_GET) = mode;
}

void
user_early_init(void)
{
    /* Use a self-contained system-program interface. In particular, do not
     * retain an apparently valid SAROO callback table: its resident interrupt
     * state is the source of the observed return to address 0x00000002. */
    cpu_reg_vbr_set(BIOS_WORK_RAM_BASE);

    for (uint32_t i = 0; i < SCU_HANDLER_COUNT; i++) {
        master_vector_table()[SCU_VECTOR_FIRST + i] =
          (uint32_t)(uintptr_t)scu_trampolines[i];
    }

    /* Saroo's load path does not guarantee that the BIOS priority table is
     * still resident. Install the canonical defaults used by the dispatcher
     * before any SCU source can be unmasked. */
    bios_scu_priority_set((const scu_ic_priority_t *)default_scu_priorities);

    /* Make Yaul's hard-coded default callback safe in fallback mode. */
    *(volatile uint16_t *)BIOS_DEFAULT_HANDLER = 0x000BU;       /* rts */
    *(volatile uint16_t *)(BIOS_DEFAULT_HANDLER + 2U) = 0x0009U; /* nop */

    *callback_word(BIOS_SCU_IHR_SET) = (uint32_t)(uintptr_t)bios_scu_ihr_set;
    *callback_word(BIOS_SCU_IHR_GET) = (uint32_t)(uintptr_t)bios_scu_ihr_get;
    *callback_word(BIOS_CPU_IHR_SET) = (uint32_t)(uintptr_t)bios_cpu_ihr_set;
    *callback_word(BIOS_CPU_IHR_GET) = (uint32_t)(uintptr_t)bios_cpu_ihr_get;
    *callback_word(BIOS_SCU_PRIORITY_SET) =
      (uint32_t)(uintptr_t)bios_scu_priority_set;
    *callback_word(BIOS_CLOCK_SET) = (uint32_t)(uintptr_t)bios_clock_set;
    *callback_word(BIOS_CLOCK_GET) = 0U;
    /* Yaul's __scu_init() calls this callback first.  Leaving it untouched
     * transfers execution to the incomplete callback supplied by the Saroo
     * boot path, before main() can run. */
    *callback_word(BIOS_SCU_MASK_SET) =
      (uint32_t)(uintptr_t)bios_scu_mask_set;
    *callback_word(BIOS_SCU_MASK_CHANGE) =
      (uint32_t)(uintptr_t)bios_scu_mask_change;

    bios_scu_mask_set(SCU_IC_MASK_ALL);
}
