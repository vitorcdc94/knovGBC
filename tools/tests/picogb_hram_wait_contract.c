/* Reuse fixture/setup utilities, not the old shortcut oracle. Expected state
 * is derived by real mb_exec instructions and individual device commits. */
#define main original_resume_contract_main
#include "picogb_idle_resume_contract.c"
#undef main
#undef FRESH
#undef REJECT
#include "picogb_hram_wait.h"

static unsigned multiple_groups, capped, pending_at_cap, frames, irqs;
#if defined(KNOVGBC_PPU_EVENT_COALESCE)
static unsigned coalesced_groups, extended_caps;
static unsigned last_candidate_groups;
static unsigned long long candidate_groups_total;

/* The coalesced helper may reach its 512-group safety cap later than the
 * original helper. Continue the independent, instruction-by-instruction
 * oracle to the candidate's exact cycle count, without an artificial cap.
 * A frame or an enabled interrupt before that point is never skippable. */
static word
wait_oracle_to_cycles(struct fixture *f, word target, word *pending,
    word *steps, word *groups)
{
    word total = 0U;
    *steps = *groups = 0U;
    while (total < target) {
        const word deadline = f->ppu.next_update_ticks;
        const word suffix = f->cpu.IR.raw == 0xA7U ? 2U :
            f->cpu.IR.raw == 0x28U ? 1U : 0U;
        word group_cycles = 0U, group_steps = 0U;
        do {
            const word cycles = mb_exec(&f->cpu);
            if (!cycles || total + cycles > target ||
                (f->cpu.IME && ((f->cpu.IF | *pending) & f->cpu.IE))) {
                fprintf(stderr, "extended oracle crossed IRQ/cycle boundary\n");
                return 0U;
            }
            total += cycles;
            group_cycles += cycles;
            ++group_steps;
            ++*steps;
            *pending |= commit(f, cycles);
            if (total < target && (f->ppu._redrawed ||
                (f->cpu.IME && ((f->cpu.IF | *pending) & f->cpu.IE)))) {
                fprintf(stderr, "coalescing crossed frame/IRQ boundary\n");
                return 0U;
            }
        } while ((group_cycles << 2U) < deadline &&
            group_steps < 48U + suffix && total < target);
        ++*groups;
        if (total < target) {
            f->cpu.IF |= *pending;
            *pending = 0U;
        }
    }
    return total;
}
#endif

static word
wait_oracle(struct fixture *f, word *pending, word *steps, word *groups)
{
    word total = 0U;
    *steps = *groups = 0U;
    do {
        const word deadline = f->ppu.next_update_ticks;
        const word suffix = f->cpu.IR.raw == 0xA7U ? 2U :
            f->cpu.IR.raw == 0x28U ? 1U : 0U;
        word group_cycles = 0U, group_steps = 0U;
        do {
            const word cycles = mb_exec(&f->cpu);
            if (!cycles || *pending) {
                fprintf(stderr, "oracle crossed an IRQ boundary\n");
                return 0U;
            }
            group_cycles += cycles;
            ++group_steps;
            *pending |= commit(f, cycles);
        } while ((group_cycles << 2U) < deadline && group_steps < 48U + suffix);
        total += group_cycles;
        *steps += group_steps;
        ++*groups;
        if (f->ppu._redrawed || (f->cpu.IME &&
            ((f->cpu.IF | *pending) & f->cpu.IE)) ||
            *groups == 512U || !f->ppu.next_update_ticks) break;
        f->cpu.IF |= *pending;
        *pending = 0U;
    } while (1);
    return total;
}

static int
wait_rejected(const char *name, struct fixture *f, word pending)
{
    const struct fixture before = *f;
    const word initial_pending = pending;
    word steps = 0xBEEFU, groups = 0xFACEU, cycles;
    snapshot_memory();
    memcpy(saved_pixels, pixels, sizeof(pixels));
    cycles = picogb_hram_wait_try(&f->cpu, &f->ppu, &f->fabric,
        &pending, &steps, &groups);
    ++cases;
    if (cycles || steps != 0xBEEFU || groups != 0xFACEU ||
        pending != initial_pending || memcmp(&before, f, sizeof(*f)) ||
        memcmp(saved_pixels, pixels, sizeof(pixels)) || !memory_unchanged()) {
        fprintf(stderr, "wait rejection %s mutated state (cycles=%u)\n",
            name, (unsigned)cycles);
        return 0;
    }
    return 1;
}

