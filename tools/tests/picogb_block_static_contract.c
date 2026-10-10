/*
 * Contract test for the portable C basic-block executor.
 *
 * The candidate is checked against mb_exec() after every operation it says it
 * executed.  It deliberately covers the complete accepted opcode subset,
 * lazy-flag state, cache reuse/collision and every rejection guard.  The
 * real PPU/timer integration is exercised by picogb_block_static_differential
 * through picogb_smoke.c; this file verifies the CPU/pre-fetch contract in a
 * small deterministic host test.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mi.h"
#include "microcode.h"
#include "picogb_block_executor.h"

enum { PROGRAM_PC = 0x0200U };

static r8 rom[0x8000];
static r8 other_rom[0x8000];
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
setup(struct mb_state *mb, struct mi_dispatch *mi, r8 *program,
    r8 opcode, unsigned variant)
{
    memset(mb, 0, sizeof(*mb));
    memset(mi, 0, sizeof(*mi));

    mi->ROM = program;
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
    mb->PC = PROGRAM_PC + 1U;
    mb->SP = (r16)(0xFFFEU - variant);
    mb->reg.A = (r8)(0xF0U ^ (variant * 0x35U));
    mb->reg.B = (r8)(0x01U + variant);
    mb->reg.C = (r8)(0xFFU - variant);
    mb->reg.D = (r8)(0x80U ^ (variant * 0x11U));
    mb->reg.E = (r8)(0x10U + variant);
    mb->reg.H = (r8)(0x7FU - variant);
    mb->reg.L = (r8)(0xF0U + variant);
    mb->reg.F = (r8)((variant & 1U) ? MB_FLAG_C : MB_FLAG_Z);
    mb->FR1 = (r16)(0x1357U ^ (variant * 0x1111U));
    mb->FR2 = (r16)(0x2468U ^ (variant * 0x0101U));
    mb->FMC_MODE = variant & 7U;
    mb->DIV = 0x26E168U + variant;
    mb->IR.raw = opcode;
    micache_invalidate(&mb->micache);
}

static int
state_equal(const struct mb_state *left, const struct mb_state *right)
{
    return memcmp(&left->reg, &right->reg, sizeof(left->reg)) == 0 &&
        (left->IR.raw == right->IR.raw) &&
        (left->IMM.raw == right->IMM.raw) && (left->PC == right->PC) &&
        (left->SP == right->SP) && (left->IF == right->IF) &&
        (left->IE == right->IE) && (left->IME == right->IME) &&
        (left->IME_ASK == right->IME_ASK) && (left->DIV == right->DIV) &&
        (left->HALTING == right->HALTING) && (left->FR1 == right->FR1) &&
        (left->FR2 == right->FR2) && (left->FMC_MODE == right->FMC_MODE);
}

static int
opcode_is_eligible(r8 opcode)
{
    if (opcode == 0x00U) {
        return 1;
    }
    if ((opcode < 0x40U) && ((opcode & 0x0FU) == 0x01U)) {
        return 1;
    }
    if ((opcode < 0x40U) && ((opcode & 0x0FU) == 0x09U)) {
        return 1;
    }
    if ((opcode < 0x40U) && (((opcode & 0x0FU) == 0x03U) ||
        ((opcode & 0x0FU) == 0x0BU))) {
        return 1;
    }
    if ((((opcode & 0xC7U) == 0x04U) || ((opcode & 0xC7U) == 0x05U)) &&
        (((opcode >> 3) & 7U) != 6U)) {
        return 1;
    }
    if (((opcode & 0xC7U) == 0x06U) && (((opcode >> 3) & 7U) != 6U)) {
        return 1;
    }
    if ((opcode >= 0x40U) && (opcode < 0x80U) && (opcode != 0x76U) &&
        ((opcode & 7U) != 6U) && (((opcode >> 3) & 7U) != 6U)) {
        return 1;
    }
    if ((opcode >= 0x80U) && (opcode < 0xC0U) && ((opcode & 7U) != 6U)) {
        return 1;
    }
    return opcode == 0xF0U;
}

static unsigned int
opcode_bytes(r8 opcode)
{
    if (((opcode < 0x40U) && ((opcode & 0x0FU) == 0x01U))) {
        return 3U;
    }
    if (((opcode & 0xC7U) == 0x06U) || (opcode == 0xF0U)) {
        return 2U;
    }
    return 1U;
}

static void
prepare_program(r8 *program, r8 opcode)
{
    const unsigned int bytes = opcode_bytes(opcode);

    memset(program, 0xC3, 0x8000U);
    program[PROGRAM_PC] = opcode;
    if (bytes >= 2U) {
        program[PROGRAM_PC + 1U] = (opcode == 0xF0U) ? 0x80U : 0x5AU;
    }
    if (bytes == 3U) {
        program[PROGRAM_PC + 2U] = 0xA5U;
    }
    program[PROGRAM_PC + bytes] = 0x00U;
    program[PROGRAM_PC + bytes + 1U] = 0x00U;
    program[PROGRAM_PC + bytes + 2U] = 0x00U;
    program[PROGRAM_PC + bytes + 3U] = 0xC3U;
}

static int
compare_eligible_opcode(r8 opcode, unsigned variant)
{
    struct picogb_block_executor_cache cache;
    struct picogb_block_executor_runtime runtime;
    struct mb_state candidate;
    struct mb_state reference;
    struct mi_dispatch candidate_mi;
    struct mi_dispatch reference_mi;
    r8 wram_before[sizeof(wram)];
    r8 vram_before[sizeof(vram)];
    r8 hram_before[sizeof(hram)];
    r8 oam_before[sizeof(oam)];
    word committed_cycles = 0U;
    word reference_cycles = 0U;
    unsigned int index;
    const int expected = opcode_is_eligible(opcode);
    int executed;

    prepare_program(rom, opcode);
    hram[0x80U] = 0xA6U;
    memcpy(wram_before, wram, sizeof(wram));
    memcpy(vram_before, vram, sizeof(vram));
    memcpy(hram_before, hram, sizeof(hram));
    memcpy(oam_before, oam, sizeof(oam));

    setup(&candidate, &candidate_mi, rom, opcode, variant);
    setup(&reference, &reference_mi, rom, opcode, variant);
    picogb_block_executor_cache_init(&cache);
    runtime.userdata = &committed_cycles;
    runtime.commit = commit_cycles;
    runtime.instructions = 0xFFFFU;
    runtime.cycles = 0xFFFFU;

    executed = picogb_block_executor_try_execute(&cache, &candidate, &runtime,
        UINT32_MAX);
    if (!!executed != !!expected) {
        fprintf(stderr, "opcode admission mismatch: %02X variant %u\n",
            opcode, variant);
        return 0;
    }

    if (!executed) {
        if (!state_equal(&candidate, &reference) ||
            (runtime.instructions != 0xFFFFU) || (runtime.cycles != 0xFFFFU) ||
            (committed_cycles != 0U)) {
            fprintf(stderr, "fallback mutated state: %02X variant %u\n",
                opcode, variant);
            return 0;
        }
        return 1;
    }

    if (runtime.instructions != 4U) {
        fprintf(stderr, "unexpected block length: %02X got %u\n", opcode,
            (unsigned int)runtime.instructions);
        return 0;
    }

    for (index = 0U; index < runtime.instructions; ++index) {
        const word cycles = mb_exec(&reference);

        if (cycles == 0U) {
            fprintf(stderr, "reference stopped: %02X at operation %u\n",
                opcode, index);
            return 0;
        }
        reference_cycles += cycles;
    }

    if ((runtime.cycles != reference_cycles) ||
        (committed_cycles != reference_cycles) ||
        !state_equal(&candidate, &reference) ||
        (memcmp(wram_before, wram, sizeof(wram)) != 0) ||
        (memcmp(vram_before, vram, sizeof(vram)) != 0) ||
        (memcmp(hram_before, hram, sizeof(hram)) != 0) ||
        (memcmp(oam_before, oam, sizeof(oam)) != 0)) {
        fprintf(stderr, "semantic mismatch: %02X variant %u\n", opcode,
            variant);
        return 0;
    }

    return 1;
}

static int
rejected_without_mutation(struct picogb_block_executor_cache *cache,
    struct mb_state *candidate, uint32_t safe_ppu_ticks)
{
    struct picogb_block_executor_runtime runtime;
    const struct mb_state before = *candidate;
    word committed_cycles = 0U;

    runtime.userdata = &committed_cycles;
    runtime.commit = commit_cycles;
    runtime.instructions = 0xFFFFU;
    runtime.cycles = 0xFFFFU;

    if (picogb_block_executor_try_execute(cache, candidate, &runtime,
            safe_ppu_ticks) || !state_equal(candidate, &before) ||
        (runtime.instructions != 0xFFFFU) || (runtime.cycles != 0xFFFFU) ||
        (committed_cycles != 0U)) {
        return 0;
    }
    return 1;
}

static int
test_guards_and_deadline(void)
{
    struct picogb_block_executor_cache cache;
    struct mb_state candidate;
    struct mi_dispatch mi;
    unsigned int guard;

    prepare_program(rom, 0x00U); /* Four NOPs = four M-cycles. */
    for (guard = 0U; guard < 7U; ++guard) {
        setup(&candidate, &mi, rom, 0x00U, guard);
        picogb_block_executor_cache_init(&cache);
        switch (guard) {
        case 0U: candidate.HALTING = 1U; break;
        case 1U: candidate.IR.high = 1U; break;
        case 2U:
            candidate.IME = 1U;
#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
            candidate.IE = candidate.IF = 1U;
#endif
            break;
        case 3U: candidate.IME_ASK = 1U; break;
        case 4U: candidate.mi->ROM_MAPPER = 1U; break;
        case 5U: candidate.mi->N_ROM = 4U; break;
        case 6U: candidate.mi->BANK_ROM = 2U; break;
        default: break;
        }
        if (!rejected_without_mutation(&cache, &candidate, UINT32_MAX)) {
            fprintf(stderr, "guard fallback mismatch: %u\n", guard);
            return 0;
        }
    }

    setup(&candidate, &mi, rom, 0x00U, 0U);
    picogb_block_executor_cache_init(&cache);
    if (!rejected_without_mutation(&cache, &candidate, 16U)) {
        fputs("PPU deadline equality mismatch\n", stderr);
        return 0;
    }

    /* One PPU tick beyond the exact endpoint is safe. */
    setup(&candidate, &mi, rom, 0x00U, 0U);
    {
        struct picogb_block_executor_runtime runtime;
        word committed_cycles = 0U;

        runtime.userdata = &committed_cycles;
        runtime.commit = commit_cycles;
        runtime.instructions = 0U;
        runtime.cycles = 0U;
        picogb_block_executor_cache_init(&cache);
        if (!picogb_block_executor_try_execute(&cache, &candidate, &runtime,
                17U) || (runtime.instructions != 4U) ||
            (runtime.cycles != 4U) || (committed_cycles != 4U)) {
            fputs("PPU deadline acceptance mismatch\n", stderr);
            return 0;
        }
    }

    /* A pending IRQ is still ordinary code while IME is disabled. */
    setup(&candidate, &mi, rom, 0x00U, 0U);
    candidate.IE = 1U;
    candidate.IF = 1U;
    {
        struct picogb_block_executor_runtime runtime;
        word committed_cycles = 0U;

        runtime.userdata = &committed_cycles;
        runtime.commit = commit_cycles;
        runtime.instructions = 0U;
        runtime.cycles = 0U;
        picogb_block_executor_cache_init(&cache);
        if (!picogb_block_executor_try_execute(&cache, &candidate, &runtime,
                UINT32_MAX)) {
            fputs("IME-disabled IRQ admission mismatch\n", stderr);
            return 0;
        }
    }

    return 1;
}

