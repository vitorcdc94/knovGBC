/* Independent instruction-by-instruction oracle for the event-boundary path.
 * Its fixture mirrors the original idle contract, without importing tests
 * of the mutually exclusive older shortcut symbol. */
#include "picogb_idle_event_fixture.h"

static pixel_t event_pixels[144][192];
static pixel_t event_expected_pixels[144][192];
static pixel_t event_initial_pixels[144][192];
static pixel_t *event_lines[144];
static unsigned event_cases;

static void
event_setup(struct fixture *fixture, word ticks, word event, word scanline,
    word stat, word pc, word bank, r8 operand)
{
    word i;
    setup(fixture, pc, bank, operand);
    ppu_initialize(&fixture->ppu);
    for (i = 0U; i < 144U; ++i) event_lines[i] = event_pixels[i];
    memset(event_pixels, 0xA5, sizeof(event_pixels));
    fixture->ppu.VRAM = vram;
    fixture->ppu.OAM = oam;
    fixture->ppu.state.framebuffer = event_lines;
    fixture->ppu.state.scanY = scanline;
    fixture->ppu.state.interlace = 3U;
    fixture->ppu.rLCDC = 0x91U;
    fixture->ppu.rSTAT = stat;
    fixture->ppu.rBGP = 0xE4U;
    fixture->ppu.rLYC = 3U;
    fixture->ppu.next_update_ticks = ticks;
    fixture->ppu.next_update_n = event;
#if defined(KNOVGBC_PPU_PALETTE_CACHE)
    ppu_dmg_palette_cache_refresh(&fixture->ppu);
#endif
    fixture->cpu.FMC_MODE = MB_FMC_MODE_ADD_r8;
    fixture->cpu.IE = 3U;
    fixture->cpu.IME_ASK = 1U; /* Inert while IME is already set. */
    /* Distinct, safe handlers: LD A,1; LDH (85),A; NOP. They make an
     * incorrectly delayed interrupt or speculative extra loop visible. */
    rom[0x40] = rom[0x48] = 0x3EU;
    rom[0x41] = rom[0x49] = 1U;
    rom[0x42] = rom[0x4A] = 0xE0U;
    rom[0x43] = rom[0x4B] = operand;
}

static word
event_commit(struct fixture *fixture, word cycles)
{
    fixture->cpu.DIV += cycles;
    pgf_timer_update(&fixture->fabric, cycles);
    return ppu_tick(&fixture->ppu, cycles << 2U);
}

static int
event_rejected(const char *name, struct fixture *fixture)
{
    const struct fixture before = *fixture;
    word instructions = 0xBEEFU;
    word cycles;
    snapshot_memory();
    cycles = picogb_idle_event_try(&fixture->cpu, &fixture->ppu,
        &fixture->fabric, &instructions);
    ++event_cases;
    if (cycles || instructions != 0xBEEFU ||
        !same_cpu(&before.cpu, &fixture->cpu) ||
        memcmp(&before.ppu, &fixture->ppu, sizeof(before.ppu)) ||
        memcmp(&before.fabric, &fixture->fabric, sizeof(before.fabric)) ||
        !memory_unchanged()) {
        fprintf(stderr, "event reject %s: changed state, cycles=%u steps=%u\n",
            name, (unsigned)cycles, (unsigned)instructions);
        return 0;
    }
    return 1;
}

