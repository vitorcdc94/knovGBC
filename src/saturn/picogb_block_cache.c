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
#include "picogb_block_cache.h"

#include "microcode_dispatch.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#if defined(__sh__)
#include <yaul/cpu/cache.h>
#endif

/* The handlers are deliberately separate, non-inlined functions.  A block
 * holds their addresses and the SH-2 emitter calls them directly, avoiding
 * the giant mb_exec() switch while retaining PicoGB's memory/pre-fetch ABI. */
#define BLOCK_HANDLER_ATTRIBUTES \
    __attribute__((noinline, noclone, externally_visible, hot, optimize("O3")))

static struct picogb_block_runtime *active_runtime;

static inline r8 *
block_reg8(mb_state *mb, word code)
{
    /* PicoGB's internal register indices retain the original little-endian
     * decoder convention.  MB_REG8 handles the SH-2's big-endian layout. */
    return &MB_REG8(mb, code ^ 1U);
}

static inline void
block_finish(mb_state *mb)
{
    mb->IR.raw = mch_memory_fetch_PC(mb);
}

static inline void
block_lazy_add8(mb_state *mb, word left, word right)
{
    mb->FR1 = (r16)left;
    mb->FR2 = (r16)right;
    mb->FMC_MODE = MB_FMC_MODE_ADD_r8;
}

static inline void
block_lazy_adc8(mb_state *mb, word left, word right)
{
    mb->FR1 = (r16)left;
    mb->FR2 = (r16)right;
    mb->FMC_MODE = MB_FMC_MODE_ADC_r8;
}

static inline void
block_lazy_sub8(mb_state *mb, word left, word right)
{
    mb->FR1 = (r16)left;
    mb->FR2 = (r16)right;
    mb->FMC_MODE = MB_FMC_MODE_SUB_r8;
}

static inline void
block_lazy_sbc8(mb_state *mb, word left, word right)
{
    mb->FR1 = (r16)left;
    mb->FR2 = (r16)right;
    mb->FMC_MODE = MB_FMC_MODE_SBC_r8;
}

static inline void
block_lazy_add16(mb_state *mb, word left, word right)
{
    mb->FR1 = (r16)left;
    mb->FR2 = (r16)right;
    mb->FMC_MODE = MB_FMC_MODE_ADD_r16;
}

BLOCK_HANDLER_ATTRIBUTES void
picogb_block_op_nop(mb_state *mb)
{
    block_finish(mb);
}

BLOCK_HANDLER_ATTRIBUTES void
picogb_block_op_ld_r_r(mb_state *mb)
{
    const word opcode = mb->IR.low;
    const word source = opcode & 7U;
    const word destination = (opcode >> 3) & 7U;

    *block_reg8(mb, destination) = *block_reg8(mb, source);
    block_finish(mb);
}

BLOCK_HANDLER_ATTRIBUTES void
picogb_block_op_ld_r_n(mb_state *mb)
{
    const word opcode = mb->IR.low;
    const word destination = (opcode >> 3) & 7U;

    *block_reg8(mb, destination) = (r8)mch_memory_fetch_PC_op_1(mb);
    block_finish(mb);
}

BLOCK_HANDLER_ATTRIBUTES void
picogb_block_op_inc_dec_r(mb_state *mb)
{
    const word opcode = mb->IR.low;
    const word destination = (opcode >> 3) & 7U;
    r8 *const register_value = block_reg8(mb, destination);
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
    block_finish(mb);
}

BLOCK_HANDLER_ATTRIBUTES void
picogb_block_op_inc_dec_r16(mb_state *mb)
{
    const word opcode = mb->IR.low;
    const word index = (opcode >> 4) & 3U;
    r16 *const register_value = (index == 3U) ? &mb->SP :
        &mb->reg.raw16[index];

    if (opcode & 8U) {
        --*register_value;
    }
    else {
        ++*register_value;
    }

    block_finish(mb);
}

