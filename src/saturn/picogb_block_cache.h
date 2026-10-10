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
 * Conservative ROM block cache for the Saturn MVP.
 *
 * PicoGB normally executes one prefetched SM83 opcode per mb_exec() call.
 * This module recognizes a small, general subset of immutable ROM code and
 * turns a short straight-line sequence into an SH-2 call chain.  The chain
 * still commits the Game Boy devices after every guest instruction, so it
 * never trades timer/PPU accuracy for batching.
 *
 * Version one is intentionally MBC0-only.  Its exclusion of RAM code,
 * mapper writes, branches and I/O is a correctness boundary, not a
 * Tetris-specific optimization.  Unsupported code always falls back to
 * PicoGB's complete interpreter.
 */

#include "microcode.h"

#include <stdint.h>

enum {
    PICOGB_BLOCK_CACHE_SLOTS = 32U,
    PICOGB_BLOCK_MAX_OPERATIONS = 4U,
    PICOGB_BLOCK_MIN_OPERATIONS = 2U,
    /* The SH-2's unified cache is 4 KiB.  Keep the generated code arena at
     * that size and flush it as a whole when full instead of creating an
     * unbounded second instruction working set. */
    PICOGB_BLOCK_NATIVE_ARENA_WORDS = 2048U
};

typedef void (*picogb_block_handler_t)(mb_state *mb);
typedef void (*picogb_block_native_t)(mb_state *mb);
typedef void (*picogb_block_commit_t)(void *userdata, word cycles);

struct picogb_block_runtime {
    void *userdata;
    picogb_block_commit_t commit;
    word instructions;
    word cycles;
};

struct picogb_block_entry {
    const r8 *rom;
    r16 guest_pc;
    r8 first_opcode;
    r8 count;
    r16 total_cycles;
    r8 cycles[PICOGB_BLOCK_MAX_OPERATIONS];
    picogb_block_handler_t handlers[PICOGB_BLOCK_MAX_OPERATIONS];
    picogb_block_native_t native;
    r16 native_offset;
    r16 native_words;
    r8 valid;
};

struct picogb_block_cache {
    struct picogb_block_entry entries[PICOGB_BLOCK_CACHE_SLOTS];
    r16 native_words_used;
    r16 native_arena[PICOGB_BLOCK_NATIVE_ARENA_WORDS]
        __attribute__((aligned(16)));
};

void picogb_block_cache_init(struct picogb_block_cache *cache);

/*
 * Attempt one cached straight-line block. `safe_ppu_ticks` is the number of
 * PPU ticks remaining before the next observable PPU event.  The routine
 * executes only when the whole block ends strictly before that boundary.
 * It returns nonzero only after it has committed every executed guest
 * instruction through runtime->commit.
 */
int picogb_block_cache_try_execute(struct picogb_block_cache *cache,
    mb_state *mb, struct picogb_block_runtime *runtime,
    uint32_t safe_ppu_ticks);

/* Target-side sanity check for generated SH-2 code, cache purge and ABI. */
int picogb_block_cache_selftest(struct picogb_block_cache *cache,
    mb_state *prototype);
