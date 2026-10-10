/* Independent, instruction-by-instruction oracle for F0 41 E6 03 20 FA.
 * The reference never uses the production batching arithmetic. */
#include "picogb_idle_event_fixture.h"

static pixel_t pixels[144][192], expected_pixels[144][192];
static pixel_t saved_pixels[144][192];
static pixel_t *lines[144];
static unsigned cases;

static void
put_stat_loop(word pc, word bank)
{
    static const r8 code[] = {0xF0U, 0x41U, 0xE6U, 0x03U, 0x20U, 0xFAU};
    word i;
    for (i = 0U; i < sizeof(code); ++i) put_rom_byte(pc + i, bank, code[i]);
}

static void
stat_setup(struct fixture *f, word ticks, word event, word scanline,
    word stat, word pc, word bank)
{
    word i;
    setup(f, LOOP_PC, bank, 0x85U);
    memset(rom, 0U, sizeof(rom));
    put_stat_loop(pc, bank);
    f->cpu.IR.raw = 0x00F0U;
    f->cpu.PC = (r16)(pc + 1U);
    f->cpu.reg.F = 0xFFU;
    f->cpu.FMC_MODE = MB_FMC_MODE_ADD_r8;
    f->cpu.IMM.raw = 0xA55AU;
    f->cpu.IE = 3U;
    f->cpu.IME_ASK = 1U;
    ppu_initialize(&f->ppu);
    memset(pixels, 0xA5, sizeof(pixels));
    for (i = 0U; i < 144U; ++i) lines[i] = pixels[i];
    f->ppu.VRAM = vram;
    f->ppu.OAM = oam;
    f->ppu.state.framebuffer = lines;
    f->ppu.state.scanY = scanline;
    f->ppu.state.interlace = 3U;
    f->ppu.rLCDC = 0x91U;
    f->ppu.rSTAT = stat;
    f->ppu.rBGP = 0xE4U;
    f->ppu.rLYC = 3U;
    f->ppu.next_update_ticks = ticks;
    f->ppu.next_update_n = event;
#if defined(KNOVGBC_PPU_PALETTE_CACHE)
    ppu_dmg_palette_cache_refresh(&f->ppu);
#endif
    /* IRQ continuation changes A, then HRAM, so servicing cannot be skipped. */
    rom[0x40] = rom[0x48] = 0x3EU;
    rom[0x41] = rom[0x49] = 1U;
    rom[0x42] = rom[0x4A] = 0xE0U;
    rom[0x43] = rom[0x4B] = 0x85U;
}

static word
commit(struct fixture *f, word cycles)
{
    f->cpu.DIV += cycles;
    pgf_timer_update(&f->fabric, cycles);
    return ppu_tick(&f->ppu, cycles << 2U);
}

static int
rejected(const char *name, struct fixture *f)
{
    const struct fixture before = *f;
    word instructions = 0xBEEFU;
    word cycles, route;
    snapshot_memory();
    cycles = picogb_idle_stat_try(&f->cpu, &f->ppu, &f->fabric, &instructions);
    route = picogb_idle_event_try(&f->cpu, &f->ppu, &f->fabric, &instructions);
    ++cases;
    if (cycles || route || instructions != 0xBEEFU ||
        !same_cpu(&before.cpu, &f->cpu) ||
        memcmp(&before.ppu, &f->ppu, sizeof(f->ppu)) ||
        memcmp(&before.fabric, &f->fabric, sizeof(f->fabric)) ||
        !memory_unchanged()) {
        fprintf(stderr, "STAT reject %s changed state: cycles=%u route=%u steps=%u\n",
            name, (unsigned)cycles, (unsigned)route, (unsigned)instructions);
        return 0;
    }
    return 1;
}

