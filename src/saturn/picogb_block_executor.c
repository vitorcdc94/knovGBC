/* knovGBC PicoGB library extension/snapshot; notice updated 2026-10-10.
 * This source extends/adapts the PicoGB library based on upstream commit
 * 325a12a25e2d1f4799424cc18c0e69a7810695b0.
 * The preserved local implementation predates the 2026-10-07 inventory.
 * Exact historical code-edit dates are not established by surviving records.
 * Only this source-handoff notice was changed on 2026-10-10.
 * See third_party/PicoGB/KNOVGBC_CHANGES.md for dated validation evidence;
 * report dates are not claims that every code change occurred on those dates.
 * Distributed with the modified library under GNU LGPL v2.1.
 * See third_party/PicoGB/LICENSE.txt for the library license.
 * Original upstream license and copyright notices remain applicable.
 */
#include "picogb_block_executor.h"

#include "microcode_dispatch.h"

#include <stddef.h>
#include <string.h>

/* Keep the complete executor out of the already large mb_exec() body.  The
 * per-operation switch is deliberately inlined into this one small routine;
 * device time is committed once after the block, never through a callback in
 * the guest-instruction loop. */
#define BLOCK_EXECUTOR_HOT \
    __attribute__((noinline, noclone, hot, optimize("O3")))
#define BLOCK_EXECUTOR_COLD \
    __attribute__((noinline, noclone, cold, optimize("Os")))
#define BLOCK_EXECUTOR_INLINE \
    static inline __attribute__((always_inline))

BLOCK_EXECUTOR_INLINE r8 *
block_executor_reg8(mb_state *mb, word code)
{
    /* PicoGB's decoder indices use the original little-endian order. */
    return &MB_REG8(mb, code ^ 1U);
}

#if !defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
BLOCK_EXECUTOR_INLINE void
block_executor_finish(mb_state *mb)
{
    /* raw, rather than low, also clears PicoGB's internal IR.high marker. */
    mb->IR.raw = mch_memory_fetch_PC(mb);
}
#endif

BLOCK_EXECUTOR_INLINE void
block_executor_lazy_add8(mb_state *mb, word left, word right)
{
    mb->FR1 = (r16)left;
    mb->FR2 = (r16)right;
    mb->FMC_MODE = MB_FMC_MODE_ADD_r8;
}

BLOCK_EXECUTOR_INLINE void
block_executor_lazy_adc8(mb_state *mb, word left, word right)
{
    mb->FR1 = (r16)left;
    mb->FR2 = (r16)right;
    mb->FMC_MODE = MB_FMC_MODE_ADC_r8;
}

BLOCK_EXECUTOR_INLINE void
block_executor_lazy_sub8(mb_state *mb, word left, word right)
{
    mb->FR1 = (r16)left;
    mb->FR2 = (r16)right;
    mb->FMC_MODE = MB_FMC_MODE_SUB_r8;
}

BLOCK_EXECUTOR_INLINE void
block_executor_lazy_sbc8(mb_state *mb, word left, word right)
{
    mb->FR1 = (r16)left;
    mb->FR2 = (r16)right;
    mb->FMC_MODE = MB_FMC_MODE_SBC_r8;
}

BLOCK_EXECUTOR_INLINE void
block_executor_lazy_add16(mb_state *mb, word left, word right)
{
    mb->FR1 = (r16)left;
    mb->FR2 = (r16)right;
    mb->FMC_MODE = MB_FMC_MODE_ADD_r16;
}

