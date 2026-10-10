/* 041: compare adaptive PPU waits with an independent instruction-by-
 * instruction CPU/device oracle. Keep the established fixture and oracle,
 * but exercise deadlines spanning many visible lines and their IRQ guards. */
#define PICOGB_HRAM_WAIT_CONTRACT_NO_MAIN 1
#include "picogb_hram_wait_contract.c"

static unsigned adaptive_cases;

static int
adaptive_probe(const char *name, word entry, word ticks, word event,
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
    ++adaptive_cases;
    if (group_count != NULL) *group_count = last_candidate_groups;
    return 1;
}

int
main(void)
{
    static const word lines_to_vblank[] =
        {0U, 1U, 10U, 100U, 137U, 139U, 140U, 141U, 142U, 143U};
    static const word remaining_ticks[] =
        {1U, 2U, 3U, 4U, 7U, 15U, 16U, 17U,
         18U, 31U, 32U, 33U, 79U, 80U};
    static const word guarded_stat[] = {0x08U, 0x10U, 0x20U, 0x40U};
    unsigned edge0, edge100, edge141, edge142, edge143;
    unsigned edge_stat, edge_sched, edge_irq;
    word line, tick, entry, source;

    /* Mode 3 with 1..80 ticks left is the point where the next safe
     * observation boundary can be several lines away. Each entry phase
     * (F0, AND and JR) must stop on an instruction boundary. */
    for (line = 0U;
         line < sizeof(lines_to_vblank) / sizeof(lines_to_vblank[0]);
         ++line) {
        for (entry = 0U; entry != 3U; ++entry) {
            for (tick = 0U;
                 tick < sizeof(remaining_ticks) / sizeof(remaining_ticks[0]);
                 ++tick) {
                if (!adaptive_probe("adaptive mode-3/VBlank edge", entry,
                        remaining_ticks[tick], 1U,
                        lines_to_vblank[line], 0U, 1U, 3U, 0U,
                        255U, NULL)) return 1;
            }
        }
    }

    /* Crossing an LYC match without its STAT source enabled is allowed;
     * with a source enabled it must remain observable to the CPU. */
    for (entry = 0U; entry != 3U; ++entry) {
        if (!adaptive_probe("LYC match, source disabled", entry,
                17U, 1U, 10U, 0U, 1U, 3U, 0U, 11U, NULL)) return 1;
        for (source = 0U;
             source < sizeof(guarded_stat) / sizeof(guarded_stat[0]);
             ++source) {
            for (tick = 1U; tick <= 80U; tick += 7U) {
                if (!adaptive_probe("enabled STAT source", entry, tick,
                        1U, 10U, guarded_stat[source], 1U, 3U,
                        0U, 11U, NULL)) return 1;
            }
        }
        if (!adaptive_probe("scheduled STAT, IME on", entry, 17U,
                1U, 10U, 0U, 1U, 2U, 2U, 255U, NULL) ||
            !adaptive_probe("scheduled STAT, IME off", entry, 17U,
                1U, 10U, 0U, 0U, 2U, 2U, 255U, NULL) ||
            !adaptive_probe("scheduled VBlank IRQ", entry, 17U,
                1U, 10U, 0U, 1U, 1U, 1U, 255U, NULL)) return 1;
    }

    /* Stable probes expose the grouping decision to the shell comparison
     * against 040. State and pixel correctness above always comes from the
     * independent oracle, regardless of the group count. */
    if (!adaptive_probe("line 0 grouping", 0U, 17U, 1U,
            0U, 0U, 1U, 3U, 0U, 255U, &edge0) ||
        !adaptive_probe("line 100 grouping", 0U, 17U, 1U,
            100U, 0U, 1U, 3U, 0U, 255U, &edge100) ||
        !adaptive_probe("line 141 grouping", 0U, 17U, 1U,
            141U, 0U, 1U, 3U, 0U, 255U, &edge141) ||
        !adaptive_probe("line 142 grouping", 0U, 17U, 1U,
            142U, 0U, 1U, 3U, 0U, 255U, &edge142) ||
        !adaptive_probe("line 143 VBlank guard", 0U, 17U, 1U,
            143U, 0U, 1U, 3U, 0U, 255U, &edge143) ||
        !adaptive_probe("STAT guard grouping", 0U, 17U, 1U,
            10U, 0x08U, 1U, 3U, 0U, 255U, &edge_stat) ||
        !adaptive_probe("scheduled IRQ grouping", 0U, 17U, 1U,
            10U, 0U, 1U, 2U, 2U, 255U, &edge_sched) ||
        !adaptive_probe("scheduled VBlank IRQ grouping", 0U, 17U, 1U,
            10U, 0U, 1U, 1U, 1U, 255U, &edge_irq)) return 1;

    if (adaptive_cases == 0U || frames == 0U || irqs == 0U ||
        coalesced_groups == 0U) {
        fprintf(stderr, "adaptive coverage absent: cases=%u frames=%u "
            "irqs=%u coalesced=%u\n", adaptive_cases, frames, irqs,
            coalesced_groups);
        return 1;
    }
    printf("ppu_adaptive_contract: PASS cases=%u groups_total=%llu "
        "edge0=%u edge100=%u edge141=%u edge142=%u edge143=%u "
        "edge_stat=%u edge_sched=%u edge_irq=%u frames=%u irqs=%u\n",
        adaptive_cases, candidate_groups_total, edge0, edge100,
        edge141, edge142, edge143, edge_stat, edge_sched, edge_irq,
        frames, irqs);
    return 0;
}