static int
equivalent(const char *name, struct fixture *f, int null_counter)
{
    const struct fixture initial = *f;
    struct fixture candidate;
    mb_state direct_cpu;
    word candidate_steps = 0U, candidate_cycles, candidate_irq;
    word reference_steps = 0U, reference_cycles = 0U, reference_irq = 0U;
    word route_steps = 0U, route_cycles, i;
    snapshot_memory();
    candidate_cycles = picogb_idle_stat_try(&f->cpu, &f->ppu, &f->fabric,
        null_counter ? NULL : &candidate_steps);
    direct_cpu = f->cpu;
    if (!candidate_cycles || candidate_cycles > 128U ||
        memcmp(&initial.ppu, &f->ppu, sizeof(f->ppu)) ||
        memcmp(&initial.fabric, &f->fabric, sizeof(f->fabric)) ||
        !memory_unchanged()) {
        fprintf(stderr, "%s: helper rejected/changed device STAT=%02X ticks=%u\n",
            name, (unsigned)initial.ppu.rSTAT,
            (unsigned)initial.ppu.next_update_ticks);
        return 0;
    }
    *f = initial;
    route_cycles = picogb_idle_event_try(&f->cpu, &f->ppu, &f->fabric,
        null_counter ? NULL : &route_steps);
    if (route_cycles != candidate_cycles || route_steps != candidate_steps ||
        !same_cpu(&direct_cpu, &f->cpu) ||
        memcmp(&initial.ppu, &f->ppu, sizeof(f->ppu)) ||
        memcmp(&initial.fabric, &f->fabric, sizeof(f->fabric)) ||
        !memory_unchanged()) {
        fprintf(stderr, "%s: direct/helper route mismatch: cycles=%u/%u\n",
            name, (unsigned)candidate_cycles, (unsigned)route_cycles);
        return 0;
    }
    candidate_irq = commit(f, candidate_cycles);
    candidate = *f;
    memcpy(expected_pixels, pixels, sizeof(pixels));

    *f = initial;
    memset(pixels, 0xA5, sizeof(pixels));
    do {
        const word cycles = mb_exec(&f->cpu);
        if (!cycles || reference_irq) {
            fprintf(stderr, "%s: oracle crossed first event\n", name);
            return 0;
        }
        reference_cycles += cycles;
        ++reference_steps;
        reference_irq |= commit(f, cycles);
    } while ((reference_cycles << 2U) < initial.ppu.next_update_ticks &&
        reference_steps < 48U);
    ++cases;
    if (candidate_cycles != reference_cycles ||
        (!null_counter && candidate_steps != reference_steps) ||
        candidate_irq != reference_irq || !same_cpu(&candidate.cpu, &f->cpu) ||
        memcmp(&candidate.ppu, &f->ppu, sizeof(f->ppu)) ||
        memcmp(&candidate.fabric, &f->fabric, sizeof(f->fabric)) ||
        memcmp(expected_pixels, pixels, sizeof(pixels)) || !memory_unchanged()) {
        fprintf(stderr, "%s STAT=%02X ticks=%u event=%u line=%u "
            "cycles=%u/%u steps=%u/%u IRQ=%u/%u PC=%04X/%04X IR=%02X/%02X "
            "A=%02X/%02X F=%02X/%02X IMM=%04X/%04X FR1=%04X/%04X "
            "FR2=%04X/%04X FMC=%u/%u STAT=%02X/%02X\n", name,
            (unsigned)initial.ppu.rSTAT, (unsigned)initial.ppu.next_update_ticks,
            (unsigned)initial.ppu.next_update_n, (unsigned)initial.ppu.state.scanY,
            (unsigned)candidate_cycles, (unsigned)reference_cycles,
            (unsigned)candidate_steps, (unsigned)reference_steps,
            (unsigned)candidate_irq, (unsigned)reference_irq,
            candidate.cpu.PC, f->cpu.PC, candidate.cpu.IR.raw, f->cpu.IR.raw,
            candidate.cpu.reg.A, f->cpu.reg.A, candidate.cpu.reg.F, f->cpu.reg.F,
            candidate.cpu.IMM.raw, f->cpu.IMM.raw, candidate.cpu.FR1, f->cpu.FR1,
            candidate.cpu.FR2, f->cpu.FR2,
            (unsigned)candidate.cpu.FMC_MODE, (unsigned)f->cpu.FMC_MODE,
            (unsigned)candidate.ppu.rSTAT, (unsigned)f->ppu.rSTAT);
        return 0;
    }
    /* Observe the sampled old STAT after a transition, branch exit on mode 0,
     * later fetches, flag materialization and interrupt entry/handler writes. */
    for (i = 0U; i < 7U; ++i) {
        struct fixture reference;
        r8 initial_hram[sizeof(hram)], reference_hram[sizeof(hram)];
        word next_reference, next_candidate;
        memcpy(initial_hram, hram, sizeof(hram));
        memcpy(saved_pixels, pixels, sizeof(pixels));
        f->cpu.IF |= reference_irq;
        next_reference = mb_exec(&f->cpu);
        reference_irq = commit(f, next_reference);
        reference = *f;
        memcpy(reference_hram, hram, sizeof(hram));
        memcpy(expected_pixels, pixels, sizeof(pixels));

        *f = candidate;
        memcpy(hram, initial_hram, sizeof(hram));
        memcpy(pixels, saved_pixels, sizeof(pixels));
        f->cpu.IF |= candidate_irq;
        next_candidate = mb_exec(&f->cpu);
        candidate_irq = commit(f, next_candidate);
        candidate = *f;
        if (next_candidate != next_reference || candidate_irq != reference_irq ||
            !same_cpu(&reference.cpu, &candidate.cpu) ||
            memcmp(&reference.ppu, &candidate.ppu, sizeof(reference.ppu)) ||
            memcmp(&reference.fabric, &candidate.fabric, sizeof(reference.fabric)) ||
            memcmp(reference_hram, hram, sizeof(hram)) ||
            memcmp(expected_pixels, pixels, sizeof(pixels))) {
            fprintf(stderr, "%s STAT=%02X ticks=%u differs after step %u\n",
                name, (unsigned)initial.ppu.rSTAT,
                (unsigned)initial.ppu.next_update_ticks, (unsigned)i);
            return 0;
        }
        *f = reference;
    }
    return 1;
}

