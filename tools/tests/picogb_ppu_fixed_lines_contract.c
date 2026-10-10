/* 042/043: verify fixed four/eight-line PPU waits against real SM83
 * instructions and individual device commits. The included fixture also
 * compares CPU, PPU, timer, RAM, pixels and post-return IRQ handling. */
#define PICOGB_HRAM_WAIT_CONTRACT_NO_MAIN 1
#include "picogb_hram_wait_contract.c"

static unsigned fixed_cases;

static int
fixed_probe(const char *name, word entry, word ticks, word event,
    word scanline, word stat, word ime, word ie, word scheduled,
    word lyc, unsigned *group_count)
{
    struct fixture f;
    resume_setup(&f, entry, ticks, event, scanline, stat,
        LOOP_PC, 1U, 0x85U);
    f.cpu.IME = ime;
    f.cpu.IME_ASK = 0U;
    f.cpu.IE = ie;
    f.ppu.IF_SCHED = scheduled;
    f.ppu.rLYC = lyc;
    if (!wait_equivalent(name, &f, 0U)) return 0;
    ++fixed_cases;
    if (group_count != NULL) *group_count = last_candidate_groups;
    return 1;
}

int
main(void)
{
    static const word scanlines[] =
        {0U, 1U, 100U, 135U, 136U, 137U, 138U,
         139U, 140U, 141U, 142U, 143U};
    static const word ticks[] =
        {1U, 2U, 3U, 4U, 7U, 15U, 16U, 17U,
         18U, 31U, 32U, 33U, 79U, 80U};
    static const word stat_sources[] = {0x08U, 0x10U, 0x20U, 0x40U};
    unsigned edge0, edge135, edge136, edge139, edge140;
    unsigned edge141, edge142, edge143, edge_stat, edge_sched, edge_irq;
    word scan, tick, entry, source;

    /* All polling-loop entry phases and cycle edges, including the fixed
     * batch fallback at the last eight visible scanlines. */
    for (scan = 0U; scan < sizeof(scanlines) / sizeof(scanlines[0]); ++scan) {
        for (entry = 0U; entry != 3U; ++entry) {
            for (tick = 0U; tick < sizeof(ticks) / sizeof(ticks[0]); ++tick) {
                if (!fixed_probe("fixed-line mode-3/VBlank boundary", entry,
                        ticks[tick], 1U, scanlines[scan], 0U, 1U, 3U,
                        0U, 255U, NULL)) return 1;
            }
        }
    }

    /* A disabled LYC source may be passed. Every enabled STAT source, or
     * interrupt already scheduled, must still stop at its CPU-visible edge. */
    for (entry = 0U; entry != 3U; ++entry) {
        if (!fixed_probe("LYC source disabled", entry, 17U, 1U, 10U,
                0U, 1U, 3U, 0U, 11U, NULL)) return 1;
        for (source = 0U;
             source < sizeof(stat_sources) / sizeof(stat_sources[0]);
             ++source) {
            for (tick = 1U; tick <= 80U; tick += 7U) {
                if (!fixed_probe("enabled STAT source", entry, tick,
                        1U, 10U, stat_sources[source], 1U, 3U, 0U,
                        11U, NULL)) return 1;
            }
        }
        if (!fixed_probe("scheduled STAT IRQ, IME on", entry, 17U,
                1U, 10U, 0U, 1U, 2U, 2U, 255U, NULL) ||
            !fixed_probe("scheduled STAT IRQ, IME off", entry, 17U,
                1U, 10U, 0U, 0U, 2U, 2U, 255U, NULL) ||
            !fixed_probe("scheduled VBlank IRQ", entry, 17U,
                1U, 10U, 0U, 1U, 1U, 1U, 255U, NULL)) return 1;
    }

    /* Stable group counts make the 040/042/043 policy measurable. The
     * independent oracle above verifies correctness for every policy. */
#define EDGE(label, line, stat, ie, scheduled, out) \
    if (!fixed_probe(label, 0U, 17U, 1U, line, stat, 1U, ie, \
            scheduled, 255U, &out)) return 1
    EDGE("line 0 grouping", 0U, 0U, 3U, 0U, edge0);
    EDGE("line 135 grouping", 135U, 0U, 3U, 0U, edge135);
    EDGE("line 136 grouping", 136U, 0U, 3U, 0U, edge136);
    EDGE("line 139 grouping", 139U, 0U, 3U, 0U, edge139);
    EDGE("line 140 grouping", 140U, 0U, 3U, 0U, edge140);
    EDGE("line 141 grouping", 141U, 0U, 3U, 0U, edge141);
    EDGE("line 142 grouping", 142U, 0U, 3U, 0U, edge142);
    EDGE("line 143 VBlank guard", 143U, 0U, 3U, 0U, edge143);
    EDGE("STAT guard grouping", 10U, 0x08U, 3U, 0U, edge_stat);
    EDGE("scheduled STAT IRQ grouping", 10U, 0U, 2U, 2U, edge_sched);
    EDGE("scheduled VBlank IRQ grouping", 10U, 0U, 1U, 1U, edge_irq);
#undef EDGE

    if (fixed_cases == 0U || frames == 0U || irqs == 0U ||
        coalesced_groups == 0U) {
        fprintf(stderr, "fixed-line coverage absent: cases=%u frames=%u "
            "irqs=%u coalesced=%u\n", fixed_cases, frames, irqs,
            coalesced_groups);
        return 1;
    }
    printf("ppu_fixed_lines_contract: PASS cases=%u groups_total=%llu "
        "edge0=%u edge135=%u edge136=%u edge139=%u edge140=%u "
        "edge141=%u edge142=%u edge143=%u edge_stat=%u "
        "edge_sched=%u edge_irq=%u frames=%u irqs=%u\n",
        fixed_cases, candidate_groups_total, edge0, edge135, edge136,
        edge139, edge140, edge141, edge142, edge143, edge_stat,
        edge_sched, edge_irq, frames, irqs);
    return 0;
}
