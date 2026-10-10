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
#pragma once

/*
 * Portable basic-block executor for the Saturn MVP.
 *
 * This is deliberately separate from picogb_block_cache.{c,h}.  The older
 * experiment emits SH-2 instructions into a writable arena; this module
 * stores only a compact, immutable description of a few linear SM83
 * instructions and interprets that description in ordinary C.  It never
 * writes executable memory, changes cache policy, or uses the Slave SH-2.
 *
 * The accepted subset is intentionally conservative: immutable MBC0 ROM,
 * no branches, no I/O, no memory-indirect register operations and no opcode
 * that changes interrupt state.  Every other instruction falls straight
 * through to PicoGB's complete mb_exec() interpreter.
 */

#include "microcode.h"

#include <stdint.h>

enum {
    PICOGB_BLOCK_EXECUTOR_CACHE_SLOTS = 32U,
#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
    PICOGB_BLOCK_EXECUTOR_MAX_OPERATIONS = 8U,
#else
    PICOGB_BLOCK_EXECUTOR_MAX_OPERATIONS = 4U,
#endif
    PICOGB_BLOCK_EXECUTOR_MIN_OPERATIONS = 2U
};

enum picogb_block_executor_kind {
    PICOGB_BLOCK_EXECUTOR_NOP = 0,
    PICOGB_BLOCK_EXECUTOR_LD_R_R,
    PICOGB_BLOCK_EXECUTOR_LD_R_N,
    PICOGB_BLOCK_EXECUTOR_INC_DEC_R,
    PICOGB_BLOCK_EXECUTOR_INC_DEC_R16,
    PICOGB_BLOCK_EXECUTOR_LD_R16_N,
    PICOGB_BLOCK_EXECUTOR_ADD_HL_R16,
    PICOGB_BLOCK_EXECUTOR_ALU_R,
    PICOGB_BLOCK_EXECUTOR_LDH_F0_HRAM
};

typedef void (*picogb_block_executor_commit_t)(void *userdata, word cycles);

struct picogb_block_executor_runtime {
    void *userdata;
    picogb_block_executor_commit_t commit;
    word instructions;
    word cycles;
};

struct picogb_block_executor_operation {
    r8 opcode;
    r8 kind;
    r8 cycles;
    r8 bytes;
#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
    /* Immediate bytes are immutable for the admitted MBC0 ROM mapping. */
    r16 immediate;
#endif
};

struct picogb_block_executor_entry {
    const r8 *rom;
    r16 guest_pc;
    r16 total_cycles;
    r8 first_opcode;
    r8 count;
    r8 valid;
    r8 reserved;
#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
    /* Address of the one opcode fetch needed after the whole block. */
    r16 end_pc;
#endif
    struct picogb_block_executor_operation
        operations[PICOGB_BLOCK_EXECUTOR_MAX_OPERATIONS];
};

struct picogb_block_executor_cache {
    struct picogb_block_executor_entry
        entries[PICOGB_BLOCK_EXECUTOR_CACHE_SLOTS];
};

void picogb_block_executor_cache_init(struct picogb_block_executor_cache *cache);

/*
 * Execute one cached straight-line block, returning nonzero only if every
 * instruction in that block was executed and its aggregate device time was
 * committed. `safe_ppu_ticks` is the number of PPU ticks remaining before
 * the next visible PPU event; the block is admitted only when it ends
 * strictly before that boundary. With KNOVGBC_BLOCK_IMMUTABLE_WINDOW, the
 * caller also includes the next timer overflow in this deadline. Otherwise
 * it must keep the timer disabled. This permits one aggregate device commit.
 */
int picogb_block_executor_try_execute(struct picogb_block_executor_cache *cache,
    mb_state *mb, struct picogb_block_executor_runtime *runtime,
    uint32_t safe_ppu_ticks);