BLOCK_HANDLER_ATTRIBUTES void
picogb_block_op_ld_r16_n(mb_state *mb)
{
    const word opcode = mb->IR.low;
    const word index = (opcode >> 4) & 3U;
    r16 *const register_value = (index == 3U) ? &mb->SP :
        &mb->reg.raw16[index];

    *register_value = (r16)mch_memory_fetch_PC_op_2(mb);
    block_finish(mb);
}

BLOCK_HANDLER_ATTRIBUTES void
picogb_block_op_add_hl_r16(mb_state *mb)
{
    const word opcode = mb->IR.low;
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
    block_lazy_add16(mb, left, right);
    block_finish(mb);
}

static inline void
block_alu(mb_state *mb, word opcode, word operand)
{
    const word operation = (opcode >> 3) & 7U;
    const word left = mb->reg.A;
    word result;
    word flags;

    switch (operation) {
    case 0U: /* ADD A,r */
        block_lazy_add8(mb, left, operand);
        result = left + operand;
        flags = (result >> 8) ? MB_FLAG_C : 0U;
        break;
    case 1U: /* ADC A,r */
        if (mb->reg.F & MB_FLAG_C) {
            block_lazy_adc8(mb, left, operand);
            ++operand;
        }
        else {
            block_lazy_add8(mb, left, operand);
        }
        result = left + operand;
        flags = (result >> 8) ? MB_FLAG_C : 0U;
        break;
    case 2U: /* SUB r */
        block_lazy_sub8(mb, left, operand);
        result = left - operand;
        flags = (result >> 8) ? (MB_FLAG_N | MB_FLAG_C) : MB_FLAG_N;
        break;
    case 3U: /* SBC A,r */
        if (mb->reg.F & MB_FLAG_C) {
            block_lazy_sbc8(mb, left, operand);
            ++operand;
        }
        else {
            block_lazy_sub8(mb, left, operand);
        }
        result = left - operand;
        flags = (result >> 8) ? (MB_FLAG_N | MB_FLAG_C) : MB_FLAG_N;
        break;
    case 4U: /* AND r */
        mb->FMC_MODE = MB_FMC_MODE_NONE;
        result = left & operand;
        flags = MB_FLAG_H;
        break;
    case 5U: /* XOR r */
        mb->FMC_MODE = MB_FMC_MODE_NONE;
        result = left ^ operand;
        flags = 0U;
        break;
    case 6U: /* OR r */
        mb->FMC_MODE = MB_FMC_MODE_NONE;
        result = left | operand;
        flags = 0U;
        break;
    default: /* CP r */
        block_lazy_sub8(mb, left, operand);
        result = left - operand;
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

    mb->reg.A = (r8)result;
    mb->reg.F = (r8)flags;
}

BLOCK_HANDLER_ATTRIBUTES void
picogb_block_op_alu_r(mb_state *mb)
{
    const word opcode = mb->IR.low;
    const word source = opcode & 7U;

    block_alu(mb, opcode, *block_reg8(mb, source));
    block_finish(mb);
}

BLOCK_HANDLER_ATTRIBUTES void
picogb_block_op_ldh_f0_hram(mb_state *mb)
{
    const word haddr = mch_memory_fetch_PC_op_1(mb);

    /* The decoder has checked the embedded a8 operand.  FF00-FF7F and FFFF
     * are deliberately not eligible and continue through mb_exec(). */
    mb->reg.A = mb->mi->HRAM[haddr];
    block_finish(mb);
}

/* Called from generated code after every native handler.  It is a small
 * stable ABI: r4 holds the M-cycle count.  The active runtime is installed
 * immediately before entering a block and cleared immediately after it. */
void __attribute__((noinline, used, externally_visible))
picogb_block_native_commit(word cycles)
{
    struct picogb_block_runtime *const runtime = active_runtime;

    runtime->cycles += cycles;
    ++runtime->instructions;
    runtime->commit(runtime->userdata, cycles);
}

static int
block_mbc0_rom_is_eligible(const mb_state *mb)
{
    const struct mi_dispatch *const mi = mb->mi;

    return (mi != NULL) && (mi->ROM != NULL) && (mi->ROM_MAPPER == 0U) &&
        (mi->N_ROM == 2U) && (mi->BANK_ROM == 1U);
}

static unsigned int
block_slot_index(r16 guest_pc, r8 opcode)
{
    return (((unsigned int)guest_pc >> 1) ^ (unsigned int)opcode) &
        (PICOGB_BLOCK_CACHE_SLOTS - 1U);
}

static int
block_decode(const r8 *rom, r16 pc, picogb_block_handler_t *handler,
    r8 *instruction_bytes, r8 *cycles)
{
    const word opcode = rom[pc];

    *handler = NULL;
    *instruction_bytes = 0U;
    *cycles = 0U;

    if (opcode == 0x00U) {
        *handler = picogb_block_op_nop;
        *instruction_bytes = 1U;
        *cycles = 1U;
        return 1;
    }

    /* LD r16,n16; register pair 3 is SP. */
    if ((opcode < 0x40U) && ((opcode & 0x0FU) == 0x01U) &&
        (pc <= 0x7FFDU)) {
        *handler = picogb_block_op_ld_r16_n;
        *instruction_bytes = 3U;
        *cycles = 3U;
        return 1;
    }

    /* ADD HL,r16. */
    if ((opcode < 0x40U) && ((opcode & 0x0FU) == 0x09U)) {
        *handler = picogb_block_op_add_hl_r16;
        *instruction_bytes = 1U;
        *cycles = 2U;
        return 1;
    }

    /* INC/DEC r16. */
    if ((opcode < 0x40U) && (((opcode & 0x0FU) == 0x03U) ||
        ((opcode & 0x0FU) == 0x0BU))) {
        *handler = picogb_block_op_inc_dec_r16;
        *instruction_bytes = 1U;
        *cycles = 2U;
        return 1;
    }

    /* INC/DEC r8, excluding (HL). */
    if (((opcode & 0xC7U) == 0x04U || (opcode & 0xC7U) == 0x05U) &&
        (((opcode >> 3) & 7U) != 6U)) {
        *handler = picogb_block_op_inc_dec_r;
        *instruction_bytes = 1U;
        *cycles = 1U;
        return 1;
    }

    /* LD r8,n8, excluding (HL). */
    if (((opcode & 0xC7U) == 0x06U) && (((opcode >> 3) & 7U) != 6U) &&
        (pc < 0x7FFFU)) {
        *handler = picogb_block_op_ld_r_n;
        *instruction_bytes = 2U;
        *cycles = 2U;
        return 1;
    }

    /* LD r8,r8, excluding (HL) and HALT. */
    if ((opcode >= 0x40U) && (opcode < 0x80U) && (opcode != 0x76U) &&
        ((opcode & 7U) != 6U) && (((opcode >> 3) & 7U) != 6U)) {
        *handler = picogb_block_op_ld_r_r;
        *instruction_bytes = 1U;
        *cycles = 1U;
        return 1;
    }

    /* ALU A,r8, excluding (HL). */
    if ((opcode >= 0x80U) && (opcode < 0xC0U) && ((opcode & 7U) != 6U)) {
        *handler = picogb_block_op_alu_r;
        *instruction_bytes = 1U;
        *cycles = 1U;
        return 1;
    }

    /* LDH A,(a8) is eligible only for ordinary HRAM. */
    if ((opcode == 0xF0U) && (pc < 0x7FFFU) && (rom[pc + 1U] >= 0x80U) &&
        (rom[pc + 1U] < 0xFFU)) {
        *handler = picogb_block_op_ldh_f0_hram;
        *instruction_bytes = 2U;
        *cycles = 3U;
        return 1;
    }

    return 0;
}

static int
block_build(const mb_state *mb, struct picogb_block_entry *entry)
{
    const r8 *const rom = mb->mi->ROM;
    const r16 start_pc = (r16)(mb->PC - 1U);
    r16 pc = start_pc;
    unsigned int count = 0U;
    unsigned int total_cycles = 0U;

    if (!block_mbc0_rom_is_eligible(mb) || (start_pc >= 0x8000U) ||
        (rom[start_pc] != mb->IR.low)) {
        return 0;
    }

    memset(entry, 0, sizeof(*entry));
    entry->rom = rom;
    entry->guest_pc = start_pc;
    entry->first_opcode = mb->IR.low;

    while ((count < PICOGB_BLOCK_MAX_OPERATIONS) && (pc < 0x8000U)) {
        picogb_block_handler_t handler;
        r8 instruction_bytes;
        r8 cycles;

        if (!block_decode(rom, pc, &handler, &instruction_bytes, &cycles)) {
            break;
        }
        if ((unsigned int)pc + instruction_bytes > 0x8000U) {
            break;
        }

        entry->handlers[count] = handler;
        entry->cycles[count] = cycles;
        total_cycles += cycles;
        ++count;
        pc = (r16)(pc + instruction_bytes);
    }

    if (count < PICOGB_BLOCK_MIN_OPERATIONS) {
        return 0;
    }

    entry->count = (r8)count;
    entry->total_cycles = (r16)total_cycles;
    return 1;
}

#if defined(__sh__)
enum {
    SH2_MOVL_R4_PREDEC_R15 = 0x2F46U,
    SH2_STSL_PR_PREDEC_R15 = 0x4F22U,
    SH2_MOVL_DISP_R15_R4 = 0x54F1U,
    SH2_MOVL_DISP_PC_R0 = 0xD000U,
    SH2_JSR_R0 = 0x400BU,
    SH2_NOP = 0x0009U,
    SH2_LDSL_POSTINC_R15_PR = 0x4F26U,
    SH2_ADD_4_R15 = 0x7F04U,
    SH2_RTS = 0x000BU
};

struct block_native_patch {
    r16 word_index;
    uintptr_t target;
};

static int
block_emit_native(struct picogb_block_cache *cache,
    struct picogb_block_entry *entry)
{
    r16 words[64];
    struct block_native_patch patches[PICOGB_BLOCK_MAX_OPERATIONS * 2U];
    unsigned int word_count = 0U;
    unsigned int patch_count = 0U;
    unsigned int literal_start;
    unsigned int index;
    r16 needed_words;

    _Static_assert(sizeof(uintptr_t) == 4U, "SH-2 function address width");

    /* Preserve only PR and the caller-saved r4.  The generated function
     * itself never touches r8-r14 or r15 except for its balanced frame. */
    words[word_count++] = SH2_MOVL_R4_PREDEC_R15;
    words[word_count++] = SH2_STSL_PR_PREDEC_R15;

    for (index = 0U; index < entry->count; ++index) {
        /* Restore mb_state* for the handler: PR is at [r15], the saved r4
         * immediately above it at [r15 + 4]. */
        words[word_count++] = SH2_MOVL_DISP_R15_R4;

        /* SH-2 MOV.L @(disp,PC),Rn addresses a longword literal relative to
         * the instruction address.  Keep the instruction itself 4-byte
         * aligned, otherwise its PC-relative base is halfword-aligned and a
         * normal longword literal cannot be reached safely. */
        if (word_count & 1U) {
            words[word_count++] = SH2_NOP;
        }
        patches[patch_count].word_index = (r16)word_count;
        patches[patch_count++].target = (uintptr_t)entry->handlers[index];
        words[word_count++] = SH2_MOVL_DISP_PC_R0;
        words[word_count++] = SH2_JSR_R0;
        words[word_count++] = SH2_NOP;

        /* r4 is now the M-cycle count for the small C commit ABI. */
        words[word_count++] = (r16)(0xE400U | entry->cycles[index]);
        if (word_count & 1U) {
            words[word_count++] = SH2_NOP;
        }
        patches[patch_count].word_index = (r16)word_count;
        patches[patch_count++].target = (uintptr_t)picogb_block_native_commit;
        words[word_count++] = SH2_MOVL_DISP_PC_R0;
        words[word_count++] = SH2_JSR_R0;
        words[word_count++] = SH2_NOP;
    }

    words[word_count++] = SH2_LDSL_POSTINC_R15_PR;
    words[word_count++] = SH2_ADD_4_R15;
    words[word_count++] = SH2_RTS;
    words[word_count++] = SH2_NOP;

    if (word_count & 1U) {
        /* Literal loads require a four-byte-aligned pool.  This word follows
         * RTS and is never executed. */
        words[word_count++] = SH2_NOP;
    }
    literal_start = word_count;
    needed_words = (r16)(literal_start + patch_count * 2U);

    if (needed_words > (r16)(sizeof(words) / sizeof(words[0]))) {
        return 0;
    }
    if ((unsigned int)cache->native_words_used + needed_words >
        PICOGB_BLOCK_NATIVE_ARENA_WORDS) {
        /* FIFO recycle is safe because no block is being executed while a
         * new one is compiled.  All descriptors become invalid before the
         * arena is reused. */
        memset(cache->entries, 0, sizeof(cache->entries));
        cache->native_words_used = 0U;
    }

    for (index = 0U; index < patch_count; ++index) {
        const uintptr_t code_base = (uintptr_t)&cache->native_arena[
            cache->native_words_used];
        const uintptr_t instruction = code_base +
            (uintptr_t)patches[index].word_index * sizeof(r16);
        const uintptr_t literal = code_base +
            (uintptr_t)(literal_start + index * 2U) * sizeof(r16);
        const uintptr_t pc_base = (instruction & ~(uintptr_t)3U) + 4U;
        const uintptr_t delta = literal - pc_base;
        const uintptr_t displacement = delta >> 2;

        if ((literal < pc_base) || ((delta & 3U) != 0U) ||
            (displacement > 0xFFU)) {
            return 0;
        }
        words[patches[index].word_index] = (r16)(SH2_MOVL_DISP_PC_R0 |
            displacement);
        words[literal_start + index * 2U] =
            (r16)(patches[index].target >> 16);
        words[literal_start + index * 2U + 1U] =
            (r16)patches[index].target;
    }

    {
        const r16 offset = cache->native_words_used;
        volatile r16 *const destination = (volatile r16 *)(uintptr_t)
            (((uintptr_t)&cache->native_arena[offset]) | CPU_CACHE_THROUGH);

        cpu_cache_area_purge(&cache->native_arena[offset],
            (uint32_t)needed_words * sizeof(r16));
        for (index = 0U; index < (unsigned int)needed_words; ++index) {
            destination[index] = words[index];
        }
        __asm__ volatile("" ::: "memory");
        cpu_cache_area_purge(&cache->native_arena[offset],
            (uint32_t)needed_words * sizeof(r16));

        entry->native = (picogb_block_native_t)(void *)&cache->native_arena[
            offset];
        entry->native_offset = offset;
        entry->native_words = needed_words;
        cache->native_words_used = (r16)(offset + needed_words);
    }

    return 1;
}
#else
static int
block_emit_native(struct picogb_block_cache *cache,
    struct picogb_block_entry *entry)
{
    (void)cache;
    entry->native = NULL;
    entry->native_offset = 0U;
    entry->native_words = 0U;
    return 1;
}
#endif

void
picogb_block_cache_init(struct picogb_block_cache *cache)
{
    memset(cache, 0, sizeof(*cache));
}

static struct picogb_block_entry *
block_lookup_or_build(struct picogb_block_cache *cache, mb_state *mb)
{
    const r16 guest_pc = (r16)(mb->PC - 1U);
    const r8 opcode = mb->IR.low;
    const unsigned int slot = block_slot_index(guest_pc, opcode);
    struct picogb_block_entry *const entry = &cache->entries[slot];
    struct picogb_block_entry built;

    if (entry->valid && (entry->guest_pc == guest_pc) &&
        (entry->first_opcode == opcode) && (entry->rom == mb->mi->ROM)) {
        return entry;
    }

    if (!block_build(mb, &built) || !block_emit_native(cache, &built)) {
        return NULL;
    }

    /* block_emit_native may recycle the arena and clear descriptors.  Select
     * the slot again after emitting, then publish one complete descriptor. */
    {
        struct picogb_block_entry *const install = &cache->entries[
            block_slot_index(guest_pc, opcode)];
        *install = built;
        __asm__ volatile("" ::: "memory");
        install->valid = 1U;
        return install;
    }
}

int
picogb_block_cache_try_execute(struct picogb_block_cache *cache,
    mb_state *mb, struct picogb_block_runtime *runtime,
    uint32_t safe_ppu_ticks)
{
    struct picogb_block_entry *entry;
#if !defined(__sh__)
    unsigned int index;
#endif

    if ((cache == NULL) || (mb == NULL) || (runtime == NULL) ||
        (runtime->commit == NULL) || (mb->HALTING != 0U) ||
        (mb->IME != 0U) || (mb->IME_ASK != 0U) ||
        !block_mbc0_rom_is_eligible(mb)) {
        return 0;
    }

    entry = block_lookup_or_build(cache, mb);
    if ((entry == NULL) || !entry->valid ||
        ((uint32_t)entry->total_cycles * 4U >= safe_ppu_ticks)) {
        return 0;
    }

    runtime->instructions = 0U;
    runtime->cycles = 0U;

#if defined(__sh__)
    if (entry->native == NULL) {
        return 0;
    }
    active_runtime = runtime;
    entry->native(mb);
    active_runtime = NULL;
#else
    for (index = 0U; index < entry->count; ++index) {
        entry->handlers[index](mb);
        runtime->cycles += entry->cycles[index];
        ++runtime->instructions;
        runtime->commit(runtime->userdata, entry->cycles[index]);
    }
#endif

    return runtime->instructions != 0U;
}

#if defined(__sh__)
static void
block_selftest_commit(void *userdata, word cycles)
{
    word *const observed_cycles = userdata;

    *observed_cycles += cycles;
}
#endif

int
picogb_block_cache_selftest(struct picogb_block_cache *cache,
    mb_state *prototype)
{
#if defined(__sh__)
    struct picogb_block_entry entry;
    struct picogb_block_runtime runtime;
    mb_state test;
    r8 *program;
    r8 saved;
    word observed_cycles = 0U;
    int passed = 1;

    if ((cache == NULL) || (prototype == NULL) || (prototype->mi == NULL) ||
        (prototype->mi->HRAM == NULL)) {
        return 0;
    }

    memset(&entry, 0, sizeof(entry));
    entry.count = 1U;
    entry.cycles[0] = 1U;
    entry.handlers[0] = picogb_block_op_nop;
    if (!block_emit_native(cache, &entry) || (entry.native == NULL)) {
        return 0;
    }

    test = *prototype;
    program = &prototype->mi->HRAM[0x80U];
    saved = *program;
    *program = 0x00U;
    test.HALTING = 0U;
    test.IME = 0U;
    test.IME_ASK = 0U;
    test.PC = 0xFF80U;
    test.IR.raw = 0U;

    runtime.userdata = &observed_cycles;
    runtime.commit = block_selftest_commit;
    runtime.instructions = 0U;
    runtime.cycles = 0U;
    active_runtime = &runtime;
    entry.native(&test);
    active_runtime = NULL;

    if ((runtime.instructions != 1U) || (runtime.cycles != 1U) ||
        (observed_cycles != 1U) || (test.PC != 0xFF81U) ||
        (test.IR.low != 0x00U)) {
        passed = 0;
    }
    *program = saved;

    /* The test has intentionally occupied the first arena slot.  Reset the
     * cache so the game starts from an empty, deterministic cache. */
    picogb_block_cache_init(cache);
    return passed;
#else
    (void)cache;
    (void)prototype;
    return 1;
#endif
}