static int
wait_equivalent(const char *name, struct fixture *f, word null_mask)
{
    const struct fixture initial = *f;
    struct fixture candidate;
    word cs = 0xBEEFU, cg = 0xFACEU, cc, ci = 0U;
    word rs, rg, rc, ri = 0U, i;
    snapshot_memory();
    cc = picogb_hram_wait_try(&f->cpu, &f->ppu, &f->fabric, &ci,
        (null_mask & 1U) ? NULL : &cs, (null_mask & 2U) ? NULL : &cg);
    if (!cc || !memory_unchanged()) {
        fprintf(stderr, "%s: rejected or mutated RAM IR=%02X ticks=%u\n",
            name, initial.cpu.IR.raw, (unsigned)initial.ppu.next_update_ticks);
        return 0;
    }
    candidate = *f;
    memcpy(expected_pixels, pixels, sizeof(pixels));
    *f = initial;
    memset(pixels, 0xA5, sizeof(pixels));
    rc = wait_oracle(f, &ri, &rs, &rg);
#if defined(KNOVGBC_PPU_EVENT_COALESCE)
    if (rc != cc && rg == PICOGB_HRAM_WAIT_MAX_GROUPS && cc > rc) {
        /* A coalesced group can consume several ordinary event groups. The
         * cap counts candidate groups, not individual PPU transitions. */
        *f = initial;
        memset(pixels, 0xA5, sizeof(pixels));
        ri = 0U;
        rc = wait_oracle_to_cycles(f, cc, &ri, &rs, &rg);
        ++extended_caps;
    }
    if (cg < rg) ++coalesced_groups;
    if (!(null_mask & 2U)) {
        last_candidate_groups = cg;
        candidate_groups_total += cg;
    }
#endif
    ++cases;
    if (rg > 1U) ++multiple_groups;
    if (rg == 512U) ++capped;
    if (rg == 512U && ri) ++pending_at_cap;
    if (f->ppu._redrawed) ++frames;
    if (f->cpu.IME && ((f->cpu.IF | ri) & f->cpu.IE)) ++irqs;
    if (!rc || cc != rc || (!(null_mask & 1U) && cs != rs) ||
        (!(null_mask & 2U) &&
#if defined(KNOVGBC_PPU_EVENT_COALESCE)
            (cg == 0U || cg > rg)) ||
#else
            cg != rg) ||
#endif
        ((null_mask & 1U) && cs != 0xBEEFU) ||
        ((null_mask & 2U) && cg != 0xFACEU) ||
        ci != ri || !same_cpu(&candidate.cpu, &f->cpu) ||
        memcmp(&candidate.ppu, &f->ppu, sizeof(f->ppu)) ||
        memcmp(&candidate.fabric, &f->fabric, sizeof(f->fabric)) ||
        memcmp(expected_pixels, pixels, sizeof(pixels)) || !memory_unchanged()) {
        fprintf(stderr, "%s IR=%02X ticks=%u event=%u line=%u cycles=%u/%u "
            "steps=%u/%u groups=%u/%u pending=%u/%u IF=%u/%u "
            "PC=%04X/%04X IR=%02X/%02X F=%02X/%02X FMC=%u/%u\n",
            name, initial.cpu.IR.raw, (unsigned)initial.ppu.next_update_ticks,
            (unsigned)initial.ppu.next_update_n, (unsigned)initial.ppu.state.scanY,
            (unsigned)cc, (unsigned)rc, (unsigned)cs, (unsigned)rs,
            (unsigned)cg, (unsigned)rg, (unsigned)ci, (unsigned)ri,
            (unsigned)candidate.cpu.IF, (unsigned)f->cpu.IF,
            candidate.cpu.PC, f->cpu.PC, candidate.cpu.IR.raw, f->cpu.IR.raw,
            candidate.cpu.reg.F, f->cpu.reg.F,
            (unsigned)candidate.cpu.FMC_MODE, (unsigned)f->cpu.FMC_MODE);
        return 0;
    }
    /* IRQ delivery and four real instructions include the handler's HRAM
     * write. A shortcut that runs beyond an interrupt cannot pass this. */
    for (i = 0U; i < 4U; ++i) {
        struct fixture reference;
        r8 initial_hram[sizeof(hram)], reference_hram[sizeof(hram)];
        word nr, nc;
        memcpy(initial_hram, hram, sizeof(hram));
        memcpy(saved_pixels, pixels, sizeof(pixels));
        f->cpu.IF |= ri;
        nr = mb_exec(&f->cpu);
        ri = commit(f, nr);
        reference = *f;
        memcpy(reference_hram, hram, sizeof(hram));
        memcpy(expected_pixels, pixels, sizeof(pixels));
        *f = candidate;
        memcpy(hram, initial_hram, sizeof(hram));
        memcpy(pixels, saved_pixels, sizeof(pixels));
        f->cpu.IF |= ci;
        nc = mb_exec(&f->cpu);
        ci = commit(f, nc);
        candidate = *f;
        if (nc != nr || ci != ri || !same_cpu(&reference.cpu, &candidate.cpu) ||
            memcmp(&reference.ppu, &candidate.ppu, sizeof(reference.ppu)) ||
            memcmp(&reference.fabric, &candidate.fabric, sizeof(reference.fabric)) ||
            memcmp(reference_hram, hram, sizeof(hram)) ||
            memcmp(expected_pixels, pixels, sizeof(pixels))) {
            fprintf(stderr, "%s: diverged after continuation instruction %u\n",
                name, (unsigned)i);
            return 0;
        }
        *f = reference;
    }
    return 1;
}

