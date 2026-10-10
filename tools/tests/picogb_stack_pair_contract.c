/* Boundary and byte-order contract for two-byte SM83 stack operations.
 * Build this unchanged source with and without KNOVGBC_STACK_PAIR_WRAM, then
 * compare every printed state/memory digest. This covers fallback boundaries
 * which the Tetris smoke trace may not visit. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mi.h"
#include "microcode.h"

static r8 rom[0x8000];
static r8 wram[0x8000];
static r8 vram[0x4000];
static r8 sram[0x20000];
static r8 hram[0x100];
static r8 oam[0x100];
static r8 io[0x80];

static word
contract_io(void *userdata, word address, word data, word type)
{
    (void)userdata;
    if (type == MB_TYPE_WRITE) {
        io[address & 0x7FU] = (r8)data;
    }
    return io[address & 0x7FU];
}

static word
contract_rom(void *userdata, word address, word data, word type)
{
    (void)userdata;
    (void)address;
    (void)data;
    (void)type;
    return 0U;
}

static r8 *
byte_at(word address)
{
    address &= 0xFFFFU;
    if (address < 0x8000U) return &rom[address];
    if (address < 0xA000U) return &vram[address & 0x1FFFU];
    if (address < 0xC000U) return &sram[0x2000U | (address & 0x1FFFU)];
    if (address < 0xD000U) return &wram[address & 0x0FFFU];
    if (address < 0xE000U) return &wram[0x1000U | (address & 0x0FFFU)];
    if (address < 0xF000U) return &wram[address & 0x0FFFU];
    if (address < 0xFE00U) return &wram[0x1000U | (address & 0x0FFFU)];
    if (address < 0xFF00U) return &oam[address & 0xFFU];
    return address >= 0xFF80U ? &hram[address & 0xFFU] : &io[address & 0x7FU];
}

static uint64_t
hash_bytes(uint64_t hash, const r8 *bytes, size_t count)
{
    size_t index;
    for (index = 0U; index < count; ++index)
        hash = (hash ^ bytes[index]) * UINT64_C(1099511628211);
    return hash;
}

static uint64_t
memory_hash(void)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    hash = hash_bytes(hash, rom, sizeof(rom));
    hash = hash_bytes(hash, wram, sizeof(wram));
    hash = hash_bytes(hash, vram, sizeof(vram));
    hash = hash_bytes(hash, sram, sizeof(sram));
    hash = hash_bytes(hash, hram, sizeof(hram));
    hash = hash_bytes(hash, oam, sizeof(oam));
    return hash_bytes(hash, io, sizeof(io));
}

static void
setup(mb_state *mb, struct mi_dispatch *mi, word opcode, word sp)
{
    memset(mb, 0, sizeof(*mb));
    memset(mi, 0, sizeof(*mi));
    memset(rom, 0, sizeof(rom));
    memset(wram, 0x51, sizeof(wram));
    memset(vram, 0x52, sizeof(vram));
    memset(sram, 0x53, sizeof(sram));
    memset(hram, 0x54, sizeof(hram));
    memset(oam, 0x55, sizeof(oam));
    memset(io, 0x56, sizeof(io));

    mi->ROM = rom;
    mi->WRAM = wram;
    mi->VRAM = vram;
    mi->SRAM = sram;
    mi->HRAM = hram;
    mi->OAM = oam;
    mi->BANK_ROM = 1U;
    mi->BANK_WRAM = 1U;
    mi->BANK_VRAM = 0U;
    mi->BANK_SRAM = 0U;
    mi->N_ROM = 2U;
    mi->N_SRAM = 1U;
    mi->dispatch_IO = contract_io;
    mi->dispatch_ROM = contract_rom;

    mb->mi = mi;
    micache_invalidate(&mb->micache);
    mb->PC = 0x0201U; /* The opcode has already been prefetched. */
    mb->IR.low = (r8)opcode;
    mb->SP = (r16)sp;
    mb->reg.BC = 0x5AA5U;
    rom[0x0201U] = 0x30U; /* CALL operand: target 0230. */
    rom[0x0202U] = 0x02U;
    rom[0x0230U] = 0x00U;
}