static int
event_equivalent(const char *name, struct fixture *fixture, int null_counter)
{
    const struct fixture initial = *fixture;
    struct fixture candidate;
    word candidate_steps = 0U, candidate_cycles, candidate_irq;
    word reference_steps = 0U, reference_cycles = 0U, reference_irq = 0U;
    word next_cycles_candidate, next_cycles_reference;
    word i;

    snapshot_memory();
    candidate_cycles = picogb_idle_event_try(&fixture->cpu, &fixture->ppu,
        &fixture->fabric, null_counter ? NULL : &candidate_steps);
    if (!candidate_cycles || candidate_cycles > 112U ||
        memcmp(&initial.ppu, &fixture->ppu, sizeof(initial.ppu)) ||
        memcmp(&initial.fabric, &fixture->fabric, sizeof(initial.fabric)) ||
        !memory_unchanged()) {
        fprintf(stderr, "%s: helper changed non-CPU state or rejected ticks=%u\n",
            name, (unsigned)initial.ppu.next_update_ticks);
        return 0;
    }
    candidate_irq = event_commit(fixture, candidate_cycles);
    candidate = *fixture;
    memcpy(event_expected_pixels, event_pixels, sizeof(event_pixels));

    *fixture = initial;
    memset(event_pixels, 0xA5, sizeof(event_pixels));
    /* Derive the stop independently: at most 48 real instructions, stopping
     * at the first instruction boundary to meet the initial PPU deadline. */
    do {
        word cycles = mb_exec(&fixture->cpu);
        if (!cycles || reference_irq) {
            fprintf(stderr, "%s: oracle passed its first event\n", name);
            return 0;
        }
        reference_cycles += cycles;
        ++reference_steps;
        reference_irq |= event_commit(fixture, cycles);
    } while ((reference_cycles << 2U) < initial.ppu.next_update_ticks &&
        reference_steps < 48U);

    ++event_cases;
    if (candidate_cycles != reference_cycles ||
        (!null_counter && candidate_steps != reference_steps) ||
        candidate_irq != reference_irq ||
        !same_cpu(&candidate.cpu, &fixture->cpu) ||
        memcmp(&candidate.ppu, &fixture->ppu, sizeof(candidate.ppu)) ||
        memcmp(&candidate.fabric, &fixture->fabric, sizeof(candidate.fabric)) ||
        memcmp(event_expected_pixels, event_pixels, sizeof(event_pixels)) ||
        !memory_unchanged()) {
        fprintf(stderr,
            "%s: ticks=%u event=%u line=%u Mcycles=%u/%u steps=%u/%u IRQ=%u/%u "
            "PC=%04X/%04X IR=%04X/%04X F=%02X/%02X FMC=%u/%u\n", name,
            (unsigned)initial.ppu.next_update_ticks,
            (unsigned)initial.ppu.next_update_n,
            (unsigned)initial.ppu.state.scanY,
            (unsigned)candidate_cycles, (unsigned)reference_cycles,
            (unsigned)candidate_steps, (unsigned)reference_steps,
            (unsigned)candidate_irq, (unsigned)reference_irq,
            candidate.cpu.PC, fixture->cpu.PC,
            candidate.cpu.IR.raw, fixture->cpu.IR.raw,
            candidate.cpu.reg.F, fixture->cpu.reg.F,
            (unsigned)candidate.cpu.FMC_MODE, (unsigned)fixture->cpu.FMC_MODE);
        return 0;
    }

    /* Deliver the resulting IRQ exactly where the frontend does. Compare
     * three further interpreter steps, including handler stack stores and
     * the handler's HRAM write, without permitting any stale-loop shortcut. */
    for (i = 0U; i < 3U; ++i) {
        struct fixture reference = *fixture;
        r8 reference_hram[sizeof(hram)];
        r8 initial_hram[sizeof(hram)];
        memcpy(initial_hram, hram, sizeof(hram));
        memcpy(event_initial_pixels, event_pixels, sizeof(event_pixels));
        fixture->cpu.IF |= reference_irq;
        reference_irq = 0U;
        next_cycles_reference = mb_exec(&fixture->cpu);
        reference_irq |= event_commit(fixture, next_cycles_reference);
        reference = *fixture;
        memcpy(reference_hram, hram, sizeof(hram));
        memcpy(event_expected_pixels, event_pixels, sizeof(event_pixels));

        *fixture = candidate;
        memcpy(hram, initial_hram, sizeof(hram));
        memcpy(event_pixels, event_initial_pixels, sizeof(event_pixels));
        fixture->cpu.IF |= candidate_irq;
        candidate_irq = 0U;
        next_cycles_candidate = mb_exec(&fixture->cpu);
        candidate_irq |= event_commit(fixture, next_cycles_candidate);
        candidate = *fixture;
        if (next_cycles_candidate != next_cycles_reference ||
            candidate_irq != reference_irq ||
            !same_cpu(&reference.cpu, &candidate.cpu) ||
            memcmp(&reference.ppu, &candidate.ppu, sizeof(reference.ppu)) ||
            memcmp(reference_hram, hram, sizeof(hram)) ||
            memcmp(event_expected_pixels, event_pixels, sizeof(event_pixels))) {
            fprintf(stderr, "%s: diverged after subsequent step %u ticks=%u\n",
                name, (unsigned)i, (unsigned)initial.ppu.next_update_ticks);
            return 0;
        }
        *fixture = reference;
    }
    return 1;
}