BLOCK_EXECUTOR_INLINE void
block_executor_alu_r(mb_state *mb, word opcode)
{
    const word operation = (opcode >> 3) & 7U;
    const word operand = *block_executor_reg8(mb, opcode & 7U);
    const word left = mb->reg.A;
    word right = operand;
    word result;
    word flags;

    switch (operation) {
    case 0U: /* ADD A,r */
        block_executor_lazy_add8(mb, left, right);
        result = left + right;
        flags = (result >> 8) ? MB_FLAG_C : 0U;
        break;
    case 1U: /* ADC A,r */
        if (mb->reg.F & MB_FLAG_C) {
            /* Lazy flags retain the unadjusted source; FMC adds carry. */
            block_executor_lazy_adc8(mb, left, right);
            ++right;
        }
        else {
            block_executor_lazy_add8(mb, left, right);
        }
        result = left + right;
        flags = (result >> 8) ? MB_FLAG_C : 0U;
        break;
    case 2U: /* SUB r */
        block_executor_lazy_sub8(mb, left, right);
        result = left - right;
        flags = (result >> 8) ? (MB_FLAG_N | MB_FLAG_C) : MB_FLAG_N;
        break;
    case 3U: /* SBC A,r */
        if (mb->reg.F & MB_FLAG_C) {
            /* As above, FR2 must be the operand before the borrow. */
            block_executor_lazy_sbc8(mb, left, right);
            ++right;
        }
        else {
            block_executor_lazy_sub8(mb, left, right);
        }
        result = left - right;
        flags = (result >> 8) ? (MB_FLAG_N | MB_FLAG_C) : MB_FLAG_N;
        break;
    case 4U: /* AND r */
        result = left & right;
        flags = MB_FLAG_H;
        mb->FMC_MODE = MB_FMC_MODE_NONE;
        break;
    case 5U: /* XOR r */
        result = left ^ right;
        flags = 0U;
        mb->FMC_MODE = MB_FMC_MODE_NONE;
        break;
    case 6U: /* OR r */
        result = left | right;
        flags = 0U;
        mb->FMC_MODE = MB_FMC_MODE_NONE;
        break;
    default: /* CP r */
        block_executor_lazy_sub8(mb, left, right);
        result = left - right;
        flags = (result >> 8) ? (MB_FLAG_N | MB_FLAG_C) : MB_FLAG_N;
        if ((result & 0xFFU) == 0U) {
            flags |= MB_FLAG_Z;
        }
        mb->reg.F = (r8)flags;
        return;
    }

    if ((result & 0xFFU) == 0U) {
        flags |= MB_FLAG_Z;
    }

    /* Match PicoGB's endian-safe combined FA write. */
    {
        hilow16_t fa;

        fa.low = (r8)result;
        fa.high = (r8)flags;
        mb->reg.hilo16[3] = fa;
    }
}

