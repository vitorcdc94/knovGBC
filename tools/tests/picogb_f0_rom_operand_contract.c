/* Exercise F0 operand fetches in both ROM banks and at mapping boundaries. */
#include <stdio.h>
#include <string.h>

#include "mi.h"
#include "microcode.h"

static r8 rom[0x10000];
static r8 wram[0x2000];
static r8 vram[0x2000];
static r8 sram[0x2000];
static r8 hram[0x100];
static r8 oam[0x100];
static unsigned io_reads;

static word
read_io(void *userdata, word addr, word data, word type)
{
    (void)userdata;
    (void)data;
    if(type == MB_TYPE_READ)
    {
        ++io_reads;
        return addr == 0U ? 0x85U : 0x42U;
    }
    return 0U;
}

static void
put_instruction_byte(word addr, r8 value)
{
    if(addr < 0x4000U)
        rom[addr] = value;
    else if(addr < 0x8000U)
        rom[0x8000U | (addr & 0x3FFFU)] = value;
    else if(addr < 0xA000U)
        vram[addr & 0x1FFFU] = value;
    else if(addr < 0xC000U)
        sram[addr & 0x1FFFU] = value;
    else if(addr < 0xE000U)
        wram[addr & 0x1FFFU] = value;
    else if(addr < 0xFE00U)
        wram[addr & 0x1FFFU] = value;
    else if(addr < 0xFF00U)
        oam[addr & 0xFFU] = value;
    else if(addr >= 0xFF80U)
        hram[addr & 0xFFU] = value;
}

static int
check_case(word pc, word from_io)
{
    struct mi_dispatch mi;
    mb_state mb;
    const r8 next_opcode = 0x42U;
    const word next_addr = (pc + 1U) & 0xFFFFU;
    const word expected_pc = (pc + 2U) & 0xFFFFU;
    const word expected_io_reads = from_io ? 2U : 0U;
    word cycles;

    memset(&mi, 0, sizeof(mi));
    memset(&mb, 0, sizeof(mb));
    memset(rom, 0, sizeof(rom));
    memset(wram, 0, sizeof(wram));
    memset(vram, 0, sizeof(vram));
    memset(sram, 0, sizeof(sram));
    memset(hram, 0, sizeof(hram));
    memset(oam, 0, sizeof(oam));
    io_reads = 0U;

    mi.ROM = rom;
    mi.WRAM = wram;
    mi.VRAM = vram;
    mi.SRAM = sram;
    mi.HRAM = hram;
    mi.OAM = oam;
    mi.BANK_ROM = 2U;
    mi.BANK_WRAM = 1U;
    mi.N_ROM = 4U;
    mi.N_SRAM = 1U;
    mi.dispatch_IO = read_io;
    mb.mi = &mi;
    mb.PC = pc;
    mb.IR.low = 0xF0U;
    mb.reg.F = 0xB0U;
    hram[0x85U] = 0x7AU;
    micache_invalidate(&mb.micache);

    if(!from_io)
        put_instruction_byte(pc, 0x85U);
    put_instruction_byte(next_addr, next_opcode);

    cycles = mb_exec(&mb);
    if(cycles != 3U || mb.PC != expected_pc ||
        mb.IR.raw != next_opcode || mb.reg.A != 0x7AU ||
        mb.reg.F != 0xB0U || io_reads != expected_io_reads)
    {
        fprintf(stderr,
            "F0 pc=%04X cycles=%u next_pc=%04X ir=%04X a=%02X f=%02X io=%u\n",
            pc, cycles, mb.PC, mb.IR.raw, mb.reg.A, mb.reg.F, io_reads);
        return 1;
    }
    return 0;
}

int
main(void)
{
    static const struct {
        word pc;
        word from_io;
    } cases[] = {
        {0x0201U, 0U}, /* Fixed ROM bank. */
        {0x3FFFU, 0U}, /* Fixed to switchable ROM boundary. */
        {0x4000U, 0U}, /* Switchable ROM bank 2. */
        {0x7FFFU, 0U}, /* Switchable ROM to VRAM boundary. */
        {0x8000U, 0U}, /* Non-ROM fallback: VRAM. */
        {0xC123U, 0U}, /* Non-ROM fallback: WRAM. */
        {0xFE00U, 0U}, /* Non-ROM fallback: OAM. */
        {0xFF00U, 1U}, /* Non-ROM fallback: I/O. */
        {0xFF80U, 0U}, /* Non-ROM fallback: HRAM. */
        {0xFFFFU, 0U}  /* PC wrap to fixed ROM. */
    };
    unsigned i;

    for(i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        if(check_case(cases[i].pc, cases[i].from_io))
            return 1;
    }
    printf("f0_rom_operand_contract: PASS cases=%u\n", i);
    return 0;
}
