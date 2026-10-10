#pragma once

/*
 * Diagnostic-only dynamic profile for the all-C PicoGB MVP.  This lives
 * outside the interpreter so the normal kmvpa018 executable contains none of
 * its counters or branches. Opcode counts are exact; data-memory classes are
 * pseudo-randomly sampled at roughly one in 64 accesses so a repeated SM83
 * access pattern cannot align with a fixed sampling phase.
 */

#include <stdint.h>

#if defined(KNOVGBC_OPCODE_PROFILE)

enum picogb_opcode_profile_access {
    PICOGB_OPCODE_PROFILE_ACCESS_READ = 0,
    PICOGB_OPCODE_PROFILE_ACCESS_WRITE,
    PICOGB_OPCODE_PROFILE_ACCESS_COUNT
};

enum picogb_opcode_profile_memory_class {
    PICOGB_OPCODE_PROFILE_MEMORY_ROM0 = 0,
    PICOGB_OPCODE_PROFILE_MEMORY_ROMX,
    PICOGB_OPCODE_PROFILE_MEMORY_VRAM,
    PICOGB_OPCODE_PROFILE_MEMORY_SRAM,
    PICOGB_OPCODE_PROFILE_MEMORY_WRAM,
    PICOGB_OPCODE_PROFILE_MEMORY_ECHO,
    PICOGB_OPCODE_PROFILE_MEMORY_OAM,
    PICOGB_OPCODE_PROFILE_MEMORY_IO,
    PICOGB_OPCODE_PROFILE_MEMORY_HRAM,
    PICOGB_OPCODE_PROFILE_MEMORY_IE,
    PICOGB_OPCODE_PROFILE_MEMORY_CLASS_COUNT
};

enum {
    PICOGB_OPCODE_PROFILE_TOP_COUNT = 6U,
    PICOGB_OPCODE_PROFILE_MEMORY_SAMPLE_RATE = 64U,
    PICOGB_OPCODE_PROFILE_MEMORY_SAMPLE_MASK =
        PICOGB_OPCODE_PROFILE_MEMORY_SAMPLE_RATE - 1U
};

struct picogb_opcode_profile_snapshot {
    uint32_t instructions;
    uint32_t interrupts;
    uint32_t memory_accesses;
    uint32_t memory_samples;
    uint32_t memory_sample_state;
    uint32_t opcode[0x100];
    uint32_t memory[PICOGB_OPCODE_PROFILE_ACCESS_COUNT]
                   [PICOGB_OPCODE_PROFILE_MEMORY_CLASS_COUNT];
};

struct picogb_opcode_profile_opcode_entry {
    uint8_t opcode;
    uint32_t count;
};

struct picogb_opcode_profile_memory_entry {
    uint8_t access;
    uint8_t memory_class;
    uint32_t count;
};

extern struct picogb_opcode_profile_snapshot picogb_opcode_profile_live;

static inline __attribute__((always_inline)) uint8_t
picogb_opcode_profile_memory_classify(uint16_t address)
{
    if (address < 0x4000U) {
        return PICOGB_OPCODE_PROFILE_MEMORY_ROM0;
    }
    if (address < 0x8000U) {
        return PICOGB_OPCODE_PROFILE_MEMORY_ROMX;
    }
    if (address < 0xA000U) {
        return PICOGB_OPCODE_PROFILE_MEMORY_VRAM;
    }
    if (address < 0xC000U) {
        return PICOGB_OPCODE_PROFILE_MEMORY_SRAM;
    }
    if (address < 0xE000U) {
        return PICOGB_OPCODE_PROFILE_MEMORY_WRAM;
    }
    if (address < 0xFE00U) {
        return PICOGB_OPCODE_PROFILE_MEMORY_ECHO;
    }
    if (address < 0xFF00U) {
        return PICOGB_OPCODE_PROFILE_MEMORY_OAM;
    }
    if (address < 0xFF80U) {
        return PICOGB_OPCODE_PROFILE_MEMORY_IO;
    }
    if (address < 0xFFFFU) {
        return PICOGB_OPCODE_PROFILE_MEMORY_HRAM;
    }
    return PICOGB_OPCODE_PROFILE_MEMORY_IE;
}

static inline __attribute__((always_inline)) void
picogb_opcode_profile_instruction_record(uint8_t opcode)
{
    picogb_opcode_profile_live.instructions++;
    picogb_opcode_profile_live.opcode[opcode]++;
}

static inline __attribute__((always_inline)) void
picogb_opcode_profile_interrupt_record(void)
{
    picogb_opcode_profile_live.interrupts++;
}

static inline __attribute__((always_inline)) void
picogb_opcode_profile_memory_record(uint8_t access, uint16_t address)
{
    uint32_t sample_state;

    picogb_opcode_profile_live.memory_accesses++;

    /* Counting every transaction's class would make the diagnostic's own
     * work dominant. A xorshift state gives each transaction an approximately
     * 1-in-64 chance of being classified, avoiding a fixed 64-access phase
     * that can alias a tight emulated loop. The nonzero seed is installed by
     * reset() and survives snapshots so adjacent windows vary their sample. */
    sample_state = picogb_opcode_profile_live.memory_sample_state;
    sample_state ^= sample_state << 13U;
    sample_state ^= sample_state >> 17U;
    sample_state ^= sample_state << 5U;
    picogb_opcode_profile_live.memory_sample_state = sample_state;
    if ((sample_state & PICOGB_OPCODE_PROFILE_MEMORY_SAMPLE_MASK) != 0U) {
        return;
    }

    picogb_opcode_profile_live.memory_samples++;
    picogb_opcode_profile_live.memory[access]
        [picogb_opcode_profile_memory_classify(address)]++;
}

void picogb_opcode_profile_reset(void);
void picogb_opcode_profile_snapshot_take(
    struct picogb_opcode_profile_snapshot *snapshot);
void picogb_opcode_profile_top_opcodes(
    const struct picogb_opcode_profile_snapshot *snapshot,
    struct picogb_opcode_profile_opcode_entry
        entries[PICOGB_OPCODE_PROFILE_TOP_COUNT]);
void picogb_opcode_profile_top_memory(
    const struct picogb_opcode_profile_snapshot *snapshot,
    struct picogb_opcode_profile_memory_entry
        entries[PICOGB_OPCODE_PROFILE_TOP_COUNT]);
uint32_t picogb_opcode_profile_memory_access_total(
    const struct picogb_opcode_profile_snapshot *snapshot, uint8_t access);
uint32_t picogb_opcode_profile_memory_class_total(
    const struct picogb_opcode_profile_snapshot *snapshot,
    uint8_t memory_class);

#endif /* KNOVGBC_OPCODE_PROFILE */
