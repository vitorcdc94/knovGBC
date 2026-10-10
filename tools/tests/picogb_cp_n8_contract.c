/* Independent CP n8 contract for both the original and direct C decoder. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mi.h"
#include "microcode.h"

word mbh_fr_get(mb_state *mb, word flags);

static r8 rom[0x8000];
static r8 vram[0x2000];
static r8 sram[0x2000];
static r8 wram[0x2000];
static r8 hram[0x100];
static r8 oam[0x100];
static struct mi_dispatch mi;
static unsigned io_reads;
static word io_address[2];

static int
fail(const char *case_name, unsigned a, unsigned operand, unsigned actual)
{
    fprintf(stderr, "CP n8 %s: A=%02X n=%02X actual=%04X\n",
        case_name, a, operand, actual);
    return 1;
}

static void
reset_cpu(mb_state *mb, word pc)
{
    memset(mb, 0, sizeof(*mb));
    mb->mi = &mi;
    mb->PC = (r16)pc; /* FE was already prefetched. */
    mb->IR.raw = 0x00FEU;
    mb->SP = 0xD000U;
}

static word
contract_io(void *userdata, word address, word data, word type)
{
    (void)userdata;
    (void)data;
    if (type == MB_TYPE_READ) {
        if (io_reads < 2U) io_address[io_reads] = address;
        ++io_reads;
        return address == 0U ? 0x91U : 0xA7U;
    }
    return 0U;
}