#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
static int
test_immutable_ime_window(void)
{
    struct picogb_block_executor_cache cache;
    struct picogb_block_executor_runtime runtime;
    struct mb_state candidate;
    struct mb_state reference;
    struct mi_dispatch candidate_mi;
    struct mi_dispatch reference_mi;
    word committed_cycles, reference_cycles;
    unsigned int index, mask;

    /* Eight NOPs fill the immutable-window block. The next byte is a
     * branch, so the boundary and prefetched IR are unambiguous. */
    prepare_program(rom, 0x00U);
    for (index = 0U; index < 8U; ++index) {
        rom[PROGRAM_PC + index] = 0x00U;
    }
    rom[PROGRAM_PC + 8U] = 0xC3U;
    rom[0x40U] = 0x00U; /* IRQ handler's prefetched opcode. */
    picogb_block_executor_cache_init(&cache);

    for (mask = 0U; mask < 2U; ++mask) {
        setup(&candidate, &candidate_mi, rom, 0x00U, 0U);
        setup(&reference, &reference_mi, rom, 0x00U, 0U);
        candidate.IME = reference.IME = 1U;
        candidate.IME_ASK = reference.IME_ASK = 1U;
        candidate.IE = reference.IE = mask ? 0U : 1U;
        candidate.IF = reference.IF = mask ? 1U : 0U;
        committed_cycles = reference_cycles = 0U;
        runtime.userdata = &committed_cycles;
        runtime.commit = commit_cycles;
        runtime.instructions = runtime.cycles = 0xFFFFU;
        if (!picogb_block_executor_try_execute(&cache, &candidate,
                &runtime, 33U) || runtime.instructions != 8U ||
            runtime.cycles != 8U || committed_cycles != 8U) {
            fprintf(stderr, "IME/IME_ASK admission mismatch: mask=%u\n",
                mask);
            return 0;
        }
        for (index = 0U; index < 8U; ++index) {
            const word cycles = mb_exec(&reference);
            if (cycles != 1U || reference.IME != 1U ||
                reference.IME_ASK != 1U) {
                fprintf(stderr, "IME/IME_ASK oracle mismatch: mask=%u step=%u\n",
                    mask, index);
                return 0;
            }
            reference_cycles += cycles;
        }
        if (reference_cycles != committed_cycles ||
            !state_equal(&candidate, &reference) ||
            candidate.IME_ASK != 1U) {
            fprintf(stderr, "IME/IME_ASK state mismatch: mask=%u\n", mask);
            return 0;
        }
    }

    /* Reuse the warmed cache to check the strict PPU boundary and the IRQ
     * guard. Neither rejection may commit time or alter CPU state. */
    setup(&candidate, &candidate_mi, rom, 0x00U, 0U);
    candidate.IME = candidate.IME_ASK = 1U;
    candidate.IE = 1U;
    if (!rejected_without_mutation(&cache, &candidate, 32U)) {
        fputs("eight-NOP PPU deadline equality mismatch\n", stderr);
        return 0;
    }

    memset(hram, 0, sizeof(hram));
    candidate.IF = 1U;
    if (!rejected_without_mutation(&cache, &candidate, 33U) ||
        hram[0xFCU] != 0U || hram[0xFDU] != 0U) {
        fputs("IME/IME_ASK pending IRQ admission mismatch\n", stderr);
        return 0;
    }
    if (mb_exec(&candidate) != 5U || candidate.PC != 0x0041U ||
        candidate.SP != 0xFFFCU || candidate.IME != 0U ||
        candidate.IME_ASK != 0U || candidate.IF != 0U ||
        hram[0xFCU] != 0x00U || hram[0xFDU] != 0x02U) {
        fputs("IME/IME_ASK pending IRQ fallback mismatch\n", stderr);
        return 0;
    }

    /* With IME off, EI's pending promotion is owned by one mb_exec step;
     * the interrupt may run only on the following step. */
    memset(hram, 0, sizeof(hram));
    setup(&candidate, &candidate_mi, rom, 0x00U, 0U);
    candidate.IME_ASK = candidate.IE = candidate.IF = 1U;
    if (!rejected_without_mutation(&cache, &candidate, 33U) ||
        hram[0xFCU] != 0U || hram[0xFDU] != 0U ||
        mb_exec(&candidate) != 1U || candidate.PC != 0x0202U ||
        candidate.IME != 1U || candidate.IME_ASK != 0U ||
        candidate.IF != 1U || mb_exec(&candidate) != 5U ||
        candidate.PC != 0x0041U || candidate.IME != 0U ||
        candidate.IME_ASK != 0U || candidate.IF != 0U ||
        hram[0xFCU] != 0x01U || hram[0xFDU] != 0x02U) {
        fputs("delayed EI fallback mismatch\n", stderr);
        return 0;
    }

    return 1;
}
#endif

