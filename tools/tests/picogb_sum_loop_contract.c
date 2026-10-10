/* Independent instruction-by-instruction oracle for 19 05 20 FC.
 * No arithmetic from the production grouping implementation is reused. */
#include "picogb_idle_event_fixture.h"

static pixel_t pixels[144][192], expected_pixels[144][192];
static pixel_t saved_pixels[144][192];
static pixel_t *lines[144];
static unsigned cases;

static void
put_sum_loop(word pc, word bank)
{
    static const r8 code[] = {0x19U, 0x05U, 0x20U, 0xFCU};
    word i;
    for (i = 0U; i < sizeof(code); ++i) {
        if (pc >= 0xFF80U) hram[(pc + i) & 0xFFU] = code[i];
        else put_rom_byte(pc + i, bank, code[i]);
    }
}

static void
sum_setup(struct fixture *f, word b, word hl, word de, word ticks,
    word event, word scanline, word stat, word pc, word bank)
{
    word i;
    setup(f, LOOP_PC, bank, 0x85U);
    memset(rom, 0U, sizeof(rom));
    put_sum_loop(pc, bank);
    f->cpu.IR.raw = 0x0019U;
    f->cpu.PC = (r16)(pc + 1U);
    f->cpu.reg.B = (r8)b;
    f->cpu.reg.HL = (r16)hl;
    f->cpu.reg.DE = (r16)de;
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
    /* IRQ handler: overwrite A and HRAM, exercising continuation and stack. */
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
    word cycles;
    snapshot_memory();
    cycles = picogb_sum_loop_try(&f->cpu, &f->ppu, &f->fabric, &instructions);
    ++cases;
    if (cycles || instructions != 0xBEEFU || !same_cpu(&before.cpu, &f->cpu) ||
        memcmp(&before.ppu, &f->ppu, sizeof(f->ppu)) ||
        memcmp(&before.fabric, &f->fabric, sizeof(f->fabric)) ||
        !memory_unchanged()) {
        fprintf(stderr, "sum reject %s changed state: cycles=%u steps=%u\n",
            name, (unsigned)cycles, (unsigned)instructions);
        return 0;
    }
    return 1;
}

