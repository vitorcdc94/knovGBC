/*
 * Host contract for the deliberately narrow F0(a8)-A7-JR Z idle shortcut.
 * A successful call must be exactly equivalent to N complete trips through
 * mb_exec(), stopping before a PPU/timer event or an interrupt can be seen.
 */
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

static int
expect_rejected(const char *name, struct fixture *fixture)
{
    const mb_state before = fixture->cpu;
    const ppu_t ppu_before = fixture->ppu;
    const struct pgf_userdata_t fabric_before = fixture->fabric;
    word iterations;

    snapshot_memory();
    iterations = picogb_idle_spin_try(&fixture->cpu, &fixture->ppu,
        &fixture->fabric);
    if (iterations != 0U || !same_cpu(&before, &fixture->cpu) ||
        memcmp(&ppu_before, &fixture->ppu, sizeof(ppu_before)) != 0 ||
        memcmp(&fabric_before, &fixture->fabric,
            sizeof(fabric_before)) != 0 || !memory_unchanged()) {
        fprintf(stderr, "%s: rejection changed state or returned %u\n", name,
            (unsigned)iterations);
        return 0;
    }
    return 1;
}

static int
expect_equivalent(const char *name, struct fixture *fixture)
{
    mb_state reference = fixture->cpu;
    const ppu_t ppu_before = fixture->ppu;
    const struct pgf_userdata_t fabric_before = fixture->fabric;
    word iterations;
    word cycles = 0U;
    word expected_iterations = (ppu_before.next_update_ticks - 1U) / 28U;
    word i;
    word j;

    snapshot_memory();
    iterations = picogb_idle_spin_try(&fixture->cpu, &fixture->ppu,
        &fixture->fabric);
    if (expected_iterations > 16U) expected_iterations = 16U;
    if (iterations == 0U || iterations != expected_iterations ||
        iterations * 28U >= ppu_before.next_update_ticks) {
        fprintf(stderr, "%s: invalid admitted iteration count %u\n", name,
            (unsigned)iterations);
        return 0;
    }

    for (i = 0U; i < iterations; ++i) {
        for (j = 0U; j < 3U; ++j) {
            const word step_cycles = mb_exec(&reference);

            if (step_cycles == 0U) {
                fprintf(stderr, "%s: reference stopped at trip %u step %u\n",
                    name, (unsigned)i, (unsigned)j);
                return 0;
            }
            cycles += step_cycles;
        }
        if (reference.PC != fixture->cpu.PC ||
            reference.IR.low != 0xF0U) {
            fprintf(stderr, "%s: reference did not return to loop head\n",
                name);
            return 0;
        }
    }

    if (cycles != iterations * 7U ||
        !same_cpu(&reference, &fixture->cpu) ||
        memcmp(&ppu_before, &fixture->ppu, sizeof(ppu_before)) != 0 ||
        memcmp(&fabric_before, &fixture->fabric,
            sizeof(fabric_before)) != 0 || !memory_unchanged()) {
        fprintf(stderr,
            "%s: mismatch after %u trips, %u reference M-cycles; pc=%04X/%04X ir=%04X/%04X af=%02X%02X/%02X%02X\n",
            name, (unsigned)iterations, (unsigned)cycles,
            fixture->cpu.PC, reference.PC,
            fixture->cpu.IR.raw, reference.IR.raw,
            fixture->cpu.reg.A, fixture->cpu.reg.F,
            reference.reg.A, reference.reg.F);
        return 0;
    }
    return 1;
}