static int
test_f0_boundaries(void)
{
    static const r8 operands[] = { 0x7FU, 0x80U, 0xFEU, 0xFFU };
    unsigned int index;

    for (index = 0U; index < sizeof(operands); ++index) {
        struct picogb_block_executor_cache cache;
        struct picogb_block_executor_runtime runtime;
        struct mb_state candidate;
        struct mb_state before;
        struct mi_dispatch mi;
        word committed_cycles = 0U;
        int executed;

        prepare_program(rom, 0xF0U);
        rom[PROGRAM_PC + 1U] = operands[index];
        hram[0x80U] = 0x3CU;
        setup(&candidate, &mi, rom, 0xF0U, 0U);
        before = candidate;

        picogb_block_executor_cache_init(&cache);
        runtime.userdata = &committed_cycles;
        runtime.commit = commit_cycles;
        runtime.instructions = 0xFFFFU;
        runtime.cycles = 0xFFFFU;
        executed = picogb_block_executor_try_execute(&cache, &candidate,
            &runtime, UINT32_MAX);

        if (((operands[index] >= 0x80U) && (operands[index] < 0xFFU)) !=
            !!executed) {
            fprintf(stderr, "F0 admission mismatch: %02X\n", operands[index]);
            return 0;
        }
        if (!executed && (!state_equal(&candidate, &before) ||
            (runtime.instructions != 0xFFFFU) || (runtime.cycles != 0xFFFFU))) {
            fprintf(stderr, "F0 fallback mutation: %02X\n", operands[index]);
            return 0;
        }
    }
    return 1;
}