BLOCK_EXECUTOR_INLINE void
block_executor_execute_one(mb_state *mb,
    const struct picogb_block_executor_operation *operation)
{
    const word opcode = operation->opcode;

    switch ((enum picogb_block_executor_kind)operation->kind) {
    case PICOGB_BLOCK_EXECUTOR_NOP:
        break;

    case PICOGB_BLOCK_EXECUTOR_LD_R_R:
        *block_executor_reg8(mb, (opcode >> 3) & 7U) =
            *block_executor_reg8(mb, opcode & 7U);
        break;

    case PICOGB_BLOCK_EXECUTOR_LD_R_N:
        *block_executor_reg8(mb, (opcode >> 3) & 7U) =
#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
            (r8)operation->immediate;
#else
            (r8)mch_memory_fetch_PC_op_1(mb);
#endif
        break;

    case PICOGB_BLOCK_EXECUTOR_INC_DEC_R:
    {
        r8 *const register_value =
            block_executor_reg8(mb, (opcode >> 3) & 7U);
        word value = *register_value;
        word flags = mb->reg.F & MB_FLAG_C;

        if (opcode & 1U) {
            flags |= MB_FLAG_N;
            if ((value & 0x0FU) == 0U) {
                flags |= MB_FLAG_H;
            }
            value = (value - 1U) & 0xFFU;
        }
        else {
            value = (value + 1U) & 0xFFU;
            if ((value & 0x0FU) == 0U) {
                flags |= MB_FLAG_H;
            }
        }

        if (value == 0U) {
            flags |= MB_FLAG_Z;
        }
        *register_value = (r8)value;
        mb->reg.F = (r8)flags;
        mb->FMC_MODE = MB_FMC_MODE_NONE;
        break;
    }

    case PICOGB_BLOCK_EXECUTOR_INC_DEC_R16:
    {
        const word index = (opcode >> 4) & 3U;
        r16 *const register_value = (index == 3U) ? &mb->SP :
            &mb->reg.raw16[index];

        if (opcode & 8U) {
            --*register_value;
        }
        else {
            ++*register_value;
        }
        break;
    }

    case PICOGB_BLOCK_EXECUTOR_LD_R16_N:
    {
        const word index = (opcode >> 4) & 3U;
        r16 *const register_value = (index == 3U) ? &mb->SP :
            &mb->reg.raw16[index];

#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
        *register_value = operation->immediate;
#else
        *register_value = (r16)mch_memory_fetch_PC_op_2(mb);
#endif
        break;
    }

    case PICOGB_BLOCK_EXECUTOR_ADD_HL_R16:
    {
        const word index = (opcode >> 4) & 3U;
        const word right = (index == 3U) ? mb->SP : mb->reg.raw16[index];
        const word left = mb->reg.HL;
        const word result = left + right;
        word flags = mb->reg.F & MB_FLAG_Z;

        if (result >> 16) {
            flags |= MB_FLAG_C;
        }
        mb->reg.HL = (r16)result;
        mb->reg.F = (r8)flags;
        block_executor_lazy_add16(mb, left, right);
        break;
    }

    case PICOGB_BLOCK_EXECUTOR_ALU_R:
        block_executor_alu_r(mb, opcode);
        break;

    case PICOGB_BLOCK_EXECUTOR_LDH_F0_HRAM:
    {
#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
        const word haddr = operation->immediate;
#else
        const word haddr = mch_memory_fetch_PC_op_1(mb);
#endif

        /* The static decoder admits FF80-FFFE only. I/O and FFFF stay in
         * the normal microcode dispatch path. */
        mb->reg.A = mb->mi->HRAM[haddr];
        break;
    }
    }

#if !defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
    block_executor_finish(mb);
#endif
}

static int
block_executor_mbc0_rom_is_eligible(const mb_state *mb)
{
    const struct mi_dispatch *const mi = mb->mi;

    return (mi != NULL) && (mi->ROM != NULL) && (mi->ROM_MAPPER == 0U) &&
        (mi->N_ROM == 2U) && (mi->BANK_ROM == 1U);
}

static unsigned int
block_executor_slot_index(r16 guest_pc, r8 opcode)
{
    return (((unsigned int)guest_pc >> 1) ^ (unsigned int)opcode) &
        (PICOGB_BLOCK_EXECUTOR_CACHE_SLOTS - 1U);
}

static int
block_executor_decode(const r8 *rom, r16 pc,
    struct picogb_block_executor_operation *operation)
{
    const r8 opcode = rom[pc];

    operation->opcode = opcode;
    operation->kind = 0U;
    operation->cycles = 0U;
    operation->bytes = 0U;
#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
    operation->immediate = 0U;
#endif

    if (opcode == 0x00U) {
        operation->kind = PICOGB_BLOCK_EXECUTOR_NOP;
        operation->cycles = 1U;
        operation->bytes = 1U;
        return 1;
    }

    /* LD r16,n16; register pair 3 is SP. */
    if ((opcode < 0x40U) && ((opcode & 0x0FU) == 0x01U) &&
        (pc <= 0x7FFDU)) {
        operation->kind = PICOGB_BLOCK_EXECUTOR_LD_R16_N;
        operation->cycles = 3U;
        operation->bytes = 3U;
#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
        operation->immediate = (r16)(rom[pc + 1U] |
            ((word)rom[pc + 2U] << 8));
#endif
        return 1;
    }

    if ((opcode < 0x40U) && ((opcode & 0x0FU) == 0x09U)) {
        operation->kind = PICOGB_BLOCK_EXECUTOR_ADD_HL_R16;
        operation->cycles = 2U;
        operation->bytes = 1U;
        return 1;
    }