#if !defined(PICOGB_HRAM_WAIT_CONTRACT_NO_MAIN)
int
main(void)
{
    struct fixture f;
    word entry, ticks, event, mode, i;
    static const word edges[] = {0U, 0x3FFBU, 0x4000U, 0x7FFBU};
    static const r8 operands[] = {0x80U, 0x85U, 0xFEU};
    static const word distant[] = {457U, 999U, 2000U, 0xFFFFFFFFU};
#if defined(KNOVGBC_PPU_EVENT_COALESCE)
    static const word scanlines[] = {0U, 1U, 141U, 142U, 143U};
    static const r8 stat_sources[] = {0x08U, 0x10U, 0x20U, 0x40U};
    word scan;
    unsigned edge141 = 0U, edge142 = 0U, edge143 = 0U;
    unsigned edge_stat = 0U, edge_sched = 0U;
    /* Enter a visible line at mode 2 or mode 3, then exercise each possible
     * mode-3 remainder. Mode 2 lets the first ordinary admission stop at
     * mode 3, so the next group actually tests n1 -> n3 coalescing. */
    for (scan = 0U; scan < sizeof(scanlines) / sizeof(scanlines[0]); ++scan) {
        for (event = 0U; event <= 1U; ++event) {
            const unsigned before = coalesced_groups;
            for (ticks = 1U; ticks <= 80U; ++ticks) {
                resume_setup(&f, 0U, ticks, event, scanlines[scan], 0U,
                    LOOP_PC, 1U, 0x85U);
                f.cpu.IME = f.cpu.IME_ASK = f.cpu.IE = 0U;
                if (!wait_equivalent("n1 remaining ticks/visible-line edge",
                        &f, 0U)) return 1;
            }
            if (event == 0U && scanlines[scan] < 143U &&
                coalesced_groups == before) {
                fprintf(stderr, "n1 -> n3 coalescing not reached at line %u\n",
                    (unsigned)scanlines[scan]);
                return 1;
            }
            if (scanlines[scan] == 143U && coalesced_groups != before) {
                fprintf(stderr, "coalescing crossed the VBlank guard\n");
                return 1;
            }
        }
    }
    /* Fixed probes make the 039/040 comparison observable without relying
     * on timing. Only line 141 may take the two-line path; 142 must retain
     * the one-line path, while 143 must stop before VBlank. */
    resume_setup(&f, 0U, 17U, 0U, 141U, 0U, LOOP_PC, 1U, 0x85U);
    f.cpu.IME = f.cpu.IME_ASK = f.cpu.IE = 0U;
    if (!wait_equivalent("two-line edge 141", &f, 0U)) return 1;
    edge141 = last_candidate_groups;
    resume_setup(&f, 0U, 17U, 0U, 142U, 0U, LOOP_PC, 1U, 0x85U);
    f.cpu.IME = f.cpu.IME_ASK = f.cpu.IE = 0U;
    if (!wait_equivalent("one-line edge 142", &f, 0U)) return 1;
    edge142 = last_candidate_groups;
    resume_setup(&f, 0U, 17U, 0U, 143U, 0U, LOOP_PC, 1U, 0x85U);
    f.cpu.IME = f.cpu.IME_ASK = f.cpu.IE = 0U;
    if (!wait_equivalent("VBlank edge 143", &f, 0U)) return 1;
    edge143 = last_candidate_groups;
    /* STAT sources and a pre-scheduled STAT interrupt must retain the
     * one-event path. No IRQ is allowed to be swallowed by coalescing. */
    for (i = 0U; i < sizeof(stat_sources) / sizeof(stat_sources[0]); ++i) {
        const unsigned before = coalesced_groups;
        resume_setup(&f, 0U, 17U, 0U, 10U, stat_sources[i],
            LOOP_PC, 1U, 0x85U);
        f.cpu.IME = f.cpu.IME_ASK = f.cpu.IE = 0U;
        if (!wait_equivalent("enabled STAT source", &f, 0U) ||
            coalesced_groups != before) return 1;
        if (i == 0U) edge_stat = last_candidate_groups;
    }
    {
        const unsigned before = coalesced_groups;
        resume_setup(&f, 0U, 17U, 0U, 10U, 0U, LOOP_PC, 1U, 0x85U);
        f.ppu.IF_SCHED = 2U;
        f.cpu.IME = 1U;
        f.cpu.IME_ASK = 0U;
        f.cpu.IE = 2U;
        if (!wait_equivalent("pre-scheduled STAT IRQ", &f, 0U) ||
            coalesced_groups != before) return 1;
        edge_sched = last_candidate_groups;
    }
#endif
    for (entry = 0U; entry < 3U; ++entry) {
        for (ticks = 1U; ticks <= 456U; ++ticks) {
            for (event = 0U; event < 4U; ++event) {
                resume_setup(&f, entry, ticks, event, 2U, 0x78U, LOOP_PC, 1U, 0x85U);
                if (!wait_equivalent("event/resume boundary", &f, 0U)) return 1;
            }
            resume_setup(&f, entry, ticks, 3U, 143U, 0U, LOOP_PC, 1U, 0xFEU);
            if (!wait_equivalent("VBlank frame exit", &f, 0U)) return 1;
        }
        for (i = 0U; i < sizeof(edges) / sizeof(edges[0]); ++i) {
            word op;
            for (op = 0U; op < sizeof(operands) / sizeof(operands[0]); ++op) {
                resume_setup(&f, entry, 17U, 0U, 2U, 0x78U, edges[i], 2U, operands[op]);
                if (!wait_equivalent("ROM/HRAM edge", &f, (i + op) & 3U)) return 1;
            }
        }
        for (mode = 0U; mode < 8U; ++mode) {
            resume_setup(&f, entry, 1U + mode, 0U, 153U, 0x78U, LOOP_PC, 1U, 0x80U);
            f.cpu.IME = mode & 1U;
            f.cpu.IME_ASK = 0U;
            f.cpu.IE = mode >> 1U;
            if (!wait_equivalent("line153 IRQ masks", &f, mode & 3U)) return 1;
        }
        for (i = 0U; i < sizeof(distant) / sizeof(distant[0]); ++i) {
            resume_setup(&f, entry, distant[i], 0U, 2U, 0U, LOOP_PC, 1U, 0x85U);
            f.cpu.IE = 0U;
            if (!wait_equivalent("distant event/group cap", &f, i)) return 1;
        }
        for (mode = 0U; mode < 4U; ++mode) {
            resume_setup(&f, entry, 79U, 0U, 100U, 0x78U, LOOP_PC, 1U, 0x85U);
            f.cpu.IME = mode & 1U;
            f.cpu.IME_ASK = 0U;
            f.cpu.IE = (mode & 2U) ? 1U : 0U;
            f.cpu.IF = 0xE0U | ((mode & 1U) ? 0U : 3U);
            f.cpu.DIV = 0xFFFFFFF0U;
            f.fabric.TIMER_ACCUM = 255U;
            f.fabric.TIMER_SUB = 127U;
            if (!wait_equivalent("long wait/disabled timer", &f, mode)) return 1;
        }
        /* Extend the initial deadline so 512 groups expire before VBlank,
         * with an enabled STAT source but IME off on the final group. */
        resume_setup(&f, entry, 44801U, 2U, 0U, 0x78U, LOOP_PC, 1U, 0x85U);
        f.cpu.IME = f.cpu.IME_ASK = 0U;
        if (!wait_equivalent("unmerged IRQ retained at cap", &f, 0U)) return 1;
        for (mode = 0U; mode < 4U; ++mode) {
            resume_setup(&f, entry, 79U, 0U, 140U, 0U, LOOP_PC, 1U, 0x85U);
            f.cpu.IE = 0U;
            f.ppu.state.interlace = mode;
            f.ppu.rLCDC |= 2U;
            f.ppu.rOBP0 = f.ppu.rOBP1 = 0xE4U;
            for (i = 0U; i < sizeof(vram); ++i) vram[i] = (r8)(i * 13U + 7U);
            oam[0] = 156U; oam[1] = 16U; oam[2] = 7U;
            oam[4] = 157U; oam[5] = 12U; oam[6] = 5U; oam[7] = 0x20U;
#if defined(KNOVGBC_PPU_PALETTE_CACHE)
            ppu_dmg_palette_cache_refresh(&f.ppu);
#endif
            if (!wait_equivalent("rendered/frameskipped pixels", &f, mode)) return 1;
        }
    }
    for (mode = 0U; mode < 16U; ++mode) {
        for (entry = 0U; entry < 2U; ++entry) {
            resume_setup(&f, entry, 1U, 0U, 2U, 0x78U, LOOP_PC, 1U, 0x85U);
            f.cpu.reg.F = (r8)(mode << 4U);
            if (!wait_equivalent("deferred flags", &f, 0U)) return 1;
            resume_setup(&f, entry, 13U, 0U, 2U, 0x78U, LOOP_PC, 1U, 0x85U);
            f.cpu.reg.F = (r8)(mode << 4U);
            f.cpu.FMC_MODE = MB_FMC_MODE_NONE;
            if (!wait_equivalent("raw flags", &f, 0U)) return 1;
        }
    }
#define FRESH() resume_setup(&f, 0U, 80U, 0U, 2U, 0U, LOOP_PC, 1U, 0x85U)
#define REJECT(name, change) do { FRESH(); change; \
    if (!wait_rejected(name, &f, 0U)) return 1; } while (0)
    REJECT("unsupported opcode", f.cpu.IR.raw = 0x3EU);
    REJECT("high IR byte", f.cpu.IR.high = 1U);
    REJECT("HALT", f.cpu.HALTING = 1U);
    REJECT("pending interrupt", f.cpu.IF = 1U);
    REJECT("high pending bits", f.cpu.IF = 0xE0U; f.cpu.IE = 0xE0U);
    REJECT("delayed EI", f.cpu.IME = 0U; f.cpu.IME_ASK = 1U);
    REJECT("timer enabled", f.fabric.TIMER_CNT = 4U);
    REJECT("timer overflow", f.fabric.TIMER_CNT = 5U;
        f.fabric.TIMER_ACCUM = 255U; f.fabric.TIMER_SUB = 3U);
    REJECT("LCD off", f.ppu.rLCDC &= ~0x80U);
    REJECT("frame ready", f.ppu._redrawed = 255U);
    REJECT("deadline zero", f.ppu.next_update_ticks = 0U);
    REJECT("mailbox nonzero", hram[0x85] = 1U);
    REJECT("IO operand", rom[LOOP_PC + 1U] = 0x7FU);
    REJECT("IE operand", rom[LOOP_PC + 1U] = 0xFFU);
    REJECT("ROM F0", rom[LOOP_PC] = 0U);
    REJECT("ROM AND", rom[LOOP_PC + 2U] = 0U);
    REJECT("ROM JR", rom[LOOP_PC + 3U] = 0x18U);
    REJECT("ROM displacement", rom[LOOP_PC + 4U] = 0xFCU);
    REJECT("missing memory", f.cpu.mi = NULL);
    REJECT("missing HRAM", f.memory.HRAM = NULL);
    REJECT("missing ROM", f.memory.ROM = NULL);
    REJECT("PC below ROM", f.cpu.PC = 0U);
    REJECT("PC outside ROM", f.cpu.PC = 0xC001U);
    REJECT("fixed bank crossing", f.cpu.PC = 0x3FFDU);
    REJECT("switch bank crossing", f.cpu.PC = 0x7FFDU);
    REJECT("invalid bank", f.cpu.PC = 0x4001U; f.memory.BANK_ROM = f.memory.N_ROM);
    REJECT("wrong CPU binding", f.fabric.mb = NULL);
    REJECT("wrong PPU binding", f.fabric.ppu = NULL);
    FRESH();
    if (!wait_rejected("incoming unmerged IRQ", &f, 2U)) return 1;
    for (entry = 1U; entry < 3U; ++entry) {
        resume_setup(&f, entry, 80U, 0U, 2U, 0U, LOOP_PC, 1U, 0x85U);
        f.cpu.reg.A = 1U;
        if (!wait_rejected("resume A nonzero", &f, 0U)) return 1;
    }
    resume_setup(&f, 2U, 80U, 0U, 2U, 0U, LOOP_PC, 1U, 0x85U);
    f.cpu.reg.F = MB_FLAG_Z;
    if (!wait_rejected("JR flags", &f, 0U)) return 1;
    resume_setup(&f, 2U, 80U, 0U, 2U, 0U, LOOP_PC, 1U, 0x85U);
    f.cpu.FMC_MODE = MB_FMC_MODE_ADD_r8;
    if (!wait_rejected("JR deferred flags", &f, 0U)) return 1;
    FRESH();
    {
        const struct fixture before = f;
        word pending = 0U, steps = 0xBEEFU, groups = 0xFACEU;
        snapshot_memory();
        if (picogb_hram_wait_try(NULL, &f.ppu, &f.fabric, &pending, &steps, &groups) ||
            picogb_hram_wait_try(&f.cpu, NULL, &f.fabric, &pending, &steps, &groups) ||
            picogb_hram_wait_try(&f.cpu, &f.ppu, NULL, &pending, &steps, &groups) ||
            picogb_hram_wait_try(&f.cpu, &f.ppu, &f.fabric, NULL, &steps, &groups) ||
            pending || steps != 0xBEEFU || groups != 0xFACEU ||
            memcmp(&before, &f, sizeof(f)) || !memory_unchanged()) return 1;
    }
    if (!multiple_groups || !capped || !pending_at_cap || !frames || !irqs
#if defined(KNOVGBC_PPU_EVENT_COALESCE)
        || !coalesced_groups
#endif
        ) {
        fprintf(stderr, "missing coverage: multi=%u cap=%u pendingcap=%u frame=%u IRQ=%u\n",
            multiple_groups, capped, pending_at_cap, frames, irqs);
        return 1;
    }
    printf("hram_wait_contract: PASS (%u cases + NULL guards; "
        "multi=%u cap=%u pendingcap=%u frame=%u IRQ=%u",
        cases, multiple_groups, capped, pending_at_cap, frames, irqs);
#if defined(KNOVGBC_PPU_EVENT_COALESCE)
    printf(" coalesced=%u extended_caps=%u candidate_groups_total=%llu"
        " edge141=%u edge142=%u edge143=%u edge_stat=%u edge_sched=%u",
        coalesced_groups, extended_caps, candidate_groups_total,
        edge141, edge142, edge143, edge_stat, edge_sched);
#endif
    puts(")");
    return 0;
}
#endif