#if defined(KNOVGBC_IDLE_SPIN_CACHE)
static int
cache_contract(void)
{
    struct fixture fixture;
    static const word ppu_ticks[] = { 29U, 56U, 57U, 448U, 449U, 2000U };
    word i;

    setup(&fixture, LOOP_PC, 1U, 0x85U);
    if (!expect_equivalent("cold recognition", &fixture) ||
        !expect_equivalent("warm recognition", &fixture)) return 0;
#if defined(KNOVGBC_IDLE_SPIN_CACHE_STATS)
    if (picogb_idle_spin_cache_hits == 0U ||
        picogb_idle_spin_cache_misses == 0U) {
        fputs("recognition cache was never populated and reused\n", stderr);
        return 0;
    }
#endif

    /* The cached signature must never cache a live HRAM value or pointer. */
    hram[0x85U] = 1U;
    if (!expect_rejected("warm HRAM changed", &fixture)) return 0;
    hram[0x85U] = 0U;
    memset(alternate_hram, 0U, sizeof(alternate_hram));
    alternate_hram[0x85U] = 1U;
    fixture.memory.HRAM = alternate_hram;
    if (!expect_rejected("warm HRAM pointer changed", &fixture)) return 0;
    alternate_hram[0x85U] = 0U;
    if (!expect_equivalent("warm alternate HRAM zero", &fixture)) return 0;
    fixture.memory.HRAM = NULL;
    if (!expect_rejected("warm missing HRAM", &fixture)) return 0;
    fixture.memory.HRAM = hram;

    /* All runtime guards still apply after recognition has been cached. */
    fixture.cpu.IF = fixture.cpu.IE;
    if (!expect_rejected("warm pending interrupt", &fixture)) return 0;
    fixture.cpu.IF = 0U;
    fixture.cpu.IME = 0U;
    fixture.cpu.IME_ASK = 1U;
    if (!expect_rejected("warm delayed interrupt enable", &fixture)) return 0;
    fixture.cpu.IME = 1U;
    fixture.cpu.IME_ASK = 0U;
    fixture.cpu.HALTING = 1U;
    if (!expect_rejected("warm HALT", &fixture)) return 0;
    fixture.cpu.HALTING = 0U;
    fixture.cpu.IR.high = 1U;
    if (!expect_rejected("warm IR high", &fixture)) return 0;
    fixture.cpu.IR.high = 0U;
    fixture.cpu.IR.low = 0xA7U;
    if (!expect_rejected("warm wrong opcode", &fixture)) return 0;
    fixture.cpu.IR.low = 0xF0U;
    fixture.fabric.TIMER_CNT = 4U;
    if (!expect_rejected("warm enabled timer", &fixture)) return 0;
    fixture.fabric.TIMER_CNT = 0U;
    fixture.ppu.rLCDC &= (r8)~0x80U;
    if (!expect_rejected("warm LCD disabled", &fixture)) return 0;
    fixture.ppu.rLCDC |= 0x80U;
    fixture.ppu._redrawed = 1U;
    if (!expect_rejected("warm pending redraw", &fixture)) return 0;
    fixture.ppu._redrawed = 0U;
    fixture.ppu.next_update_ticks = 28U;
    if (!expect_rejected("warm exact PPU event boundary", &fixture)) return 0;
    for (i = 0U; i < sizeof(ppu_ticks) / sizeof(ppu_ticks[0]); ++i) {
        fixture.ppu.next_update_ticks = ppu_ticks[i];
        if (!expect_equivalent("warm PPU interval/cap", &fixture)) return 0;
    }

    /* Same PC in a different ROM must use that ROM's signature. */
    memset(alternate_rom, 0U, sizeof(alternate_rom));
    fixture.memory.ROM = alternate_rom;
    if (!expect_rejected("different ROM base has no loop", &fixture)) return 0;
    memcpy(alternate_rom, rom, sizeof(rom));
    alternate_rom[LOOP_PC + 1U] = 0x86U;
    hram[0x86U] = 1U;
    if (!expect_rejected("different ROM base has new operand", &fixture))
        return 0;
    hram[0x86U] = 0U;
    if (!expect_equivalent("different ROM base valid loop", &fixture)) return 0;
    fixture.memory.ROM = rom;
    fixture.cpu.PC = LOOP_PC + 0x11U;
    if (!expect_rejected("new PC without loop", &fixture)) return 0;
    put_loop(LOOP_PC + 0x10U, 1U, 0x87U);
    if (!expect_equivalent("new PC valid loop", &fixture)) return 0;
    fixture.cpu.PC = LOOP_PC + 1U;
    if (!expect_equivalent("old PC recognized again", &fixture)) return 0;

    setup(&fixture, 0x4000U, 2U, 0x85U);
    if (!expect_equivalent("warm bank 2", &fixture)) return 0;
    fixture.memory.N_ROM = 2U;
    if (!expect_rejected("warm bank no longer in range", &fixture)) return 0;
    fixture.memory.N_ROM = 4U;
    fixture.memory.BANK_ROM = 3U;
    if (!expect_rejected("same PC in bank 3 without loop", &fixture)) return 0;
    put_loop(0x4000U, 3U, 0x86U);
    hram[0x86U] = 1U;
    if (!expect_rejected("same PC new bank operand", &fixture)) return 0;
    hram[0x86U] = 0U;
    if (!expect_equivalent("bank 3 valid loop", &fixture)) return 0;
    fixture.memory.BANK_ROM = 2U;
    if (!expect_equivalent("bank 2 recognized again", &fixture)) return 0;

    /* Explicit reset is required when ROM bytes change at the same address. */
    setup(&fixture, LOOP_PC, 1U, 0x85U);
    if (!expect_equivalent("before in-place ROM reload", &fixture)) return 0;
    rom[LOOP_PC + 2U] = 0x00U;
    picogb_idle_spin_cache_reset();
#if defined(KNOVGBC_IDLE_SPIN_CACHE_STATS)
    if (picogb_idle_spin_cache_hits != 0U ||
        picogb_idle_spin_cache_misses != 0U) {
        fputs("cache reset did not reset host counters\n", stderr);
        return 0;
    }
#endif
    if (!expect_rejected("in-place ROM reload reset", &fixture)) return 0;
    return 1;
}
#endif