    if ((opcode < 0x40U) && (((opcode & 0x0FU) == 0x03U) ||
        ((opcode & 0x0FU) == 0x0BU))) {
        operation->kind = PICOGB_BLOCK_EXECUTOR_INC_DEC_R16;
        operation->cycles = 2U;
        operation->bytes = 1U;
        return 1;
    }

    /* INC/DEC r8, excluding (HL). */
    if ((((opcode & 0xC7U) == 0x04U) || ((opcode & 0xC7U) == 0x05U)) &&
        (((opcode >> 3) & 7U) != 6U)) {
        operation->kind = PICOGB_BLOCK_EXECUTOR_INC_DEC_R;
        operation->cycles = 1U;
        operation->bytes = 1U;
        return 1;
    }

    /* LD r8,n8, excluding (HL). */
    if ((opcode & 0xC7U) == 0x06U && (((opcode >> 3) & 7U) != 6U) &&
        (pc < 0x7FFFU)) {
        operation->kind = PICOGB_BLOCK_EXECUTOR_LD_R_N;
        operation->cycles = 2U;
        operation->bytes = 2U;
#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
        operation->immediate = rom[pc + 1U];
#endif
        return 1;
    }

    /* LD r8,r8, excluding (HL) and HALT. */
    if ((opcode >= 0x40U) && (opcode < 0x80U) && (opcode != 0x76U) &&
        ((opcode & 7U) != 6U) && (((opcode >> 3) & 7U) != 6U)) {
        operation->kind = PICOGB_BLOCK_EXECUTOR_LD_R_R;
        operation->cycles = 1U;
        operation->bytes = 1U;
        return 1;
    }

    /* ALU A,r8, excluding (HL). */
    if ((opcode >= 0x80U) && (opcode < 0xC0U) && ((opcode & 7U) != 6U)) {
        operation->kind = PICOGB_BLOCK_EXECUTOR_ALU_R;
        operation->cycles = 1U;
        operation->bytes = 1U;
        return 1;
    }

    /* LDH A,(a8) is eligible only for ordinary HRAM. */
    if ((opcode == 0xF0U) && (pc < 0x7FFFU) && (rom[pc + 1U] >= 0x80U) &&
        (rom[pc + 1U] < 0xFFU)) {
        operation->kind = PICOGB_BLOCK_EXECUTOR_LDH_F0_HRAM;
        operation->cycles = 3U;
        operation->bytes = 2U;
#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
        operation->immediate = rom[pc + 1U];
#endif
        return 1;
    }

    return 0;
}

/* Decoder misses are cached as count == 0. This keeps a hot branch or I/O
 * opcode from rebuilding the same rejected descriptor on every visit. */
BLOCK_EXECUTOR_COLD static void
block_executor_build(const mb_state *mb,
    struct picogb_block_executor_entry *entry)
{
    const r8 *const rom = mb->mi->ROM;
    const r16 start_pc = (r16)(mb->PC - 1U);
    r16 pc = start_pc;
    unsigned int count = 0U;
    unsigned int total_cycles = 0U;

    memset(entry, 0, sizeof(*entry));
    entry->rom = rom;
    entry->guest_pc = start_pc;
    entry->first_opcode = mb->IR.low;

    if ((start_pc >= 0x8000U) || (rom[start_pc] != mb->IR.low)) {
        return;
    }

    while ((count < PICOGB_BLOCK_EXECUTOR_MAX_OPERATIONS) &&
        (pc < 0x8000U)) {
        struct picogb_block_executor_operation operation;

        if (!block_executor_decode(rom, pc, &operation) ||
            ((unsigned int)pc + operation.bytes > 0x8000U)) {
            break;
        }

        entry->operations[count] = operation;
        total_cycles += operation.cycles;
        ++count;
        pc = (r16)(pc + operation.bytes);
    }