static int
run_case(const char *name, word opcode, word sp, int writes_stack)
{
    mb_state mb;
    struct mi_dispatch mi;
    const word first = writes_stack ? (sp - 1U) & 0xFFFFU : sp;
    const word second = writes_stack ? (sp - 2U) & 0xFFFFU : (sp + 1U) & 0xFFFFU;
    const int fixed_wram = (first >= 0xC000U) && (first <= 0xCFFFU) &&
        (second >= 0xC000U) && (second <= 0xCFFFU);
    word cycles;
    word expected_sp = (sp + (writes_stack ? 0xFFFEU : 2U)) & 0xFFFFU;
    word expected_word = opcode == 0xC5U ? 0x5AA5U :
        opcode == 0xCDU ? 0x0203U :
        opcode == 0xC7U ? 0x0201U : 0x0230U;

    setup(&mb, &mi, opcode, sp);
    if (!writes_stack) {
        *byte_at(first) = 0x30U;
        *byte_at(second) = 0x02U;
    }
    cycles = mb_exec(&mb);

    if (cycles == 0U || mb.SP != expected_sp ||
        (fixed_wram && writes_stack &&
            (*byte_at(first) != (r8)(expected_word >> 8) ||
             *byte_at(second) != (r8)expected_word)) ||
        (fixed_wram && opcode == 0xC1U && mb.reg.BC != 0x0230U) ||
        (fixed_wram && opcode == 0xC9U && mb.PC != 0x0231U) ||
        (opcode == 0xCDU && mb.PC != 0x0231U) ||
        (opcode == 0xC7U && mb.PC != 0x0001U)) {
        fprintf(stderr, "FAIL %s opcode=%02X sp=%04X cycles=%u result_sp=%04X pc=%04X bytes=%02X,%02X\n",
            name, (unsigned)opcode, (unsigned)sp, (unsigned)cycles,
            (unsigned)mb.SP, (unsigned)mb.PC,
            (unsigned)*byte_at(first), (unsigned)*byte_at(second));
        return 1;
    }

    printf("case=%s opcode=%02X sp=%04X fixed_wram=%u cycles=%u pc=%04X new_sp=%04X bc=%04X af=%02X%02X ie=%02X ir=%02X memory=%016llx\n",
        name, (unsigned)opcode, (unsigned)sp, (unsigned)fixed_wram, (unsigned)cycles,
        (unsigned)mb.PC, (unsigned)mb.SP, (unsigned)mb.reg.BC,
        (unsigned)mb.reg.A, (unsigned)mb.reg.F, (unsigned)mb.IE,
        (unsigned)mb.IR.low, (unsigned long long)memory_hash());
    return 0;
}

int
main(void)
{
    static const word boundaries[] = {
        0xC001U, 0xC002U, 0xCFFEU, 0xCFFFU, 0xD000U, 0xD001U,
        0xFFFEU, 0x0000U, 0x0001U
    };
    static const struct {
        const char *name;
        word opcode;
        int writes_stack;
    } operations[] = {
        { "push_bc", 0xC5U, 1 },
        { "pop_bc", 0xC1U, 0 },
        { "call", 0xCDU, 1 },
        { "ret", 0xC9U, 0 },
        { "rst", 0xC7U, 1 }
    };
    size_t boundary;
    size_t operation;

    for (operation = 0U; operation < sizeof(operations) / sizeof(operations[0]); ++operation) {
        for (boundary = 0U; boundary < sizeof(boundaries) / sizeof(boundaries[0]); ++boundary) {
            if (run_case(operations[operation].name, operations[operation].opcode,
                    boundaries[boundary], operations[operation].writes_stack))
                return 1;
        }
    }
    return 0;
}
