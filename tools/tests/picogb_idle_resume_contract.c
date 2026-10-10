/* Instruction-by-instruction oracle for resuming the same idle loop at AND
 * or JR after the frontend has committed a PPU event. */
#include "picogb_idle_event_fixture.h"

static pixel_t pixels[144][192], expected_pixels[144][192];
static pixel_t saved_pixels[144][192];
static pixel_t *lines[144];
static unsigned cases;

static void
resume_setup(struct fixture *f, word entry, word ticks, word event,
    word scanline, word stat, word pc, word bank, r8 operand)
{
    word i;
    setup(f, pc, bank, operand);
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
    f->cpu.FMC_MODE = MB_FMC_MODE_ADD_r8;
    f->cpu.IE = 3U;
    f->cpu.IME_ASK = 1U; /* Inert with IME already enabled. */
    if (entry == 1U) {
        f->cpu.PC = (r16)(pc + 3U);
        f->cpu.IR.raw = 0xA7U;
        f->cpu.reg.A = 0U;
    }
    else if (entry == 2U) {
        f->cpu.PC = (r16)(pc + 4U);
        f->cpu.IR.raw = 0x28U;
        f->cpu.reg.A = 0U;
        f->cpu.reg.F = MB_FLAG_H | MB_FLAG_Z;
        f->cpu.FMC_MODE = MB_FMC_MODE_NONE;
    }
    rom[0x40] = rom[0x48] = 0x3EU; /* LD A,1; LDH (operand),A. */
    rom[0x41] = rom[0x49] = 1U;
    rom[0x42] = rom[0x4A] = 0xE0U;
    rom[0x43] = rom[0x4B] = operand;
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
    cycles = picogb_idle_event_try(&f->cpu, &f->ppu, &f->fabric, &instructions);
    ++cases;
    if (cycles || instructions != 0xBEEFU || !same_cpu(&before.cpu, &f->cpu) ||
        memcmp(&before.ppu, &f->ppu, sizeof(f->ppu)) ||
        memcmp(&before.fabric, &f->fabric, sizeof(f->fabric)) ||
        !memory_unchanged()) {
        fprintf(stderr, "resume rejection %s changed state: cycles=%u steps=%u\n",
            name, (unsigned)cycles, (unsigned)instructions);
        return 0;
    }
    return 1;
}

