/* Host-only profile: real mb_exec work remaining with baseline 032 enabled.
 * Never reads through emulated callbacks and does not alter emulated state. */
#include <stdint.h>
#include <stdio.h>
#include "microcode.h"
#include "fabric.h"
#include "ppu.h"

static word profile_mb_exec(mb_state *mb);
static unsigned profile_frames;
#define KNOVGBC_SMOKE_FRAME_HOOK(frame_no) (profile_frames = (frame_no))
#define mb_exec profile_mb_exec
#define main smoke_main
#include "picogb_smoke.c"
#undef main
#undef mb_exec
#undef KNOVGBC_SMOKE_FRAME_HOOK

enum { PROFILE_PHASES = 2, PROFILE_EDGES = 1024, PROFILE_BYTES = 80 };
static uint64_t profile_calls[PROFILE_PHASES];
static uint64_t profile_irq[PROFILE_PHASES];
static uint64_t profile_pcs[PROFILE_PHASES][0x10000];
static uint64_t profile_ops[PROFILE_PHASES][256];
static unsigned char profile_code[0x10000][16];
static unsigned char profile_code_valid[0x10000];
static struct edge {
    unsigned pc, target, op, bank, length;
    uint64_t count[PROFILE_PHASES];
    unsigned char bytes[PROFILE_BYTES];
} profile_edges[PROFILE_EDGES];
static unsigned profile_num_edges;
static struct countdown {
    uint64_t dec, start40, timer_on, ime_on, ime_ask, pending, redrawn, lcd_off;
    uint64_t within4, within16, after16;
    uint64_t ppu_mode[4];
} profile_countdown[PROFILE_PHASES];

static unsigned char profile_peek(const mb_state *mb, unsigned address)
{
    address &= 0xFFFFU;
    if (address < 0x4000U) return mb->mi->ROM[address];
    if (address < 0x8000U && mb->mi->BANK_ROM < mb->mi->N_ROM)
        return mb->mi->ROM[(mb->mi->BANK_ROM << 14) | (address & 0x3FFFU)];
    if (address >= 0xFF80U && address < 0xFFFFU)
        return mb->mi->HRAM[address & 0xFFU];
    return 0xFFU;
}

static word profile_mb_exec(mb_state *mb)
{
    unsigned phase = profile_frames >= 480U;
    unsigned pc = (mb->PC - 1U) & 0xFFFFU;
    unsigned op = mb->IR.low;
    unsigned bank = pc < 0x4000U ? 0U : mb->mi->BANK_ROM;
    unsigned pending = mb->IME && mbh_irq_get_pending(mb);
    unsigned target, i;
    word result;
    if (!pending && op == 0x3DU && profile_peek(mb, pc + 1U) == 0x20U &&
            profile_peek(mb, pc + 2U) == 0xFDU) {
        struct countdown *c = &profile_countdown[phase];
        struct pgf_userdata_t *ud = mb->mi->userdata;
        ++c->dec;
        if (mb->reg.A == 40U) ++c->start40;
        if (ud->TIMER_CNT & 4U) ++c->timer_on;
        if (mb->IME) ++c->ime_on;
        if (mb->IME_ASK) ++c->ime_ask;
        if (mbh_irq_get_pending(mb)) ++c->pending;
        if (ud->ppu->_redrawed) ++c->redrawn;
        if (!(ud->ppu->rLCDC & 0x80U)) ++c->lcd_off;
        if (ud->ppu->next_update_ticks <= 4U) ++c->within4;
        else if (ud->ppu->next_update_ticks <= 16U) ++c->within16;
        else ++c->after16;
        ++c->ppu_mode[ud->ppu->rSTAT & 3U];
    }
    ++profile_calls[phase];
    if (pending) ++profile_irq[phase];
    else {
        ++profile_pcs[phase][pc];
        ++profile_ops[phase][op];
        if (!profile_code_valid[pc]) {
            for (i = 0; i < 16U; ++i)
                profile_code[pc][i] = profile_peek(mb, pc + i);
            profile_code_valid[pc] = 1;
        }
    }
    result = mb_exec(mb);
    target = (mb->PC - 1U) & 0xFFFFU;
    if (!pending && target <= pc && pc - target < 64U &&
        (op == 0x18U || op == 0x20U || op == 0x28U || op == 0x30U ||
         op == 0x38U || op == 0xC2U || op == 0xCAU || op == 0xD2U ||
         op == 0xDAU || op == 0xC3U)) {
        for (i = 0; i < profile_num_edges; ++i)
            if (profile_edges[i].pc == pc && profile_edges[i].target == target &&
                profile_edges[i].bank == bank) break;
        if (i == profile_num_edges && i < PROFILE_EDGES) {
            unsigned j;
            struct edge *edge = &profile_edges[profile_num_edges++];
            edge->pc = pc; edge->target = target; edge->op = op; edge->bank = bank;
            edge->length = pc - target + 3U;
            for (j = 0; j < edge->length; ++j)
                edge->bytes[j] = profile_peek(mb, target + j);
        }
        if (i < PROFILE_EDGES) ++profile_edges[i].count[phase];
    }
    return result;
}