static int
equivalent(const char *name, struct fixture *f, int null_counter)
{
    const struct fixture initial = *f;
    struct fixture candidate;
    word candidate_steps = 0U, candidate_cycles, candidate_irq;
    word reference_steps = 0U, reference_cycles = 0U, reference_irq = 0U;
    word i;
    snapshot_memory();
    candidate_cycles = picogb_sum_loop_try(&f->cpu, &f->ppu, &f->fabric,
        null_counter ? NULL : &candidate_steps);
    if (!candidate_cycles || candidate_cycles > 96U ||
        memcmp(&initial.ppu, &f->ppu, sizeof(f->ppu)) ||
        memcmp(&initial.fabric, &f->fabric, sizeof(f->fabric)) ||
        !memory_unchanged()) {
        fprintf(stderr, "%s: helper rejected/changed device B=%02X ticks=%u\n",
            name, initial.cpu.reg.B, (unsigned)initial.ppu.next_update_ticks);
        return 0;
    }
    candidate_irq = commit(f, candidate_cycles);
    candidate = *f;
    memcpy(expected_pixels, pixels, sizeof(pixels));

    *f = initial;
    memset(pixels, 0xA5, sizeof(pixels));
    do {
        const word executed = f->cpu.IR.raw;
        const word cycles = mb_exec(&f->cpu);
        if (!cycles || reference_irq) {
            fprintf(stderr, "%s: oracle crossed first event\n", name);
            return 0;
        }
        reference_cycles += cycles;
        ++reference_steps;
        reference_irq |= commit(f, cycles);
        if (executed == 0x0005U && f->cpu.reg.B == 0U) break;
    } while ((reference_cycles << 2U) < initial.ppu.next_update_ticks &&
        reference_steps < 48U);
    ++cases;
    if (candidate_cycles != reference_cycles ||
        (!null_counter && candidate_steps != reference_steps) ||
        candidate_irq != reference_irq || !same_cpu(&candidate.cpu, &f->cpu) ||
        memcmp(&candidate.ppu, &f->ppu, sizeof(f->ppu)) ||
        memcmp(&candidate.fabric, &f->fabric, sizeof(f->fabric)) ||
        memcmp(expected_pixels, pixels, sizeof(pixels)) || !memory_unchanged()) {
        fprintf(stderr, "%s B=%02X HL=%04X DE=%04X ticks=%u event=%u line=%u "
            "cycles=%u/%u steps=%u/%u IRQ=%u/%u PC=%04X/%04X IR=%02X/%02X "
            "B=%02X/%02X HL=%04X/%04X F=%02X/%02X FR1=%04X/%04X "
            "FR2=%04X/%04X FMC=%u/%u\n", name, initial.cpu.reg.B,
            initial.cpu.reg.HL, initial.cpu.reg.DE,
            (unsigned)initial.ppu.next_update_ticks,
            (unsigned)initial.ppu.next_update_n, (unsigned)initial.ppu.state.scanY,
            (unsigned)candidate_cycles, (unsigned)reference_cycles,
            (unsigned)candidate_steps, (unsigned)reference_steps,
            (unsigned)candidate_irq, (unsigned)reference_irq,
            candidate.cpu.PC, f->cpu.PC, candidate.cpu.IR.raw, f->cpu.IR.raw,
            candidate.cpu.reg.B, f->cpu.reg.B, candidate.cpu.reg.HL, f->cpu.reg.HL,
            candidate.cpu.reg.F, f->cpu.reg.F, candidate.cpu.FR1, f->cpu.FR1,
            candidate.cpu.FR2, f->cpu.FR2,
            (unsigned)candidate.cpu.FMC_MODE, (unsigned)f->cpu.FMC_MODE);
        return 0;
    }
    /* The next instructions materialize flags and service pending interrupts. */
    for (i = 0U; i < 4U; ++i) {
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
            fprintf(stderr, "%s B=%02X ticks=%u differs after next step %u\n",
                name, initial.cpu.reg.B, (unsigned)initial.ppu.next_update_ticks,
                (unsigned)i);
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
    word b, ticks, i, pair, event, ime;
    const word selected_ticks[] = {1U, 7U, 8U, 9U, 11U, 12U, 13U, 23U,
        24U, 25U, 31U, 32U, 33U, 35U, 36U, 37U, 47U, 48U, 49U,
        372U, 373U, 383U, 384U, 385U, 456U, 0xFFFFFFFFU};
    const word selected_b[] = {0U, 1U, 2U, 16U, 17U, 40U};
    const word operands[][2] = {{0U, 0U}, {0U, 1U}, {0x0FFFU, 1U},
        {0xFFFFU, 1U}, {0x8000U, 0x8000U}, {0xFFFFU, 0xFFFFU},
        {0x1234U, 0x4321U}, {0xF001U, 0x0FFFU}};
    const word safe_pc[] = {0U, 0x3FFCU, 0x4000U, 0x7FFCU,
        0xFF80U, 0xFFB0U, 0xFFFBU};

    for (b = 0U; b < 256U; ++b) {
        for (pair = 0U; pair < sizeof(operands) / sizeof(operands[0]); ++pair) {
            for (i = 0U; i < sizeof(selected_ticks) / sizeof(selected_ticks[0]); ++i) {
                sum_setup(&f, b, operands[pair][0], operands[pair][1],
                    selected_ticks[i], 0U, 2U, 0U,
                    i & 1U ? 0xFFB0U : LOOP_PC, 1U);
                f.cpu.reg.F = (r8)(b ^ pair);
                f.cpu.FMC_MODE = b & 7U;
                if (!equivalent("all B/operand pairs and cycle edges", &f, i & 1U))
                    return 1;
            }
        }
    }
    for (ticks = 1U; ticks <= 456U; ++ticks) {
        for (i = 0U; i < sizeof(selected_b) / sizeof(selected_b[0]); ++i) {
            for (event = 0U; event < 4U; ++event) {
                sum_setup(&f, selected_b[i], 0xFFFFU, 0x1001U, ticks,
                    event, 2U, 0x78U, i & 1U ? 0xFFB0U : LOOP_PC, 1U);
                if (!equivalent("PPU phase boundary", &f, 0)) return 1;
            }
        }
        for (ime = 0U; ime < 2U; ++ime) {
            sum_setup(&f, 40U, 0x8000U, 0x9001U, ticks, 3U, 143U, 0U,
                0xFFB0U, 1U);
            f.cpu.IME = ime;
            f.cpu.IME_ASK = 0U;
            if (!equivalent("VBlank and frame boundary", &f, 0)) return 1;
        }
        sum_setup(&f, 0U, 0x4321U, 0xFEDCU, ticks, 0U, 153U, 0x40U,
            LOOP_PC, 1U);
        if (!equivalent("line 153 short event", &f, 0)) return 1;
    }
    for (i = 0U; i < sizeof(safe_pc) / sizeof(safe_pc[0]); ++i) {
        sum_setup(&f, 2U, 0xFFFFU, 1U, 80U, 0U, 2U, 0U, safe_pc[i], 2U);
        if (!equivalent("ROM/HRAM last-byte boundary", &f, 0)) return 1;
    }
    for (i = 0xFF80U; i <= 0xFFFBU; ++i) {
        sum_setup(&f, 20U, 0xAAAAU, 0x5555U, 37U, 0U, 2U, 0U, i, 1U);
        if (!equivalent("every valid HRAM loop position", &f, 0)) return 1;
    }
    for (i = 0U; i < 256U; ++i) {
        for (ticks = 8U; ticks <= 24U; ticks += 4U) {
            sum_setup(&f, 16U, 0xFFFU, 1U, ticks, 0U, 2U, 0U, LOOP_PC, 1U);
            f.cpu.reg.F = (r8)i;
            f.cpu.FMC_MODE = i & 7U;
            if (!equivalent("all incoming flags in partial phases", &f, 0)) return 1;
        }
    }

#define FRESH() sum_setup(&f, 40U, 0xFFFFU, 0x1001U, 80U, 0U, 2U, 0U, LOOP_PC, 1U)
#define REJECT(name, change) do { FRESH(); change; \
    if (!rejected(name, &f)) return 1; } while (0)
    REJECT("wrong opcode", f.cpu.IR.raw = 0x05U);
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
    REJECT("wrong ADD", rom[LOOP_PC] = 0x29U);
    REJECT("wrong DEC", rom[LOOP_PC + 1U] = 0x0DU);
    REJECT("wrong branch", rom[LOOP_PC + 2U] = 0x28U);
    REJECT("wrong offset", rom[LOOP_PC + 3U] = 0xFDU);
    REJECT("PC underflow", f.cpu.PC = 0U);
    REJECT("fixed bank crossing", f.cpu.PC = 0x3FFEU);
    REJECT("switched bank crossing", f.cpu.PC = 0x7FFEU);
    REJECT("WRAM", f.cpu.PC = 0xC001U);
    REJECT("VRAM", f.cpu.PC = 0x8001U);
    REJECT("I/O", f.cpu.PC = 0xFF80U);
    REJECT("IE crossing", f.cpu.PC = 0xFFFDU);
    REJECT("IE as opcode", f.cpu.PC = 0xFFFFU);
    REJECT("missing memory", f.cpu.mi = NULL);
    REJECT("missing ROM", f.memory.ROM = NULL);
    REJECT("empty ROM", f.memory.N_ROM = 0U);
    REJECT("invalid bank", f.cpu.PC = 0x4001U;
        f.memory.BANK_ROM = f.memory.N_ROM);
    sum_setup(&f, 40U, 0xFFFFU, 0x1001U, 80U, 0U, 2U, 0U, 0xFFB0U, 1U);
    f.memory.HRAM = NULL;
    if (!rejected("missing HRAM", &f)) return 1;
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
    for (i = 0U; i < 4U; ++i) {
        mb_state before;
        sum_setup(&f, 40U, 0xFFFFU, 0x1001U, 80U, 0U, 2U, 0U, 0xFFB0U, 1U);
        before = f.cpu;
        if (!picogb_sum_loop_try(&f.cpu, &f.ppu, &f.fabric, NULL)) return 1;
        f.cpu = before;
        hram[0xB0U + i] ^= 1U;
        if (!rejected("HRAM code changed after accepted call", &f)) return 1;
    }
    sum_setup(&f, 40U, 0xFFFFU, 0x1001U, 80U, 0U, 2U, 0U, 0x4000U, 2U);
    {
        const mb_state before = f.cpu;
        if (!picogb_sum_loop_try(&f.cpu, &f.ppu, &f.fabric, NULL)) return 1;
        f.cpu = before;
        f.memory.BANK_ROM = 1U;
        if (!rejected("switched bank changed", &f)) return 1;
    }
    FRESH();
    if (picogb_sum_loop_try(NULL, &f.ppu, &f.fabric, NULL) ||
        picogb_sum_loop_try(&f.cpu, NULL, &f.fabric, NULL) ||
        picogb_sum_loop_try(&f.cpu, &f.ppu, NULL, NULL)) return 1;
    printf("sum_loop_contract: PASS (%u cases + NULL guards)\n", cases);
    return 0;
}