int
main(void)
{
    static const r8 prior_flags[] = {0x00U, 0x10U, 0xA0U, 0xF0U};
    unsigned a, operand, prior;
    mb_state mb;

    memset(&mi, 0, sizeof(mi));
    mi.ROM = rom;
    mi.VRAM = vram;
    mi.SRAM = sram;
    mi.WRAM = wram;
    mi.HRAM = hram;
    mi.OAM = oam;
    mi.BANK_ROM = 1U;
    mi.BANK_WRAM = 1U;
    mi.N_ROM = 2U;
    mi.N_SRAM = 1U;
    mi.dispatch_IO = contract_io;
    rom[0x0201U] = 0xA7U;

    /* All 65,536 arithmetic pairs, with four incoming flag/deferred states.
     * CP must overwrite its own flags without changing A or the timing. */
    for (a = 0U; a < 0x100U; ++a) {
        for (operand = 0U; operand < 0x100U; ++operand) {
            const word ordinary_flags = MB_FLAG_N |
                (a == operand ? MB_FLAG_Z : 0U) |
                (a < operand ? MB_FLAG_C : 0U);
            const word materialized_flags = ordinary_flags |
                ((a & 0x0FU) < (operand & 0x0FU) ? MB_FLAG_H : 0U);
            rom[0x0200U] = (r8)operand;
            for (prior = 0U; prior < sizeof(prior_flags); ++prior) {
                reset_cpu(&mb, 0x0200U);
                mb.reg.A = (r8)a;
                mb.reg.F = prior_flags[prior];
                mb.FMC_MODE = prior & 1U ? MB_FMC_MODE_ADD_r8 :
                    MB_FMC_MODE_NONE;
                mb.FR1 = 0x1234U;
                mb.FR2 = 0x5678U;

                if (mb_exec(&mb) != 2U || mb.PC != 0x0202U ||
                    mb.IR.raw != 0x00A7U || mb.reg.A != a ||
                    mb.reg.F != ordinary_flags ||
                    mb.FMC_MODE != MB_FMC_MODE_SUB_r8 ||
                    mb.FR1 != a || mb.FR2 != operand) {
                    return fail("state/cycles", a, operand, mb.reg.F);
                }
                if (mbh_fr_get(&mb, mb.reg.F) != materialized_flags ||
                    mb.FMC_MODE != MB_FMC_MODE_NONE) {
                    return fail("half-borrow", a, operand, mb.reg.F);
                }
            }
        }
    }

    /* Fetch the immediate and next opcode across two common map edges. */
    reset_cpu(&mb, 0x3FFFU);
    mb.reg.A = 0x20U;
    rom[0x3FFFU] = 0x10U;
    rom[0x4000U] = 0xA7U;
    if (mb_exec(&mb) != 2U || mb.PC != 0x4001U ||
        mb.IR.raw != 0x00A7U || mb.reg.F != MB_FLAG_N) {
        return fail("ROM bank edge", 0x20U, 0x10U, mb.IR.raw);
    }

    reset_cpu(&mb, 0x7FFFU);
    mb.reg.A = 0x20U;
    rom[0x7FFFU] = 0x10U;
    vram[0] = 0xA7U;
    if (mb_exec(&mb) != 2U || mb.PC != 0x8001U ||
        mb.IR.raw != 0x00A7U || mb.reg.F != MB_FLAG_N) {
        return fail("ROM/VRAM edge", 0x20U, 0x10U, mb.IR.raw);
    }

    reset_cpu(&mb, 0xA000U);
    mb.reg.A = 0x20U;
    sram[0] = 0x21U;
    sram[1] = 0xA7U;
    if (mb_exec(&mb) != 2U || mb.PC != 0xA002U ||
        mb.IR.raw != 0x00A7U ||
        mb.reg.F != (MB_FLAG_N | MB_FLAG_C)) {
        return fail("SRAM fetch", 0x20U, 0x21U, mb.IR.raw);
    }

    reset_cpu(&mb, 0xC000U);
    mb.reg.A = 0x20U;
    wram[0] = 0x20U;
    wram[1] = 0xA7U;
    if (mb_exec(&mb) != 2U || mb.PC != 0xC002U ||
        mb.IR.raw != 0x00A7U ||
        mb.reg.F != (MB_FLAG_N | MB_FLAG_Z)) {
        return fail("WRAM fetch", 0x20U, 0x20U, mb.IR.raw);
    }

    reset_cpu(&mb, 0xFE00U);
    mb.reg.A = 0x20U;
    oam[0] = 0x10U;
    oam[1] = 0xA7U;
    if (mb_exec(&mb) != 2U || mb.PC != 0xFE02U ||
        mb.IR.raw != 0x00A7U || mb.reg.F != MB_FLAG_N) {
        return fail("OAM fetch", 0x20U, 0x10U, mb.IR.raw);
    }

    reset_cpu(&mb, 0xFF80U);
    mb.reg.A = 0x91U;
    hram[0x80U] = 0x91U;
    hram[0x81U] = 0xA7U;
    if (mb_exec(&mb) != 2U || mb.PC != 0xFF82U ||
        mb.IR.raw != 0x00A7U || mb.reg.F != (MB_FLAG_N | MB_FLAG_Z)) {
        return fail("HRAM fetch", 0x91U, 0x91U, mb.IR.raw);
    }

    reset_cpu(&mb, 0xFF00U);
    mb.reg.A = 0x91U;
    io_reads = 0U;
    if (mb_exec(&mb) != 2U || mb.PC != 0xFF02U ||
        mb.IR.raw != 0x00A7U || mb.reg.F != (MB_FLAG_N | MB_FLAG_Z) ||
        io_reads != 2U || io_address[0] != 0U || io_address[1] != 1U) {
        return fail("I/O fetch", 0x91U, 0x91U, io_reads);
    }

    reset_cpu(&mb, 0xFFFFU);
    mb.reg.A = 0x35U;
    hram[0xFFU] = 0x35U;
    rom[0] = 0xA7U;
    if (mb_exec(&mb) != 2U || mb.PC != 0x0001U ||
        mb.IR.raw != 0x00A7U ||
        mb.reg.F != (MB_FLAG_N | MB_FLAG_Z)) {
        return fail("PC wrap", 0x35U, 0x35U, mb.PC);
    }

    /* An already-enabled IRQ owns the step; delayed EI does not. */
    reset_cpu(&mb, 0x0200U);
    mb.reg.A = 0x42U;
    mb.IME = 1U;
    mb.IE = 1U;
    mb.IF = 1U;
    rom[0x0200U] = 0x42U;
    rom[0x0040U] = 0xA7U;
    if (mb_exec(&mb) != 5U || mb.reg.A != 0x42U ||
        mb.IR.raw != 0x00A7U || mb.PC != 0x0041U || mb.IME != 0U) {
        return fail("IRQ priority", 0x42U, 0x42U, mb.PC);
    }

    reset_cpu(&mb, 0x0200U);
    mb.reg.A = 0x42U;
    mb.IME_ASK = 1U;
    mb.IE = 1U;
    mb.IF = 1U;
    if (mb_exec(&mb) != 2U || mb.IME != 1U || mb.IME_ASK != 0U ||
        mb.IR.raw != 0x00A7U || mb.PC != 0x0202U ||
        mb.reg.F != (MB_FLAG_N | MB_FLAG_Z)) {
        return fail("delayed EI", 0x42U, 0x42U, mb.PC);
    }

    puts("picogb_cp_n8_contract: PASS (262144 arithmetic states + edges/IRQ)");
    return 0;
}
