#include "picogb_opcode_profile.h"

#if defined(KNOVGBC_OPCODE_PROFILE)

struct picogb_opcode_profile_snapshot picogb_opcode_profile_live;

void
picogb_opcode_profile_reset(void)
{
    unsigned int opcode;
    unsigned int access;
    unsigned int memory_class;

    picogb_opcode_profile_live.instructions = 0U;
    picogb_opcode_profile_live.interrupts = 0U;
    picogb_opcode_profile_live.memory_accesses = 0U;
    picogb_opcode_profile_live.memory_samples = 0U;
    picogb_opcode_profile_live.memory_sample_state = 0xC3E5A8D1U;

    for (opcode = 0U; opcode < 0x100U; ++opcode) {
        picogb_opcode_profile_live.opcode[opcode] = 0U;
    }
    for (access = 0U; access < PICOGB_OPCODE_PROFILE_ACCESS_COUNT; ++access) {
        for (memory_class = 0U;
             memory_class < PICOGB_OPCODE_PROFILE_MEMORY_CLASS_COUNT;
             ++memory_class) {
            picogb_opcode_profile_live.memory[access][memory_class] = 0U;
        }
    }
}

void
picogb_opcode_profile_snapshot_take(
    struct picogb_opcode_profile_snapshot *snapshot)
{
    unsigned int opcode;
    unsigned int access;
    unsigned int memory_class;

    *snapshot = picogb_opcode_profile_live;
    picogb_opcode_profile_live.instructions = 0U;
    picogb_opcode_profile_live.interrupts = 0U;
    picogb_opcode_profile_live.memory_accesses = 0U;
    picogb_opcode_profile_live.memory_samples = 0U;
    /* Keep the pseudo-random phase running between HUD windows. Resetting
     * it here would repeatedly sample the same point of a periodic loop. */

    for (opcode = 0U; opcode < 0x100U; ++opcode) {
        picogb_opcode_profile_live.opcode[opcode] = 0U;
    }
    for (access = 0U; access < PICOGB_OPCODE_PROFILE_ACCESS_COUNT; ++access) {
        for (memory_class = 0U;
             memory_class < PICOGB_OPCODE_PROFILE_MEMORY_CLASS_COUNT;
             ++memory_class) {
            picogb_opcode_profile_live.memory[access][memory_class] = 0U;
        }
    }
}

static void
opcode_entry_insert(struct picogb_opcode_profile_opcode_entry *entries,
    uint8_t opcode, uint32_t count)
{
    unsigned int index;

    for (index = 0U; index < PICOGB_OPCODE_PROFILE_TOP_COUNT; ++index) {
        const uint32_t existing = entries[index].count;

        if ((count < existing) ||
            ((count == existing) && (opcode >= entries[index].opcode))) {
            continue;
        }

        for (unsigned int shift = PICOGB_OPCODE_PROFILE_TOP_COUNT - 1U;
             shift > index; --shift) {
            entries[shift] = entries[shift - 1U];
        }
        entries[index].opcode = opcode;
        entries[index].count = count;
        return;
    }
}

void
picogb_opcode_profile_top_opcodes(
    const struct picogb_opcode_profile_snapshot *snapshot,
    struct picogb_opcode_profile_opcode_entry
        entries[PICOGB_OPCODE_PROFILE_TOP_COUNT])
{
    unsigned int index;

    for (index = 0U; index < PICOGB_OPCODE_PROFILE_TOP_COUNT; ++index) {
        entries[index].opcode = 0U;
        entries[index].count = 0U;
    }

    for (index = 0U; index < 0x100U; ++index) {
        const uint32_t count = snapshot->opcode[index];

        if (count != 0U) {
            opcode_entry_insert(entries, (uint8_t)index, count);
        }
    }
}

static void
memory_entry_insert(struct picogb_opcode_profile_memory_entry *entries,
    uint8_t access, uint8_t memory_class, uint32_t count)
{
    const uint16_t key = (uint16_t)((access << 8) | memory_class);
    unsigned int index;

    for (index = 0U; index < PICOGB_OPCODE_PROFILE_TOP_COUNT; ++index) {
        const uint16_t existing_key = (uint16_t)((entries[index].access << 8) |
            entries[index].memory_class);
        const uint32_t existing = entries[index].count;

        if ((count < existing) || ((count == existing) &&
            (key >= existing_key))) {
            continue;
        }

        for (unsigned int shift = PICOGB_OPCODE_PROFILE_TOP_COUNT - 1U;
             shift > index; --shift) {
            entries[shift] = entries[shift - 1U];
        }
        entries[index].access = access;
        entries[index].memory_class = memory_class;
        entries[index].count = count;
        return;
    }
}

void
picogb_opcode_profile_top_memory(
    const struct picogb_opcode_profile_snapshot *snapshot,
    struct picogb_opcode_profile_memory_entry
        entries[PICOGB_OPCODE_PROFILE_TOP_COUNT])
{
    unsigned int index;
    unsigned int access;
    unsigned int memory_class;

    for (index = 0U; index < PICOGB_OPCODE_PROFILE_TOP_COUNT; ++index) {
        entries[index].access = 0U;
        entries[index].memory_class = 0U;
        entries[index].count = 0U;
    }

    for (access = 0U; access < PICOGB_OPCODE_PROFILE_ACCESS_COUNT; ++access) {
        for (memory_class = 0U;
             memory_class < PICOGB_OPCODE_PROFILE_MEMORY_CLASS_COUNT;
             ++memory_class) {
            const uint32_t count = snapshot->memory[access][memory_class];

            if (count != 0U) {
                memory_entry_insert(entries, (uint8_t)access,
                    (uint8_t)memory_class, count);
            }
        }
    }
}

uint32_t
picogb_opcode_profile_memory_access_total(
    const struct picogb_opcode_profile_snapshot *snapshot, uint8_t access)
{
    uint32_t total = 0U;

    for (unsigned int memory_class = 0U;
         memory_class < PICOGB_OPCODE_PROFILE_MEMORY_CLASS_COUNT;
         ++memory_class) {
        total += snapshot->memory[access][memory_class];
    }

    return total;
}

uint32_t
picogb_opcode_profile_memory_class_total(
    const struct picogb_opcode_profile_snapshot *snapshot,
    uint8_t memory_class)
{
    uint32_t total = 0U;

    for (unsigned int access = 0U;
         access < PICOGB_OPCODE_PROFILE_ACCESS_COUNT; ++access) {
        total += snapshot->memory[access][memory_class];
    }

    return total;
}

#endif /* KNOVGBC_OPCODE_PROFILE */
