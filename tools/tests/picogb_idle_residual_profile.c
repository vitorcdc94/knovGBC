/* Host-only profile of interpreter work left by the kmvpi029 idle shortcut.
 * Includes the unchanged smoke harness and wraps only its mb_exec calls. */
#include <stdint.h>
#include <stdio.h>
#include "microcode.h"
#include "fabric.h"
#include "ppu.h"

static word residual_mb_exec(mb_state *mb);
#define mb_exec residual_mb_exec
#define main smoke_main
#include "picogb_smoke.c"
#undef main
#undef mb_exec

struct residual_counts {
    uint64_t calls, cycles, timer_off, zero, irq_pending, ime_off;
    uint64_t near_4, near_8, near_12, near_28, far, crosses;
};
static struct residual_counts residual[5]; /* F0, A7, JR Z, other, IRQ */
static uint64_t residual_pc[0x10000];
static uint64_t residual_opcode[256];
static uint64_t residual_boundary_ops[3];

static int residual_pattern(const mb_state *mb, word *operand)
{
    word back, start, offset;
    const r8 *rom = mb->mi->ROM;
    if (mb->IR.raw == 0x00F0U) back = 1U;
    else if (mb->IR.raw == 0x00A7U) back = 3U;
    else if (mb->IR.raw == 0x0028U) back = 4U;
    else return -1;
    if (mb->PC < back || rom == NULL) return -1;
    start = mb->PC - back;
    if (start <= 0x3FFBU) offset = start;
    else if (start >= 0x4000U && start <= 0x7FFBU &&
            mb->mi->BANK_ROM < mb->mi->N_ROM)
        offset = (mb->mi->BANK_ROM << 14) | (start & 0x3FFFU);
    else return -1;
    if (rom[offset] != 0xF0U || rom[offset + 2U] != 0xA7U ||
            rom[offset + 3U] != 0x28U || rom[offset + 4U] != 0xFBU ||
            rom[offset + 1U] < 0x80U || rom[offset + 1U] >= 0xFFU)
        return -1;
    *operand = rom[offset + 1U];
    return back == 1U ? 0 : (back == 3U ? 1 : 2);
}

static word residual_mb_exec(mb_state *mb)
{
    struct pgf_userdata_t *ud = mb->mi->userdata;
    word operand = 0U;
    int kind = residual_pattern(mb, &operand);
    word ticks = ud->ppu->next_update_ticks;
    word cycles;
    int lcd_on = (ud->ppu->rLCDC & 0x80U) != 0U;
    struct residual_counts *c;
    if (mb->IME && mbh_irq_get_pending(mb)) kind = 4;
    if (kind < 0) kind = 3;
    c = &residual[kind];
    ++c->calls;
    if (!(ud->TIMER_CNT & 4U)) ++c->timer_off;
    if (kind < 3 && mb->mi->HRAM[operand] == 0U) ++c->zero;
    if (kind == 0 && mb->mi->HRAM[operand] == 0U &&
            !mb->HALTING && !(mb->IME == 0U && mb->IME_ASK != 0U) &&
            !(mb->IME != 0U && mbh_irq_get_pending(mb)) &&
            !(ud->TIMER_CNT & 4U) && lcd_on && !ud->ppu->_redrawed &&
            ticks > 0U && ticks <= 28U) {
        ++residual_boundary_ops[ticks <= 12U ? 0 : ticks <= 16U ? 1 : 2];
    }
    if (mbh_irq_get_pending(mb)) ++c->irq_pending;
    if (!mb->IME) ++c->ime_off;
    if (lcd_on && ticks <= 4U) ++c->near_4;
    else if (lcd_on && ticks <= 8U) ++c->near_8;
    else if (lcd_on && ticks <= 12U) ++c->near_12;
    else if (lcd_on && ticks <= 28U) ++c->near_28;
    else ++c->far;
    if (kind != 4) {
        ++residual_pc[(word)(mb->PC - 1U) & 0xFFFFU];
        ++residual_opcode[mb->IR.low];
    }
    cycles = mb_exec(mb);
    c->cycles += cycles;
    if (lcd_on && cycles * 4U >= ticks) ++c->crosses;
    return cycles;
}

int main(int argc, char **argv)
{
    const char *names[] = {"F0", "A7", "JRZ", "OTHER", "IRQ"};
    int result = smoke_main(argc, argv);
    unsigned i;
    for (i = 0; i < 5U; ++i) {
        const struct residual_counts *c = &residual[i];
        printf("residual %s calls=%llu cycles=%llu timer_off=%llu hram_zero=%llu pending=%llu ime_off=%llu distance_1_4=%llu distance_5_8=%llu distance_9_12=%llu distance_13_28=%llu far_or_lcd_off=%llu crosses=%llu\n",
            names[i], (unsigned long long)c->calls, (unsigned long long)c->cycles,
            (unsigned long long)c->timer_off, (unsigned long long)c->zero,
            (unsigned long long)c->irq_pending, (unsigned long long)c->ime_off,
            (unsigned long long)c->near_4, (unsigned long long)c->near_8,
            (unsigned long long)c->near_12, (unsigned long long)c->near_28,
            (unsigned long long)c->far, (unsigned long long)c->crosses);
    }
    for (i = 0; i < 12U; ++i) {
        unsigned pc, best = 0;
        for (pc = 0; pc < 0x10000U; ++pc)
            if (residual_pc[pc] > residual_pc[best]) best = pc;
        printf("residual_pc rank=%u pc=%04X calls=%llu\n", i + 1U, best,
            (unsigned long long)residual_pc[best]);
        residual_pc[best] = 0;
    }
    printf("residual_boundary one=%llu two=%llu three=%llu total_mb_exec_avoidable=%llu\n",
        (unsigned long long)residual_boundary_ops[0],
        (unsigned long long)residual_boundary_ops[1],
        (unsigned long long)residual_boundary_ops[2],
        (unsigned long long)(residual_boundary_ops[0] +
            2U * residual_boundary_ops[1] + 3U * residual_boundary_ops[2]));
    return result;
}