    if (count >= PICOGB_BLOCK_EXECUTOR_MIN_OPERATIONS) {
        entry->count = (r8)count;
        entry->total_cycles = (r16)total_cycles;
#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
        entry->end_pc = pc;
#endif
    }
}

static struct picogb_block_executor_entry *
block_executor_lookup_or_build(struct picogb_block_executor_cache *cache,
    const mb_state *mb)
{
    const r16 guest_pc = (r16)(mb->PC - 1U);
    const r8 opcode = mb->IR.low;
    const unsigned int slot = block_executor_slot_index(guest_pc, opcode);
    struct picogb_block_executor_entry *const entry = &cache->entries[slot];

    if (entry->valid && (entry->guest_pc == guest_pc) &&
        (entry->first_opcode == opcode) && (entry->rom == mb->mi->ROM)) {
        return entry;
    }

    block_executor_build(mb, entry);
    __asm__ volatile("" ::: "memory");
    entry->valid = 1U;
    return entry;
}

void
picogb_block_executor_cache_init(struct picogb_block_executor_cache *cache)
{
    if (cache != NULL) {
        memset(cache, 0, sizeof(*cache));
    }
}

BLOCK_EXECUTOR_HOT int
picogb_block_executor_try_execute(struct picogb_block_executor_cache *cache,
    mb_state *mb, struct picogb_block_executor_runtime *runtime,
    uint32_t safe_ppu_ticks)
{
    struct picogb_block_executor_entry *entry;
    unsigned int index;

    /* These guards mirror mb_exec()'s pre-instruction interrupt/HALT
     * contract. A rejected block must not touch CPU state or the runtime. */
    if ((cache == NULL) || (mb == NULL) || (runtime == NULL) ||
        (runtime->commit == NULL) || (mb->HALTING != 0U) ||
        (mb->IR.high != 0U) ||
#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
        ((mb->IME != 0U) && (mbh_irq_get_pending(mb) != 0U)) ||
#else
        (mb->IME != 0U) ||
#endif
#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
        ((mb->IME == 0U) && (mb->IME_ASK != 0U)) ||
#else
        (mb->IME_ASK != 0U) ||
#endif
        !block_executor_mbc0_rom_is_eligible(mb)) {
        return 0;
    }

    entry = block_executor_lookup_or_build(cache, mb);
    if ((entry == NULL) || (entry->count < PICOGB_BLOCK_EXECUTOR_MIN_OPERATIONS) ||
        ((uint32_t)entry->total_cycles * 4U >= safe_ppu_ticks)) {
        return 0;
    }

    runtime->instructions = 0U;
    runtime->cycles = 0U;

    /* MBC0 ROM is immutable. The first opcode is checked before lookup, and
     * all following bytes were decoded from that same ROM mapping, so no
     * per-operation handler pointer or dynamic-code validation is needed. */
    for (index = 0U; index < entry->count; ++index) {
        const struct picogb_block_executor_operation *const operation =
            &entry->operations[index];

        block_executor_execute_one(mb, operation);
        runtime->cycles += operation->cycles;
        ++runtime->instructions;
    }

#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
    /* Each accepted instruction is linear and cannot change its own MBC0
     * bytes.  The ordinary interpreter would fetch the next opcode after
     * every operation; only the final prefetch can affect future execution.
     * end_pc may equal 0x8000, in which case the normal memory dispatcher
     * performs that final fetch from the next region. */
    mb->PC = entry->end_pc;
    mb->IR.raw = mch_memory_fetch_PC(mb);
#endif

    /* The front end keeps each block strictly before the next PPU event and,
     * in the immutable-window variant, the next timer overflow. Accepted
     * opcodes cannot observe device time inside the block, so one aggregate
     * commit retains instruction-visible behavior. */
    runtime->commit(runtime->userdata, runtime->cycles);

    return 1;
}

#undef BLOCK_EXECUTOR_INLINE
#undef BLOCK_EXECUTOR_COLD
#undef BLOCK_EXECUTOR_HOT