int
main(void)
{
    struct fixture f;
    word ticks, i, stat, flags, event, ime;
    const word selected_ticks[] = {1U, 11U, 12U, 13U, 19U, 20U, 21U,
        31U, 32U, 33U, 43U, 44U, 45U, 51U, 52U, 53U,
        511U, 512U, 513U, 524U, 0xFFFFFFFFU};
    const word safe_pc[] = {0U, 0x3FFAU, 0x4000U, 0x7FFAU};

    for (stat = 0U; stat < 256U; ++stat) {
        if (!(stat & 3U)) continue;
        for (i = 0U; i < sizeof(selected_ticks) / sizeof(selected_ticks[0]); ++i) {
            stat_setup(&f, selected_ticks[i], 2U, 2U, stat, LOOP_PC, 1U);
            f.cpu.reg.F = (r8)(stat ^ i);
            f.cpu.FMC_MODE = i & 7U;
            if (!equivalent("all STAT bits and cycle edges", &f, i & 1U)) return 1;
        }
    }
    for (ticks = 1U; ticks <= 524U; ++ticks) {
        for (stat = 1U; stat <= 3U; ++stat) {
            for (event = 0U; event < 4U; ++event) {
                stat_setup(&f, ticks, event, 2U, 0x78U | stat, LOOP_PC, 1U);
                if (!equivalent("every dot/PPU event/mode", &f, 0)) return 1;
            }
        }
        for (ime = 0U; ime < 2U; ++ime) {
            stat_setup(&f, ticks, 3U, 143U, 0x7AU, LOOP_PC, 1U);
            f.cpu.IME = ime;
            f.cpu.IME_ASK = 0U;
            if (!equivalent("VBlank IRQ and frame boundary", &f, 0)) return 1;
        }
        stat_setup(&f, ticks, 0U, 153U, 0x41U, LOOP_PC, 1U);
        if (!equivalent("line 153 short event", &f, 0)) return 1;
    }
    for (flags = 0U; flags < 256U; ++flags) {
        for (i = 0U; i < sizeof(selected_ticks) / sizeof(selected_ticks[0]); ++i) {
            stat_setup(&f, selected_ticks[i], 2U, 2U, 0xE3U, LOOP_PC, 1U);
            f.cpu.reg.F = (r8)flags;
            f.cpu.FMC_MODE = flags & 7U;
            if (!equivalent("all incoming flags/deferred flags", &f, 0)) return 1;
        }
    }
    for (i = 0U; i < sizeof(safe_pc) / sizeof(safe_pc[0]); ++i) {
        stat_setup(&f, 80U, 2U, 2U, 3U, safe_pc[i], 2U);
        if (!equivalent("ROM last-byte boundary", &f, 0)) return 1;
    }

#define FRESH() stat_setup(&f, 80U, 2U, 2U, 0x7BU, LOOP_PC, 1U)
#define REJECT(name, change) do { FRESH(); change; \
    if (!rejected(name, &f)) return 1; } while (0)
    REJECT("wrong opcode", f.cpu.IR.raw = 0xE6U);
    REJECT("high IR", f.cpu.IR.high = 1U);
    REJECT("HALT", f.cpu.HALTING = 1U);
    REJECT("pending IRQ", f.cpu.IF = 1U);
    REJECT("delayed EI", f.cpu.IME = 0U; f.cpu.IME_ASK = 1U);
    REJECT("timer enabled", f.fabric.TIMER_CNT = 4U);
    REJECT("timer near overflow", f.fabric.TIMER_CNT = 5U;
        f.fabric.TIMER_ACCUM = 255U; f.fabric.TIMER_SUB = 3U);
    REJECT("LCD off", f.ppu.rLCDC &= ~0x80U);
    REJECT("frame pending", f.ppu._redrawed = 255U);
    REJECT("event due", f.ppu.next_update_ticks = 0U);
    REJECT("wrong LDH opcode", rom[LOOP_PC] = 0xE0U);
    REJECT("wrong I/O address", rom[LOOP_PC + 1U] = 0x44U);
    REJECT("wrong AND", rom[LOOP_PC + 2U] = 0xA7U);
    REJECT("wrong mask", rom[LOOP_PC + 3U] = 0x02U);
    REJECT("wrong branch", rom[LOOP_PC + 4U] = 0x28U);
    REJECT("wrong offset", rom[LOOP_PC + 5U] = 0xFBU);
    REJECT("PC underflow", f.cpu.PC = 0U);
    REJECT("fixed bank crossing", f.cpu.PC = 0x3FFCU);
    REJECT("switched bank crossing", f.cpu.PC = 0x7FFCU);
    REJECT("WRAM", f.cpu.PC = 0xC001U);
    REJECT("VRAM", f.cpu.PC = 0x8001U);
    REJECT("I/O", f.cpu.PC = 0xFF42U);
    REJECT("HRAM", f.cpu.PC = 0xFF81U;
        memcpy(hram + 0x80U, rom + LOOP_PC, 6U));
    REJECT("IE as operand", f.cpu.PC = 0xFFFFU);
    REJECT("missing memory", f.cpu.mi = NULL);
    REJECT("missing ROM", f.memory.ROM = NULL);
    REJECT("different I/O callback", f.memory.dispatch_IO = pgf_cb_ROM_);
    REJECT("missing I/O callback", f.memory.dispatch_IO = NULL);
    REJECT("different I/O userdata", f.memory.userdata = NULL);
    REJECT("different PPU binding", f.fabric.ppu = NULL);
    REJECT("empty ROM", f.memory.N_ROM = 0U);
    REJECT("invalid bank", f.cpu.PC = 0x4001U;
        f.memory.BANK_ROM = f.memory.N_ROM);
    for (stat = 0U; stat < 256U; stat += 4U) {
        FRESH();
        f.ppu.rSTAT = stat;
        if (!rejected("all mode 0 STAT values", &f)) return 1;
    }
    FRESH();
    f.cpu.IME = f.cpu.IME_ASK = 0U;
    f.cpu.IF = 3U;
    if (!equivalent("IRQ disabled", &f, 0)) return 1;
    FRESH();
    f.cpu.IE = 0U;
    f.cpu.IF = 3U;
    if (!equivalent("IRQ masked", &f, 0)) return 1;
    FRESH();
    f.cpu.DIV = 0xFFFFFFF0U;
    if (!equivalent("DIV unsigned wrap", &f, 0)) return 1;
    for (i = 0U; i < 6U; ++i) {
        mb_state before;
        FRESH();
        before = f.cpu;
        if (!picogb_idle_stat_try(&f.cpu, &f.ppu, &f.fabric, NULL)) return 1;
        f.cpu = before;
        rom[LOOP_PC + i] ^= 1U;
        if (!rejected("ROM code changed after accepted call", &f)) return 1;
    }
    stat_setup(&f, 80U, 2U, 2U, 3U, 0x4000U, 2U);
    {
        const mb_state before = f.cpu;
        if (!picogb_idle_stat_try(&f.cpu, &f.ppu, &f.fabric, NULL)) return 1;
        f.cpu = before;
        f.memory.BANK_ROM = 1U;
        if (!rejected("switched bank changed", &f)) return 1;
    }
    /* The original HRAM polling recognizer must retain its own semantics. */
    setup(&f, LOOP_PC, 1U, 0x85U);
    if (picogb_idle_stat_try(&f.cpu, &f.ppu, &f.fabric, NULL) ||
        !picogb_idle_event_try(&f.cpu, &f.ppu, &f.fabric, NULL)) return 1;
    ++cases;
    FRESH();
    if (picogb_idle_stat_try(NULL, &f.ppu, &f.fabric, NULL) ||
        picogb_idle_stat_try(&f.cpu, NULL, &f.fabric, NULL) ||
        picogb_idle_stat_try(&f.cpu, &f.ppu, NULL, NULL)) return 1;
    printf("idle_stat_contract: PASS (%u cases + NULL guards; direct + routed)\n", cases);
    return 0;
}
