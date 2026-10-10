#include <stdint.h>
#include <stdio.h>

#include "picogb_opcode_profile.h"

static int
fail(const char *name, uint32_t expected, uint32_t actual)
{
    fprintf(stderr, "%s expected=%u actual=%u\n", name,
        (unsigned int)expected, (unsigned int)actual);
    return 1;
}

int
main(void)
{
    struct picogb_opcode_profile_snapshot snapshot;
    struct picogb_opcode_profile_opcode_entry
        opcode[PICOGB_OPCODE_PROFILE_TOP_COUNT];
    struct picogb_opcode_profile_memory_entry
        memory[PICOGB_OPCODE_PROFILE_TOP_COUNT];
    static const uint16_t boundary[
        PICOGB_OPCODE_PROFILE_MEMORY_CLASS_COUNT] = {
        0x0000U, 0x4000U, 0x8000U, 0xA000U, 0xC000U,
        0xE000U, 0xFE00U, 0xFF00U, 0xFF80U, 0xFFFFU
    };
    unsigned int index;

    picogb_opcode_profile_reset();
    for (index = 0U; index < 7U; ++index) {
        picogb_opcode_profile_instruction_record(0xA7U);
    }
    for (index = 0U; index < 4U; ++index) {
        picogb_opcode_profile_instruction_record(0xF0U);
    }
    picogb_opcode_profile_instruction_record(0xCBU);
    picogb_opcode_profile_interrupt_record();

    /* The pseudo-random gate is intentionally not phase-locked to a tight
     * loop. Use enough calls per boundary to exercise every class reliably,
     * including FF80/FFFF, without assuming an exact sample count. */
    for (index = 0U;
         index < PICOGB_OPCODE_PROFILE_MEMORY_CLASS_COUNT;
         ++index) {
        const uint8_t access = (index & 1U) ?
            PICOGB_OPCODE_PROFILE_ACCESS_WRITE :
            PICOGB_OPCODE_PROFILE_ACCESS_READ;
        const uint16_t selected = boundary[index];

        for (unsigned int sample = 0U; sample < 4096U; ++sample) {
            picogb_opcode_profile_memory_record(access, selected);
        }
    }

    picogb_opcode_profile_snapshot_take(&snapshot);
    if ((snapshot.instructions != 12U) || (snapshot.interrupts != 1U) ||
        (snapshot.memory_accesses != 40960U) ||
        (snapshot.memory_samples == 0U) ||
        (snapshot.memory_samples >= snapshot.memory_accesses)) {
        return fail("profile totals", 12U, snapshot.instructions);
    }
    if ((snapshot.opcode[0xA7U] != 7U) || (snapshot.opcode[0xF0U] != 4U) ||
        (snapshot.opcode[0xCBU] != 1U)) {
        return fail("opcode counts", 7U, snapshot.opcode[0xA7U]);
    }

    for (index = 0U; index < PICOGB_OPCODE_PROFILE_MEMORY_CLASS_COUNT;
         ++index) {
        const uint8_t access = (index & 1U) ?
            PICOGB_OPCODE_PROFILE_ACCESS_WRITE :
            PICOGB_OPCODE_PROFILE_ACCESS_READ;
        if (snapshot.memory[access][index] == 0U) {
            return fail("memory class", 1U, snapshot.memory[access][index]);
        }
    }

    picogb_opcode_profile_top_opcodes(&snapshot, opcode);
    if ((opcode[0].opcode != 0xA7U) || (opcode[0].count != 7U) ||
        (opcode[1].opcode != 0xF0U) || (opcode[1].count != 4U) ||
        (opcode[2].opcode != 0xCBU) || (opcode[2].count != 1U)) {
        return fail("opcode ranking", 0xA7U, opcode[0].opcode);
    }

    picogb_opcode_profile_top_memory(&snapshot, memory);
    if (memory[0].count == 0U) {
        return fail("memory ranking", 1U, memory[0].count);
    }
    for (index = 1U; index < PICOGB_OPCODE_PROFILE_TOP_COUNT; ++index) {
        if (memory[index - 1U].count < memory[index].count) {
            return fail("memory ranking", memory[index - 1U].count,
                memory[index].count);
        }
    }
    if (picogb_opcode_profile_memory_access_total(&snapshot,
            PICOGB_OPCODE_PROFILE_ACCESS_READ) +
        picogb_opcode_profile_memory_access_total(&snapshot,
            PICOGB_OPCODE_PROFILE_ACCESS_WRITE) != snapshot.memory_samples ||
        picogb_opcode_profile_memory_class_total(&snapshot,
            PICOGB_OPCODE_PROFILE_MEMORY_HRAM) == 0U) {
        return fail("memory totals", snapshot.memory_samples,
            picogb_opcode_profile_memory_access_total(&snapshot,
                PICOGB_OPCODE_PROFILE_ACCESS_READ));
    }

    picogb_opcode_profile_snapshot_take(&snapshot);
    if ((snapshot.instructions != 0U) || (snapshot.memory_samples != 0U)) {
        return fail("profile reset", 0U, snapshot.instructions);
    }

    puts("picogb_opcode_profile_test: PASS");
    return 0;
}
