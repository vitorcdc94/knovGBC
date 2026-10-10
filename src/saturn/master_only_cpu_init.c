/*
 * Master-only replacement for Yaul's CPU startup implementation.
 *
 * This file intentionally mirrors Yaul's cpu_init.c for all Master SH-2
 * services: exception vectors, DIVU, FRT, WDT, SCI and CPU-DMAC. The one
 * omitted operation is cpu_dual_comm_mode_set(), which otherwise installs a
 * Slave entry and sends SSHON.
 *
 * cpu_exceptions.sx also calls __exception_assert(), normally supplied by the
 * same Yaul compilation unit. A small local terminal handler is provided for
 * that exceptional path so the original cpu_init.o (and its Slave-starting
 * code) is never pulled from libyaul.a just to satisfy that symbol.
 */

#include <sys/cdefs.h>

#include <stdint.h>

/* math_compat.h is force-included for SameBoy. Yaul's fixed-point headers
 * define their own float-typed M_PI; this startup file does not use it, so
 * allow that header to establish its normal definition without a warning. */
#ifdef M_PI
#undef M_PI
#endif

#include <cpu/divu.h>
#include <cpu/dmac.h>
#include <cpu/frt.h>
#include <cpu/intc.h>
#include <cpu/map.h>
#include <cpu/registers.h>
#include <cpu/wdt.h>

#include <smpc/smc.h>

/* Yaul deliberately exposes these startup helpers only to its own sources.
 * Their ABI is stable within the project-local Yaul version used to build this
 * image, and declaring them here lets the Master-only replacement retain the
 * normal Master peripheral setup without importing Yaul private headers. */
extern void __cpu_divu_init(void);
extern void __cpu_dmac_init(void);
extern void __cpu_sci_init(void);

void
__cpu_init(void)
{
    extern void __exception_illegal_instruction(void);
    extern void __exception_illegal_slot(void);
    extern void __exception_cpu_address_error(void);
    extern void __exception_dma_address_error(void);

    /* Install the same Master SH-2 exception handlers as Yaul. */
    cpu_intc_ihr_set(CPU_INTC_INTERRUPT_ILLEGAL_INSTRUCTION,
      __exception_illegal_instruction);
    cpu_intc_ihr_set(CPU_INTC_INTERRUPT_ILLEGAL_SLOT,
      __exception_illegal_slot);
    cpu_intc_ihr_set(CPU_INTC_INTERRUPT_CPU_ADDRESS_ERROR,
      __exception_cpu_address_error);
    cpu_intc_ihr_set(CPU_INTC_INTERRUPT_DMA_ADDRESS_ERROR,
      __exception_dma_address_error);

    volatile cpu_ioregs_t *const cpu_ioregs =
      (volatile cpu_ioregs_t *)CPU_IOREG_BASE;
    cpu_ioregs->vcrwdt &= ~0x007FU;
    cpu_ioregs->vcrwdt |= CPU_INTC_INTERRUPT_BSC;

    __cpu_divu_init();
    cpu_frt_init(CPU_FRT_CLOCK_DIV_8);
    cpu_wdt_init(CPU_WDT_CLOCK_DIV_2);
    __cpu_sci_init();
    __cpu_dmac_init();

    /* Yaul normally turns the Slave on immediately after this point. Issue
     * the complementary command instead, in the same interrupt-safe phase,
     * so the Master-only contract also holds if a loader left the Slave on. */
    const uint8_t old_mask = cpu_intc_mask_get();
    cpu_intc_mask_set(15);
    (void)smpc_smc_sshoff_call();
    cpu_intc_mask_set(old_mask);
}

void __used __noreturn
__exception_assert(const cpu_registers_t *regs, const char *exception_name)
{
    /* The arguments are intentionally unused. This handler must not call
     * Yaul's reset/debug path, because that would pull cpu_dual.o back into a
     * Master-only image. Hold the CPU with interrupts masked instead. */
    (void)regs;
    (void)exception_name;
    cpu_intc_mask_set(15);
    for (;;) {
        __asm__ volatile("nop");
    }
}