int main(int argc, char **argv)
{
    int result = smoke_main(argc, argv);
    unsigned phase, rank, i;
    printf("profile_frames=%u\n", profile_frames);
    for (phase = 0; phase < PROFILE_PHASES; ++phase) {
        printf("phase=%s calls=%llu irq_dispatch=%llu\n", phase ? "after480" : "before480",
            (unsigned long long)profile_calls[phase],
            (unsigned long long)profile_irq[phase]);
        {
            const struct countdown *c = &profile_countdown[phase];
            printf("countdown dec=%llu start40=%llu timer_on=%llu ime_on=%llu ime_ask=%llu pending=%llu redrawn=%llu lcd_off=%llu ticks_le4=%llu ticks_5_16=%llu ticks_gt16=%llu modes=%llu/%llu/%llu/%llu\n",
                (unsigned long long)c->dec, (unsigned long long)c->start40,
                (unsigned long long)c->timer_on, (unsigned long long)c->ime_on,
                (unsigned long long)c->ime_ask, (unsigned long long)c->pending,
                (unsigned long long)c->redrawn, (unsigned long long)c->lcd_off,
                (unsigned long long)c->within4, (unsigned long long)c->within16,
                (unsigned long long)c->after16, (unsigned long long)c->ppu_mode[0],
                (unsigned long long)c->ppu_mode[1], (unsigned long long)c->ppu_mode[2],
                (unsigned long long)c->ppu_mode[3]);
        }
        for (rank = 0; rank < 30U; ++rank) {
            unsigned best = 0;
            for (i = 0; i < 0x10000U; ++i)
                if (profile_pcs[phase][i] > profile_pcs[phase][best]) best = i;
            printf("pc=%04X count=%llu bytes=", best,
                (unsigned long long)profile_pcs[phase][best]);
            for (i = 0; i < 16U; ++i) printf("%02X ", profile_code[best][i]);
            putchar('\n');
            profile_pcs[phase][best] = 0;
        }
        for (rank = 0; rank < 15U; ++rank) {
            unsigned best = 0;
            for (i = 0; i < 256U; ++i)
                if (profile_ops[phase][i] > profile_ops[phase][best]) best = i;
            printf("op=%02X count=%llu\n", best, (unsigned long long)profile_ops[phase][best]);
            profile_ops[phase][best] = 0;
        }
        for (rank = 0; rank < 30U && rank < profile_num_edges; ++rank) {
            unsigned best = 0;
            struct edge *edge;
            for (i = 0; i < profile_num_edges; ++i)
                if (profile_edges[i].count[phase] > profile_edges[best].count[phase]) best = i;
            edge = &profile_edges[best];
            if (!edge->count[phase]) break;
            printf("edge=%04X->%04X op=%02X bank=%u count=%llu bytes=",
                edge->pc, edge->target, edge->op, edge->bank,
                (unsigned long long)edge->count[phase]);
            for (i = 0; i < edge->length; ++i) printf("%02X ", edge->bytes[i]);
            putchar('\n');
            edge->count[phase] = 0;
        }
    }
    return result;
}