int
main(void)
{
    struct fixture fixture;

    setup(&fixture, LOOP_PC, 1U, 0x85U);
    if (!expect_equivalent("fixed-ROM HRAM FF85", &fixture)) return 1;
    hram[0x85U] = 1U;
    if (!expect_rejected("HRAM changed after admitted trip", &fixture))
        return 1;

    setup(&fixture, LOOP_PC, 1U, 0xFEU);
    if (!expect_equivalent("generic HRAM FFFE", &fixture)) return 1;

    setup(&fixture, LOOP_PC, 1U, 0x85U);
    rom[LOOP_PC + 2U] = 0x00U;
    if (!expect_rejected("changed AND opcode", &fixture)) return 1;
    setup(&fixture, LOOP_PC, 1U, 0x85U);
    rom[LOOP_PC + 3U] = 0x18U;
    if (!expect_rejected("changed JR opcode", &fixture)) return 1;
    setup(&fixture, LOOP_PC, 1U, 0x85U);
    rom[LOOP_PC + 4U] = 0xFCU;
    if (!expect_rejected("changed JR displacement", &fixture)) return 1;

    setup(&fixture, LOOP_PC, 1U, 0x7FU);
    if (!expect_rejected("F0 points into I/O", &fixture)) return 1;
    setup(&fixture, LOOP_PC, 1U, 0xFFU);
    if (!expect_rejected("F0 points at IE", &fixture)) return 1;
    setup(&fixture, LOOP_PC, 1U, 0x85U);
    fixture.cpu.PC = 0xC201U;
    if (!expect_rejected("loop body outside ROM", &fixture)) return 1;

    setup(&fixture, LOOP_PC, 1U, 0x85U);
    fixture.cpu.IF = fixture.cpu.IE = fixture.cpu.IME = 1U;
    if (!expect_rejected("interrupt pending", &fixture)) return 1;
    setup(&fixture, LOOP_PC, 1U, 0x85U);
    fixture.cpu.IME = 0U;
    fixture.cpu.IF = fixture.cpu.IE = 1U;
    if (!expect_equivalent("pending IRQ while IME off", &fixture))
        return 1;
    setup(&fixture, LOOP_PC, 1U, 0x85U);
    fixture.cpu.IME_ASK = 1U;
    if (!expect_equivalent("IME_ASK inert while IME on", &fixture))
        return 1;
    setup(&fixture, LOOP_PC, 1U, 0x85U);
    fixture.cpu.IME = 0U;
    fixture.cpu.IME_ASK = 1U;
    if (!expect_rejected("delayed IME enable", &fixture)) return 1;
    setup(&fixture, LOOP_PC, 1U, 0x85U);
    fixture.cpu.HALTING = 1U;
    if (!expect_rejected("HALT", &fixture)) return 1;
    setup(&fixture, LOOP_PC, 1U, 0x85U);
    fixture.cpu.IR.high = 1U;
    if (!expect_rejected("IR high word active", &fixture)) return 1;

    setup(&fixture, LOOP_PC, 1U, 0x85U);
    fixture.ppu.next_update_ticks = 27U;
    if (!expect_rejected("PPU event before first complete trip", &fixture))
        return 1;
    setup(&fixture, LOOP_PC, 1U, 0x85U);
    fixture.fabric.TIMER_CNT = 5U; /* Enabled, four M-cycles per increment. */
    fixture.fabric.TIMER_ACCUM = 255U;
    fixture.fabric.TIMER_SUB = 3U; /* Overflow in one M-cycle. */
    if (!expect_rejected("timer overflow before first trip", &fixture))
        return 1;
    setup(&fixture, LOOP_PC, 1U, 0x85U);
    fixture.fabric.TIMER_CNT = 4U; /* Even a distant overflow is excluded. */
    if (!expect_rejected("timer enabled", &fixture)) return 1;
    setup(&fixture, LOOP_PC, 1U, 0x85U);
    fixture.ppu.rLCDC &= (r8)~0x80U;
    if (!expect_rejected("LCD off", &fixture)) return 1;
    setup(&fixture, LOOP_PC, 1U, 0x85U);
    fixture.ppu._redrawed = 1U;
    if (!expect_rejected("unconsumed frame", &fixture)) return 1;

    setup(&fixture, 0x4000U, 2U, 0x85U);
    /* Even if a switchable ROM bank is not eligible, a bank switch must
     * never reuse an old signature from a different bank. */
    (void)picogb_idle_spin_try(&fixture.cpu, &fixture.ppu, &fixture.fabric);
    fixture.memory.BANK_ROM = 3U;
    if (!expect_rejected("ROM bank changed", &fixture)) return 1;
    setup(&fixture, 0x4000U, 2U, 0x85U);
    fixture.memory.BANK_ROM = fixture.memory.N_ROM;
    if (!expect_rejected("ROM bank out of range", &fixture)) return 1;

#if defined(KNOVGBC_IDLE_SPIN_CACHE)
    if (!cache_contract()) return 1;
#endif
    puts("idle_spin_contract: PASS");
    return 0;
}
