#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fabric.h"
#include "mi.h"
#include "microcode.h"
#include "picogb_idle_spin.h"
#include "ppu.h"

enum { LOOP_PC = 0x0200U, ROM_BYTES = 0x10000U };

static r8 rom[ROM_BYTES];
static r8 wram[0x2000U];
static r8 vram[0x2000U];
static r8 sram[0x2000U];
static r8 hram[0x100U];
static r8 oam[0x100U];
static r8 original_rom[ROM_BYTES];
static r8 original_wram[sizeof(wram)];
static r8 original_vram[sizeof(vram)];
static r8 original_sram[sizeof(sram)];
static r8 original_hram[sizeof(hram)];
static r8 original_oam[sizeof(oam)];
#if defined(KNOVGBC_IDLE_SPIN_CACHE)
static r8 alternate_rom[ROM_BYTES];
static r8 alternate_hram[sizeof(hram)];
#endif

struct fixture {
    mb_state cpu;
    struct mi_dispatch memory;
    ppu_t ppu;
    struct pgf_userdata_t fabric;
};

static void
put_rom_byte(word address, word bank, r8 value)
{
    word offset = address;

    if (address >= 0x4000U) {
        offset = bank * 0x4000U + (address & 0x3FFFU);
    }
    rom[offset] = value;
}

static void
put_loop(word pc, word bank, r8 hram_offset)
{
    put_rom_byte(pc + 0U, bank, 0xF0U);
    put_rom_byte(pc + 1U, bank, hram_offset);
    put_rom_byte(pc + 2U, bank, 0xA7U);
    put_rom_byte(pc + 3U, bank, 0x28U);
    put_rom_byte(pc + 4U, bank, 0xFBU);
}

static void
setup(struct fixture *fixture, word pc, word bank, r8 hram_offset)
{
#if defined(KNOVGBC_IDLE_SPIN_CACHE)
    picogb_idle_spin_cache_reset();
#endif
    memset(fixture, 0, sizeof(*fixture));
    memset(rom, 0U, sizeof(rom));
    memset(wram, 0U, sizeof(wram));
    memset(vram, 0U, sizeof(vram));
    memset(sram, 0U, sizeof(sram));
    memset(hram, 0U, sizeof(hram));
    memset(oam, 0U, sizeof(oam));
    put_loop(pc, bank, hram_offset);

    fixture->memory.ROM = rom;
    fixture->memory.WRAM = wram;
    fixture->memory.VRAM = vram;
    fixture->memory.SRAM = sram;
    fixture->memory.HRAM = hram;
    fixture->memory.OAM = oam;
    fixture->memory.N_ROM = 4U;
    fixture->memory.N_SRAM = 1U;
    fixture->memory.BANK_ROM = bank;
    fixture->memory.BANK_WRAM = 1U;
    fixture->memory.ROM_MAPPER = 0U;
    fixture->memory.userdata = &fixture->fabric;
    fixture->memory.dispatch_ROM = pgf_cb_ROM_;
    fixture->memory.dispatch_IO = pgf_cb_IO_;

    fixture->cpu.mi = &fixture->memory;
    fixture->cpu.PC = (r16)(pc + 1U);
    fixture->cpu.SP = 0xFFFEU;
    fixture->cpu.IR.low = 0xF0U;
    fixture->cpu.reg.A = 0x5AU;
    fixture->cpu.reg.F = MB_FLAG_C;
    fixture->cpu.reg.BC = 0x1357U;
    fixture->cpu.reg.DE = 0x2468U;
    fixture->cpu.reg.HL = 0x9ABCU;
    fixture->cpu.FR1 = 0x55AAU;
    fixture->cpu.FR2 = 0xAA55U;
    fixture->cpu.DIV = 0x26E168U;
    fixture->cpu.IME = 1U;
    fixture->cpu.IE = 1U;
    micache_invalidate(&fixture->cpu.micache);

    fixture->ppu.rLCDC = 0x91U;
    fixture->ppu.next_update_ticks = 140U; /* Four trips strictly before event. */
    fixture->fabric.mb = &fixture->cpu;
    fixture->fabric.ppu = &fixture->ppu;
}

static int
same_cpu(const mb_state *left, const mb_state *right)
{
    return memcmp(&left->reg, &right->reg, sizeof(left->reg)) == 0 &&
        left->IR.raw == right->IR.raw &&
        left->IMM.raw == right->IMM.raw &&
        left->PC == right->PC && left->SP == right->SP &&
        left->IF == right->IF && left->IE == right->IE &&
        left->IME == right->IME && left->IME_ASK == right->IME_ASK &&
        left->DIV == right->DIV && left->HALTING == right->HALTING &&
        left->FR1 == right->FR1 && left->FR2 == right->FR2 &&
        left->FMC_MODE == right->FMC_MODE && left->mi == right->mi;
}

static void
snapshot_memory(void)
{
    memcpy(original_rom, rom, sizeof(rom));
    memcpy(original_wram, wram, sizeof(wram));
    memcpy(original_vram, vram, sizeof(vram));
    memcpy(original_sram, sram, sizeof(sram));
    memcpy(original_hram, hram, sizeof(hram));
    memcpy(original_oam, oam, sizeof(oam));
}

static int
memory_unchanged(void)
{
    return memcmp(original_rom, rom, sizeof(rom)) == 0 &&
        memcmp(original_wram, wram, sizeof(wram)) == 0 &&
        memcmp(original_vram, vram, sizeof(vram)) == 0 &&
        memcmp(original_sram, sram, sizeof(sram)) == 0 &&
        memcmp(original_hram, hram, sizeof(hram)) == 0 &&
        memcmp(original_oam, oam, sizeof(oam)) == 0;
}