int
main(void)
{
    struct fixture fixture;
    word ticks, phase, ime;
    static const word far_ticks[] = { 457U, 999U, 2000U, 0xFFFFFFFFU };
    static const word safe_pc[] = { 0U, 0x3FFBU, 0x4000U, 0x7FFBU };

    for (ticks = 1U; ticks <= 456U; ++ticks) {
        for (phase = 0U; phase < 4U; ++phase) {
            event_setup(&fixture, ticks, phase, 2U, 0x78U,
                LOOP_PC, 1U, 0x85U);
            if (!event_equivalent("PPU phase boundary", &fixture, 0)) return 1;
        }
        for (ime = 0U; ime < 2U; ++ime) {
            event_setup(&fixture, ticks, 3U, 143U, 0U,
                LOOP_PC, 1U, 0xFEU);
            fixture.cpu.IME = ime;
            fixture.cpu.IME_ASK = 0U;
            if (!event_equivalent("VBlank frame and IRQ", &fixture, 0)) return 1;
        }
        event_setup(&fixture, ticks, 0U, 153U, 0x40U,
            LOOP_PC, 1U, 0x80U);
        if (!event_equivalent("line 153 short event", &fixture, 0)) return 1;
    }
    for (ticks = 0U; ticks < sizeof(far_ticks) / sizeof(far_ticks[0]); ++ticks) {
        event_setup(&fixture, far_ticks[ticks], 0U, 2U, 0U,
            LOOP_PC, 1U, 0x85U);
        if (!event_equivalent("bounded distant event", &fixture, ticks & 1U)) return 1;
    }
    for (ticks = 0U; ticks < sizeof(safe_pc) / sizeof(safe_pc[0]); ++ticks) {
        event_setup(&fixture, 113U, 0U, 2U, 0U, safe_pc[ticks], 2U, 0x85U);
        if (!event_equivalent("ROM bank edge", &fixture, 0)) return 1;
    }

#define FRESH() event_setup(&fixture, 80U, 0U, 2U, 0U, LOOP_PC, 1U, 0x85U)
#define REJECT(name, change) do { FRESH(); change; \
    if (!event_rejected(name, &fixture)) return 1; } while (0)
    REJECT("wrong opcode", fixture.cpu.IR.low = 0xA7U);
    REJECT("high IR byte", fixture.cpu.IR.high = 1U);
    REJECT("HALT", fixture.cpu.HALTING = 1U);
    REJECT("interrupt pending", fixture.cpu.IF = 1U);
    REJECT("delayed IME", fixture.cpu.IME = 0U; fixture.cpu.IME_ASK = 1U);
    REJECT("timer enabled", fixture.fabric.TIMER_CNT = 4U);
    REJECT("timer near overflow", fixture.fabric.TIMER_CNT = 5U;
        fixture.fabric.TIMER_ACCUM = 255U; fixture.fabric.TIMER_SUB = 3U);
    REJECT("LCD disabled", fixture.ppu.rLCDC &= ~0x80U);
    REJECT("frame pending", fixture.ppu._redrawed = 255U);
    REJECT("zero deadline", fixture.ppu.next_update_ticks = 0U);
    REJECT("nonzero HRAM", hram[0x85] = 1U);
    REJECT("I/O operand", rom[LOOP_PC + 1U] = 0x7FU);
    REJECT("IE operand", rom[LOOP_PC + 1U] = 0xFFU);
    REJECT("wrong ROM F0", rom[LOOP_PC] = 0U);
    REJECT("wrong AND", rom[LOOP_PC + 2U] = 0U);
    REJECT("wrong branch", rom[LOOP_PC + 3U] = 0x18U);
    REJECT("wrong displacement", rom[LOOP_PC + 4U] = 0xFCU);
    REJECT("missing memory", fixture.cpu.mi = NULL);
    REJECT("missing HRAM", fixture.memory.HRAM = NULL);
    REJECT("missing ROM", fixture.memory.ROM = NULL);
    REJECT("PC before ROM", fixture.cpu.PC = 0U);
    REJECT("fixed bank crossing", fixture.cpu.PC = 0x3FFDU);
    REJECT("switched bank crossing", fixture.cpu.PC = 0x7FFDU);
    REJECT("outside ROM", fixture.cpu.PC = 0xC001U);
    REJECT("invalid switched bank", fixture.cpu.PC = 0x4001U;
        fixture.memory.BANK_ROM = fixture.memory.N_ROM);

    FRESH();
    fixture.cpu.IME = fixture.cpu.IME_ASK = 0U;
    fixture.cpu.IF = 3U;
    if (!event_equivalent("pending IRQ while IME disabled", &fixture, 0)) return 1;
    FRESH();
    if (picogb_idle_event_try(NULL, &fixture.ppu, &fixture.fabric, NULL) ||
        picogb_idle_event_try(&fixture.cpu, NULL, &fixture.fabric, NULL) ||
        picogb_idle_event_try(&fixture.cpu, &fixture.ppu, NULL, NULL)) return 1;
    printf("idle_event_contract: PASS (%u cases + NULL guards)\n", event_cases);
    return 0;
}