static int
test_cache_reuse_and_collision(void)
{
    struct picogb_block_executor_cache cache;
    struct picogb_block_executor_runtime runtime;
    struct mb_state candidate;
    struct mb_state reference;
    struct mi_dispatch candidate_mi;
    struct mi_dispatch reference_mi;
    word committed_cycles = 0U;
    unsigned int index;

    memset(rom, 0xC3, sizeof(rom));
    memset(other_rom, 0xC3, sizeof(other_rom));
    rom[PROGRAM_PC] = 0x00U;
    rom[PROGRAM_PC + 1U] = 0x00U;
    rom[PROGRAM_PC + 2U] = 0x00U;
    rom[PROGRAM_PC + 3U] = 0x00U;
    other_rom[PROGRAM_PC] = 0x00U;
    other_rom[PROGRAM_PC + 1U] = 0x06U;
    other_rom[PROGRAM_PC + 2U] = 0x91U;
    other_rom[PROGRAM_PC + 3U] = 0x00U;
    other_rom[PROGRAM_PC + 4U] = 0x00U;

    picogb_block_executor_cache_init(&cache);
    setup(&candidate, &candidate_mi, rom, 0x00U, 0U);
    runtime.userdata = &committed_cycles;
    runtime.commit = commit_cycles;
    runtime.instructions = 0U;
    runtime.cycles = 0U;
    if (!picogb_block_executor_try_execute(&cache, &candidate, &runtime,
            UINT32_MAX)) {
        fputs("first cache build failed\n", stderr);
        return 0;
    }

    setup(&candidate, &candidate_mi, other_rom, 0x00U, 0U);
    setup(&reference, &reference_mi, other_rom, 0x00U, 0U);
    committed_cycles = 0U;
    runtime.instructions = 0U;
    runtime.cycles = 0U;
    if (!picogb_block_executor_try_execute(&cache, &candidate, &runtime,
            UINT32_MAX)) {
        fputs("ROM-pointer cache replacement failed\n", stderr);
        return 0;
    }
    for (index = 0U; index < runtime.instructions; ++index) {
        (void)mb_exec(&reference);
    }
    if (!state_equal(&candidate, &reference) || (candidate.reg.B != 0x91U)) {
        fputs("ROM-pointer cache replacement mismatch\n", stderr);
        return 0;
    }

    /* 0x0200 and 0x0240 map to the same direct-cache slot. */
    rom[0x0240U] = 0x00U;
    rom[0x0241U] = 0x06U;
    rom[0x0242U] = 0x77U;
    rom[0x0243U] = 0x00U;
    rom[0x0244U] = 0x00U;
    setup(&candidate, &candidate_mi, rom, 0x00U, 0U);
    setup(&reference, &reference_mi, rom, 0x00U, 0U);
    candidate.PC = 0x0241U;
    reference.PC = 0x0241U;
    committed_cycles = 0U;
    runtime.instructions = 0U;
    runtime.cycles = 0U;
    if (!picogb_block_executor_try_execute(&cache, &candidate, &runtime,
            UINT32_MAX)) {
        fputs("cache collision replacement failed\n", stderr);
        return 0;
    }
    for (index = 0U; index < runtime.instructions; ++index) {
        (void)mb_exec(&reference);
    }
    if (!state_equal(&candidate, &reference) || (candidate.reg.B != 0x77U)) {
        fputs("cache collision replacement mismatch\n", stderr);
        return 0;
    }

    return 1;
}

int
main(void)
{
    unsigned int opcode;
    unsigned int variant;

    memset(wram, 0, sizeof(wram));
    memset(vram, 0, sizeof(vram));
    memset(sram, 0, sizeof(sram));
    memset(hram, 0, sizeof(hram));
    memset(oam, 0, sizeof(oam));

    for (opcode = 0U; opcode <= 0xFFU; ++opcode) {
        for (variant = 0U; variant < 8U; ++variant) {
            if (!compare_eligible_opcode((r8)opcode, variant)) {
                return 1;
            }
        }
    }
    if (!test_guards_and_deadline() || !test_f0_boundaries() ||
        !test_cache_reuse_and_collision()) {
        return 2;
    }
#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
    if (!test_immutable_ime_window()) {
        return 2;
    }
#endif

    puts("picogb_block_static_contract: PASS");
    return 0;
}
