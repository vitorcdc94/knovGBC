/*
 * Differential contract input for the hot/cold SM83 split.
 *
 * The candidate and reference builds emit one deterministic hash over the
 * complete state produced by the eight outlined accumulator/flag opcodes.
 * Keeping expected results in the reference decoder avoids duplicating DAA's
 * deferred-flag rules in a test oracle.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mi.h"
#include "microcode.h"

static r8 rom[0x8000];
static r8 wram[0x2000];
static r8 vram[0x2000];
static r8 sram[0x2000];
static r8 hram[0x100];
static r8 oam[0x100];

static uint64_t
hash_byte(uint64_t hash, uint8_t value)
{
    return (hash ^ value) * UINT64_C(1099511628211);
}

static uint64_t
hash_word(uint64_t hash, uint16_t value)
{
    hash = hash_byte(hash, (uint8_t)value);
    return hash_byte(hash, (uint8_t)(value >> 8));
}

static uint64_t
hash_u32(uint64_t hash, uint32_t value)
{
    hash = hash_word(hash, (uint16_t)value);
    return hash_word(hash, (uint16_t)(value >> 16));
}

static void
setup(mb_state *mb, struct mi_dispatch *mi, r8 opcode, r8 next_opcode)
{
    memset(mb, 0, sizeof(*mb));
    memset(mi, 0, sizeof(*mi));

    mi->ROM = rom;
    mi->WRAM = wram;
    mi->VRAM = vram;
    mi->SRAM = sram;
    mi->HRAM = hram;
    mi->OAM = oam;
    mi->BANK_ROM = 1U;
    mi->BANK_WRAM = 1U;
    mi->BANK_VRAM = 0U;
    mi->BANK_SRAM = 0U;
    mi->N_ROM = 2U;
    mi->N_SRAM = 1U;

    mb->mi = mi;
    mb->PC = 0x0201U; /* Opcode itself was already prefetched. */
    mb->IR.low = opcode;
    rom[0x0201U] = next_opcode;
    micache_invalidate(&mb->micache);
}

static uint64_t
hash_state(uint64_t hash, const mb_state *mb, word cycles)
{
    hash = hash_word(hash, mb->reg.BC);
    hash = hash_word(hash, mb->reg.DE);
    hash = hash_word(hash, mb->reg.HL);
    hash = hash_word(hash, mb->reg.FA);
    hash = hash_word(hash, mb->IR.raw);
    hash = hash_word(hash, mb->IMM.raw);
    hash = hash_word(hash, mb->PC);
    hash = hash_word(hash, mb->SP);
    hash = hash_byte(hash, mb->IF);
    hash = hash_byte(hash, mb->IE);
    hash = hash_byte(hash, mb->IME);
    hash = hash_byte(hash, mb->IME_ASK);
    hash = hash_u32(hash, mb->DIV);
    hash = hash_u32(hash, mb->HALTING);
    hash = hash_word(hash, mb->FR1);
    hash = hash_word(hash, mb->FR2);
    hash = hash_u32(hash, mb->FMC_MODE);
    return hash_u32(hash, cycles);
}

int
main(void)
{
    static const r8 opcodes[] = {
        0x07U, 0x0FU, 0x17U, 0x1FU,
        0x27U, 0x2FU, 0x37U, 0x3FU
    };
    uint64_t hash = UINT64_C(14695981039346656037);
    unsigned int cases = 0U;
    unsigned int opcode_index;
    unsigned int a;
    unsigned int flags;
    unsigned int mode;

    memset(rom, 0, sizeof(rom));
    memset(wram, 0, sizeof(wram));
    memset(vram, 0, sizeof(vram));
    memset(sram, 0, sizeof(sram));
    memset(hram, 0, sizeof(hram));
    memset(oam, 0, sizeof(oam));

    for (opcode_index = 0U;
         opcode_index < sizeof(opcodes) / sizeof(opcodes[0]);
         ++opcode_index)
    {
        for (a = 0U; a < 0x100U; ++a)
        {
            for (flags = 0U; flags < 0x10U; ++flags)
            {
                for (mode = 0U; mode < 8U; ++mode)
                {
                    mb_state mb;
                    struct mi_dispatch mi;
                    const r8 next_opcode = (r8)(0xA5U ^ a ^ flags ^ mode);
                    word cycles;

                    setup(&mb, &mi, opcodes[opcode_index], next_opcode);
                    mb.reg.A = (r8)a;
                    mb.reg.F = (r8)(flags << 4);
                    mb.FR1 = (r16)(0x1357U ^ (a << 3) ^ mode);
                    mb.FR2 = (r16)(0x2468U ^ (flags << 8) ^ a);
                    mb.FMC_MODE = mode;

                    cycles = mb_exec(&mb);
                    if (cycles != 1U || mb.PC != 0x0202U ||
                        mb.IR.low != next_opcode || mb.IR.high != 0U)
                    {
                        fprintf(stderr,
                            "bad tail op=%02X a=%02X f=%02X mode=%u "
                            "cycles=%u pc=%04X ir=%04X\n",
                            opcodes[opcode_index], a, flags << 4, mode,
                            cycles, mb.PC, mb.IR.raw);
                        return 1;
                    }

                    hash = hash_state(hash, &mb, cycles);
                    ++cases;
                }
            }
        }
    }

    /* The IRQ prologue must still win before the outlined opcode executes. */
    {
        mb_state mb;
        struct mi_dispatch mi;
        word cycles;

        setup(&mb, &mi, 0x27U, 0x00U);
        mb.reg.A = 0x9AU;
        mb.reg.F = MB_FLAG_C;
        mb.IME = 1U;
        mb.IE = 0x01U;
        mb.IF = 0x01U;
        mb.SP = 0xFFFEU;
        rom[0x0040U] = 0x00U;
        cycles = mb_exec(&mb);
        if (cycles != 5U || mb.reg.A != 0x9AU || mb.PC != 0x0041U ||
            mb.SP != 0xFFFCU || mb.IF != 0U || mb.IME != 0U)
        {
            fputs("IRQ prologue mismatch\n", stderr);
            return 2;
        }
        hash = hash_state(hash, &mb, cycles);
        ++cases;
    }

    printf("picogb_hot_cold_contract: cases=%u hash=%016llX\n", cases,
        (unsigned long long)hash);
    return 0;
}