static int
equivalent(const char *name, struct fixture *f, int null_counter)
{
    const struct fixture initial = *f;
    const word suffix_steps = initial.cpu.IR.raw == 0xA7U ? 2U :
        initial.cpu.IR.raw == 0x28U ? 1U : 0U;
    const word max_steps = 48U + suffix_steps;
    struct fixture candidate;
    word candidate_steps = 0U, candidate_cycles, candidate_irq;
    word reference_steps = 0U, reference_cycles = 0U, reference_irq = 0U;
    word i;

    snapshot_memory();
    candidate_cycles = picogb_idle_event_try(&f->cpu, &f->ppu, &f->fabric,
        null_counter ? NULL : &candidate_steps);
    if (!candidate_cycles || candidate_cycles > 116U ||
        memcmp(&initial.ppu, &f->ppu, sizeof(f->ppu)) ||
        memcmp(&initial.fabric, &f->fabric, sizeof(f->fabric)) ||
        !memory_unchanged()) {
        fprintf(stderr, "%s: rejected or changed non-CPU state, IR=%02X ticks=%u\n",
            name, initial.cpu.IR.raw, (unsigned)initial.ppu.next_update_ticks);
        return 0;
    }
    candidate_irq = commit(f, candidate_cycles);
    candidate = *f;
    memcpy(expected_pixels, pixels, sizeof(pixels));

    *f = initial;
    memset(pixels, 0xA5, sizeof(pixels));
    /* No shortcut implementation is used in the oracle. Its bound is the
     * initial suffix followed by at most sixteen complete loop iterations. */
    do {
        const word cycles = mb_exec(&f->cpu);
        if (!cycles || reference_irq) {
            fprintf(stderr, "%s: oracle crossed its first event\n", name);
            return 0;
        }
        reference_cycles += cycles;
        ++reference_steps;
        reference_irq |= commit(f, cycles);
    } while ((reference_cycles << 2U) < initial.ppu.next_update_ticks &&
        reference_steps < max_steps);
    ++cases;
    if (candidate_cycles != reference_cycles ||
        (!null_counter && candidate_steps != reference_steps) ||
        candidate_irq != reference_irq || !same_cpu(&candidate.cpu, &f->cpu) ||
        memcmp(&candidate.ppu, &f->ppu, sizeof(f->ppu)) ||
        memcmp(&candidate.fabric, &f->fabric, sizeof(f->fabric)) ||
        memcmp(expected_pixels, pixels, sizeof(pixels)) || !memory_unchanged()) {
        fprintf(stderr, "%s IR=%02X ticks=%u event=%u line=%u cycles=%u/%u "
            "steps=%u/%u IRQ=%u/%u PC=%04X/%04X IR=%02X/%02X F=%02X/%02X FMC=%u/%u\n",
            name, initial.cpu.IR.raw, (unsigned)initial.ppu.next_update_ticks,
            (unsigned)initial.ppu.next_update_n, (unsigned)initial.ppu.state.scanY,
            (unsigned)candidate_cycles, (unsigned)reference_cycles,
            (unsigned)candidate_steps, (unsigned)reference_steps,
            (unsigned)candidate_irq, (unsigned)reference_irq,
            candidate.cpu.PC, f->cpu.PC, candidate.cpu.IR.raw, f->cpu.IR.raw,
            candidate.cpu.reg.F, f->cpu.reg.F,
            (unsigned)candidate.cpu.FMC_MODE, (unsigned)f->cpu.FMC_MODE);
        return 0;
    }
    /* Deliver IRQ at the normal frontend point. Follow three real opcodes,
     * including handler stack stores and a write invalidating the HRAM poll. */
    for (i = 0U; i < 3U; ++i) {
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
            fprintf(stderr, "%s IR=%02X ticks=%u differs after next step %u\n",
                name, initial.cpu.IR.raw, (unsigned)initial.ppu.next_update_ticks,
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
    word entry, ticks, event, ime;
    const word far_ticks[] = { 481U, 999U, 2000U, 0xFFFFFFFFU };
    const word short_ticks[] = { 1U, 5U, 17U };
    const word safe_pc[] = { 0U, 0x3FFBU, 0x4000U, 0x7FFBU };
    for (entry = 0U; entry < 3U; ++entry) {
        for (ticks = 1U; ticks <= 480U; ++ticks) {
            for (event = 0U; event < 4U; ++event) {
                resume_setup(&f, entry, ticks, event, 2U, 0x78U, LOOP_PC, 1U, 0x85U);
                if (!equivalent("PPU event boundary", &f, 0)) return 1;
            }
            for (ime = 0U; ime < 2U; ++ime) {
                resume_setup(&f, entry, ticks, 3U, 143U, 0U, LOOP_PC, 1U, 0xFEU);
                f.cpu.IME = ime;
                f.cpu.IME_ASK = 0U;
                if (!equivalent("VBlank frame and IRQ", &f, 0)) return 1;
            }
            resume_setup(&f, entry, ticks, 0U, 153U, 0x40U, LOOP_PC, 1U, 0x80U);
            if (!equivalent("line 153 short event", &f, 0)) return 1;
        }
        for (ticks = 0U; ticks < sizeof(far_ticks) / sizeof(far_ticks[0]); ++ticks) {
            resume_setup(&f, entry, far_ticks[ticks], 0U, 2U, 0U, LOOP_PC, 1U, 0x85U);
            if (!equivalent("bounded distant event", &f, ticks & 1U)) return 1;
        }
        for (ticks = 0U; ticks < sizeof(short_ticks) / sizeof(short_ticks[0]); ++ticks) {
            resume_setup(&f, entry, short_ticks[ticks], 0U, 2U, 0U, LOOP_PC, 1U, 0x85U);
            if (!equivalent("short boundary without counter", &f, 1)) return 1;
        }
        for (ticks = 0U; ticks < sizeof(safe_pc) / sizeof(safe_pc[0]); ++ticks) {
            resume_setup(&f, entry, 113U, 0U, 2U, 0U, safe_pc[ticks], 2U, 0x85U);
            if (!equivalent("ROM bank edge", &f, 0)) return 1;
        }
#define FRESH() resume_setup(&f, entry, 80U, 0U, 2U, 0U, LOOP_PC, 1U, 0x85U)
#define REJECT(name, change) do { FRESH(); change; if (!rejected(name, &f)) return 1; } while (0)
        REJECT("unsupported opcode", f.cpu.IR.raw = 0U);
        REJECT("high IR byte", f.cpu.IR.high = 1U);
        REJECT("HALT", f.cpu.HALTING = 1U);
        REJECT("pending IRQ", f.cpu.IF = 1U);
        REJECT("delayed IME", f.cpu.IME = 0U; f.cpu.IME_ASK = 1U);
        REJECT("timer enabled", f.fabric.TIMER_CNT = 4U);
        REJECT("timer near overflow", f.fabric.TIMER_CNT = 5U;
            f.fabric.TIMER_ACCUM = 255U; f.fabric.TIMER_SUB = 3U);
        REJECT("LCD disabled", f.ppu.rLCDC &= ~0x80U);
        REJECT("frame pending", f.ppu._redrawed = 255U);
        REJECT("zero deadline", f.ppu.next_update_ticks = 0U);
        REJECT("nonzero HRAM", hram[0x85] = 1U);
        REJECT("I/O operand", rom[LOOP_PC + 1U] = 0x7FU);
        REJECT("IE operand", rom[LOOP_PC + 1U] = 0xFFU);
        REJECT("wrong ROM F0", rom[LOOP_PC] = 0U);
        REJECT("wrong AND", rom[LOOP_PC + 2U] = 0U);
        REJECT("wrong branch", rom[LOOP_PC + 3U] = 0x18U);
        REJECT("wrong displacement", rom[LOOP_PC + 4U] = 0xFCU);
        REJECT("missing memory", f.cpu.mi = NULL);
        REJECT("missing HRAM", f.memory.HRAM = NULL);
        REJECT("missing ROM", f.memory.ROM = NULL);
        REJECT("PC underflow", f.cpu.PC = 0U);
        REJECT("fixed bank crossing", f.cpu.PC = (r16)(0x3FFDU + (entry == 1U ? 2U : entry == 2U ? 3U : 0U)));
        REJECT("switched bank crossing", f.cpu.PC = (r16)(0x7FFDU + (entry == 1U ? 2U : entry == 2U ? 3U : 0U)));
        REJECT("outside ROM", f.cpu.PC = 0xC001U);
        REJECT("invalid switched bank", f.cpu.PC = (r16)(0x4001U + (entry == 1U ? 2U : entry == 2U ? 3U : 0U));
            f.memory.BANK_ROM = f.memory.N_ROM);
        if (entry != 0U) {
            REJECT("nonzero A at suffix", f.cpu.reg.A = 1U);
            REJECT("suffix PC below first valid operand", f.cpu.PC = (r16)(entry == 1U ? 2U : 3U));
            REJECT("suffix PC at one", f.cpu.PC = 1U);
        }
        if (entry == 2U) {
            REJECT("branch not taken", f.cpu.reg.F = MB_FLAG_H);
            REJECT("noncanonical flags", f.cpu.reg.F |= MB_FLAG_C);
            REJECT("deferred flags", f.cpu.FMC_MODE = MB_FMC_MODE_ADD_r8);
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
        if (picogb_idle_event_try(NULL, &f.ppu, &f.fabric, NULL) ||
            picogb_idle_event_try(&f.cpu, NULL, &f.fabric, NULL) ||
            picogb_idle_event_try(&f.cpu, &f.ppu, NULL, NULL)) return 1;
        /* No positive recognition cache: HRAM must be checked anew even at
         * exactly the same opcode and ROM address on a subsequent call. */
        FRESH();
        {
            const mb_state before = f.cpu;
            if (!picogb_idle_event_try(&f.cpu, &f.ppu, &f.fabric, NULL)) return 1;
            f.cpu = before;
            hram[0x85] = 1U;
            if (!rejected("HRAM changed after accepted call", &f)) return 1;
        }
    }
    printf("idle_resume_contract: PASS (%u cases + NULL guards)\n", cases);
    return 0;
}
