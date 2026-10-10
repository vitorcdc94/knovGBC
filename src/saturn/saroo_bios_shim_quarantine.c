/*
 * Diagnostic-only build wrapper.  It compiles a second copy of the Saroo
 * callback shim whose logical SCU mask still follows Yaul, while the physical
 * SCU interrupt register remains fully masked until the visual probe reaches
 * main().  This tests the early IRQ handoff without changing the emulator.
 */
#define SAROO_SHIM_QUARANTINE_IRQS 1
#include "saroo_bios_shim.c"
