/*
 * Direct contract for the shared next-opcode fetch.  It compares the
 * macro-gated inline ROM path with the established complete map, including
 * the uncommon RAM, OAM, I/O, HRAM and 16-bit PC-wrap fallbacks.
 */

#include <stdio.h>
#include <string.h>

#include "mi.h"
#include "microcode.h"
#include "microcode_dispatch.h"

static r8 rom[0x8000];
static r8 wram[0x8000];
static r8 vram[0x4000];
static r8 sram[0x2000];
static r8 hram[0x100];
static r8 oam[0x100];

static word
fetch_contract_io(void *userdata, word address, word data, word type)
{
    (void)userdata;
    (void)data;

    if (type == MB_TYPE_READ)
        return (address == 0x0000U) ? 0x5AU : 0xFFU;

    return 0U;
}

static int
expect_fetch(mb_state *mb, word pc, word expected, word expected_next_pc)
{
    word value;

    mb->PC = pc;
    value = mch_memory_fetch_PC(mb);

    if (value == expected && mb->PC == expected_next_pc)
        return 1;

    fprintf(stderr, "fetch mismatch pc=%04X value=%02X next=%04X\n",
        pc, value, mb->PC);
    return 0;
}

int
main(void)
{
    struct mi_dispatch memory;
    mb_state mb;
    int ok = 1;

    memset(&memory, 0, sizeof(memory));
    memset(&mb, 0, sizeof(mb));
    memset(rom, 0, sizeof(rom));
    memset(wram, 0, sizeof(wram));
    memset(vram, 0, sizeof(vram));
    memset(sram, 0, sizeof(sram));
    memset(hram, 0, sizeof(hram));
    memset(oam, 0, sizeof(oam));

    memory.ROM = rom;
    memory.WRAM = wram;
    memory.VRAM = vram;
    memory.SRAM = sram;
    memory.HRAM = hram;
    memory.OAM = oam;
    memory.BANK_ROM = 1U;
    memory.BANK_WRAM = 1U;
    memory.BANK_VRAM = 0U;
    memory.BANK_SRAM = 0U;
    memory.dispatch_IO = fetch_contract_io;
    mb.mi = &memory;

    rom[0x0201] = 0x11U;
    rom[0x4012] = 0x22U;
    vram[0x0000] = 0x33U;
    wram[0x0123] = 0x44U;
    oam[0x0000] = 0x55U;
    hram[0x0080] = 0x66U;
    hram[0x00FF] = 0x77U;

    ok &= expect_fetch(&mb, 0x0201U, 0x11U, 0x0202U);
    ok &= expect_fetch(&mb, 0x4012U, 0x22U, 0x4013U);
    ok &= expect_fetch(&mb, 0x8000U, 0x33U, 0x8001U);
    ok &= expect_fetch(&mb, 0xC123U, 0x44U, 0xC124U);
    ok &= expect_fetch(&mb, 0xFE00U, 0x55U, 0xFE01U);
    ok &= expect_fetch(&mb, 0xFF00U, 0x5AU, 0xFF01U);
    ok &= expect_fetch(&mb, 0xFF80U, 0x66U, 0xFF81U);
    ok &= expect_fetch(&mb, 0xFFFFU, 0x77U, 0x0000U);

    if (!ok)
        return 1;

    puts("fetch_contract=PASS");
    return 0;
}
