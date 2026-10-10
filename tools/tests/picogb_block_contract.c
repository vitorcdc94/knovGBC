/*
 * Contract test for the conservative ROM block cache.
 *
 * It compares every block against the reference mb_exec() instruction by
 * instruction.  The host cannot execute SH-2 code, but it validates the
 * decoder, prefetch convention, per-opcode cycle accounting, lazy flags and
 * fallback boundaries before a Saturn image is produced.
 */

#include <stdio.h>
#include <string.h>

#include "mi.h"
#include "microcode.h"
#include "picogb_block_cache.h"

static r8 rom[0x8000];
static r8 wram[0x2000];
static r8 vram[0x2000];
static r8 sram[0x2000];
static r8 hram[0x100];
static r8 oam[0x100];

static void
commit_cycles(void *userdata, word cycles)
{
    word *const total = userdata;

    *total += cycles;
}

static void
setup(struct mb_state *mb, struct mi_dispatch *mi)
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
    mi->N_ROM = 2U;
    mi->N_SRAM = 1U;
    mi->ROM_MAPPER = 0U;

    mb->mi = mi;
    mb->PC = 0x0101U;
    mb->SP = 0xFFFEU;
    mb->reg.A = 0xFEU;
    mb->reg.B = 0x01U;
    mb->reg.C = 0xFFU;
    mb->reg.F = MB_FLAG_C;
    mb->IR.low = rom[0x0100U];
    micache_invalidate(&mb->micache);
}

static int
state_equal(const struct mb_state *left, const struct mb_state *right)
{
    return (left->reg.BC == right->reg.BC) &&
        (left->reg.DE == right->reg.DE) &&
        (left->reg.HL == right->reg.HL) &&
        (left->reg.FA == right->reg.FA) &&
        (left->IR.raw == right->IR.raw) && (left->IMM.raw == right->IMM.raw) &&
        (left->PC == right->PC) && (left->SP == right->SP) &&
        (left->IF == right->IF) && (left->IE == right->IE) &&
        (left->IME == right->IME) && (left->IME_ASK == right->IME_ASK) &&
        (left->DIV == right->DIV) && (left->HALTING == right->HALTING) &&
        (left->FR1 == right->FR1) && (left->FR2 == right->FR2) &&
        (left->FMC_MODE == right->FMC_MODE);
}

static int
compare_one_block(struct picogb_block_cache *cache,
    struct mb_state *candidate, struct mb_state *reference)
{
    struct picogb_block_runtime runtime;
    word candidate_cycles = 0U;
    word reference_cycles = 0U;
    unsigned int index;

    runtime.userdata = &candidate_cycles;
    runtime.commit = commit_cycles;
    runtime.instructions = 0U;
    runtime.cycles = 0U;

    if (!picogb_block_cache_try_execute(cache, candidate, &runtime,
            0xFFFFFFFFU)) {
        return 0;
    }
    for (index = 0U; index < runtime.instructions; ++index) {
        const word cycles = mb_exec(reference);

        if (cycles == 0U) {
            return -1;
        }
        reference_cycles += cycles;
    }

    if ((runtime.cycles != reference_cycles) ||
        (candidate_cycles != reference_cycles) ||
        !state_equal(candidate, reference)) {
        return -1;
    }
    return 1;
}

int
main(void)
{
    struct picogb_block_cache cache;
    struct mb_state candidate;
    struct mb_state reference;
    struct mi_dispatch candidate_mi;
    struct mi_dispatch reference_mi;
    struct picogb_block_runtime runtime;
    word ignored_cycles = 0U;

    memset(rom, 0xC3, sizeof(rom));
    memset(wram, 0, sizeof(wram));
    memset(vram, 0, sizeof(vram));
    memset(sram, 0, sizeof(sram));
    memset(hram, 0, sizeof(hram));
    memset(oam, 0, sizeof(oam));

    /* Four generic register/immediate operations, then a second block with
     * HRAM-only F0 and NOP.  C3 is an explicit unsupported terminal. */
    rom[0x0100U] = 0x06U; /* LD B,$12 */
    rom[0x0101U] = 0x12U;
    rom[0x0102U] = 0x0CU; /* INC C */
    rom[0x0103U] = 0x41U; /* LD B,C */
    rom[0x0104U] = 0x80U; /* ADD A,B */
    rom[0x0105U] = 0xF0U; /* LDH A,($80) */
    rom[0x0106U] = 0x80U;
    rom[0x0107U] = 0x00U; /* NOP */
    rom[0x0108U] = 0xC3U; /* unsupported JP */
    hram[0x80U] = 0xA6U;

    setup(&candidate, &candidate_mi);
    setup(&reference, &reference_mi);
    picogb_block_cache_init(&cache);

    if (compare_one_block(&cache, &candidate, &reference) != 1 ||
        compare_one_block(&cache, &candidate, &reference) != 1) {
        fprintf(stderr, "block semantic mismatch\n");
        return 1;
    }

    /* An unsupported control-flow opcode must leave every observable state
     * untouched so the caller can execute it through mb_exec(). */
    runtime.userdata = &ignored_cycles;
    runtime.commit = commit_cycles;
    runtime.instructions = 0U;
    runtime.cycles = 0U;
    if (picogb_block_cache_try_execute(&cache, &candidate, &runtime,
            0xFFFFFFFFU) || (runtime.instructions != 0U) ||
        !state_equal(&candidate, &reference)) {
        fprintf(stderr, "block fallback boundary mismatch\n");
        return 2;
    }

    /* Only immutable MBC0 ROM can enter the first cache generation. */
    candidate_mi.ROM_MAPPER = 1U;
    if (picogb_block_cache_try_execute(&cache, &candidate, &runtime,
            0xFFFFFFFFU)) {
        fprintf(stderr, "block mapper boundary mismatch\n");
        return 3;
    }

    puts("picogb_block_contract: PASS");
    return 0;
}
