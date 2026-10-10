/*
 * Contract regression for the instruction families implemented by the first
 * SH-2 SM83 backend.  This runs the portable PicoGB interpreter as the
 * oracle, but checks its result against the exact flag/cycle equations used
 * by the assembly source.  It catches a wrong understanding of the emulator
 * pipeline before the target-only assembly reaches Saturn hardware.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mi.h"
#include "microcode.h"
#if defined(KNOVGBC_SM83_COMPACT)
#include "picogb_sm83_compact.h"
#endif

/* The reference exposes this non-static helper from microcode.c; it is not
 * part of the normal frontend API, but is the canonical way to materialise
 * PicoGB's deferred half-carry flag in this contract test. */
word mbh_fr_get(mb_state *mb, word flags);

static word
sm83_step(mb_state *mb)
{
#if defined(KNOVGBC_SM83_COMPACT)
    return picogb_sm83_compact_exec(mb);
#else
    return mb_exec(mb);
#endif
}

static r8 rom[0x8000];
static r8 wram[0x2000];
static r8 vram[0x2000];
static r8 sram[0x2000];
static r8 hram[0x100];
static r8 oam[0x100];
static unsigned int ldh_io_reads;
static unsigned int jr_io_reads;
static word jr_io_addresses[4];
static unsigned int f0_io_reads;
static word f0_io_addresses[4];

static word
ldh_contract_io(void *userdata, word address, word data, word type)
{
    (void)userdata;
    (void)data;

    if (type == MB_TYPE_READ)
    {
        ++ldh_io_reads;
        return (word)(0x40U + address);
    }

    return 0U;
}

static word
jr_contract_io(void *userdata, word address, word data, word type)
{
    (void)userdata;
    (void)data;

    if (type == MB_TYPE_READ)
    {
        if (jr_io_reads < sizeof(jr_io_addresses) / sizeof(jr_io_addresses[0]))
            jr_io_addresses[jr_io_reads] = address;
        ++jr_io_reads;
    }

    /* The tests use zero both as the displacement and as the next opcode. */
    return 0U;
}

/* This callback makes an F0 instruction whose operand and next opcode are
 * themselves fetched from I/O observable.  It proves that the tight path
 * still relies on PicoGB's complete operand/prefetch map rather than assuming
 * that instruction bytes always come from ROM. */
static word
f0_contract_io(void *userdata, word address, word data, word type)
{
    (void)userdata;
    (void)data;

    if (type == MB_TYPE_READ)
    {
        if (f0_io_reads < sizeof(f0_io_addresses) / sizeof(f0_io_addresses[0]))
            f0_io_addresses[f0_io_reads] = address;
        ++f0_io_reads;

        if (address == 0x00U)
            return 0x85U; /* a8: direct HRAM address FF85 */
    }

    return 0x00U; /* next opcode, or an irrelevant write result */
}

static void
setup(mb_state *mb, struct mi_dispatch *mi, word opcode)
{
    memset(mb, 0, sizeof(*mb));
    memset(mi, 0, sizeof(*mi));
    memset(rom, 0, sizeof(rom));
    memset(wram, 0, sizeof(wram));
    memset(vram, 0, sizeof(vram));
    memset(sram, 0, sizeof(sram));
    memset(hram, 0, sizeof(hram));
    memset(oam, 0, sizeof(oam));

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

    mb->mi = mi;
    micache_invalidate(&mb->micache);
    mb->PC = 0x0201U; /* opcode itself has already been prefetched. */
    mb->IR.low = (r8)opcode;
    rom[0x0201U] = 0x00U; /* next opcode fetched by mb_exec(). */
}

static r8 *
opcode_reg_ptr(mb_state *mb, unsigned index)
{
    switch (index) {
    case 0U: return &mb->reg.B;
    case 1U: return &mb->reg.C;
    case 2U: return &mb->reg.D;
    case 3U: return &mb->reg.E;
    case 4U: return &mb->reg.H;
    case 5U: return &mb->reg.L;
    case 7U: return &mb->reg.A;
    default: return NULL;
    }
}

static int
fail(const char *what, unsigned opcode, unsigned a, unsigned b,
     unsigned expected, unsigned actual)
{
    fprintf(stderr, "%s op=%02X a=%02X b=%02X expected=%02X actual=%02X\n",
        what, opcode, a, b, expected, actual);
    return 1;
}

static int
test_ld_registers(void)
{
    static const unsigned regs[] = {0U, 1U, 2U, 3U, 4U, 5U, 7U};
    unsigned s, d;

    for (d = 0; d < sizeof(regs) / sizeof(regs[0]); ++d) {
        for (s = 0; s < sizeof(regs) / sizeof(regs[0]); ++s) {
            mb_state mb;
            struct mi_dispatch mi;
            const unsigned opcode = 0x40U | (regs[d] << 3) | regs[s];
            const r8 value = (r8)(0x11U + regs[s] * 19U);
            const r8 flags = 0xF0U;

            setup(&mb, &mi, opcode);
            *opcode_reg_ptr(&mb, regs[d]) = (r8)~value;
            *opcode_reg_ptr(&mb, regs[s]) = value;
            mb.reg.F = flags;
            if (sm83_step(&mb) != 1U)
                return fail("LD cycles", opcode, regs[d], regs[s], 1U, 0U);
            if (*opcode_reg_ptr(&mb, regs[d]) != value)
                return fail("LD value", opcode, regs[d], regs[s], value,
                    *opcode_reg_ptr(&mb, regs[d]));
            if (mb.reg.F != flags)
                return fail("LD flags", opcode, regs[d], regs[s], flags, mb.reg.F);
        }
    }
    return 0;
}

static int
test_inc_dec(void)
{
    static const unsigned regs[] = {0U, 1U, 2U, 3U, 4U, 5U, 7U};
    unsigned r, value, carry;

    for (r = 0; r < sizeof(regs) / sizeof(regs[0]); ++r) {
        for (value = 0; value < 256U; ++value) {
            for (carry = 0; carry < 2U; ++carry) {
                mb_state mb;
                struct mi_dispatch mi;
                r8 result;
                r8 flags;
                unsigned opcode = 0x04U | (regs[r] << 3);

                setup(&mb, &mi, opcode);
                *opcode_reg_ptr(&mb, regs[r]) = (r8)value;
                mb.reg.F = carry ? MB_FLAG_C : 0U;
                result = (r8)(value + 1U);
                flags = (r8)(carry ? MB_FLAG_C : 0U);
                if ((value & 0x0FU) == 0x0FU)
                    flags |= MB_FLAG_H;
                if (result == 0U)
                    flags |= MB_FLAG_Z;
                if (sm83_step(&mb) != 1U || *opcode_reg_ptr(&mb, regs[r]) != result ||
                    mb.reg.F != flags || mb.FMC_MODE != MB_FMC_MODE_NONE)
                    return fail("INC", opcode, value, carry, flags, mb.reg.F);

                opcode = 0x05U | (regs[r] << 3);
                setup(&mb, &mi, opcode);
                *opcode_reg_ptr(&mb, regs[r]) = (r8)value;
                mb.reg.F = carry ? MB_FLAG_C : 0U;
                result = (r8)(value - 1U);
                flags = (r8)(MB_FLAG_N | (carry ? MB_FLAG_C : 0U));
                if ((value & 0x0FU) == 0U)
                    flags |= MB_FLAG_H;
                if (result == 0U)
                    flags |= MB_FLAG_Z;
                if (sm83_step(&mb) != 1U || *opcode_reg_ptr(&mb, regs[r]) != result ||
                    mb.reg.F != flags || mb.FMC_MODE != MB_FMC_MODE_NONE)
                    return fail("DEC", opcode, value, carry, flags, mb.reg.F);
            }
        }
    }
    return 0;
}

static int
test_alu(void)
{
    static const unsigned opcodes[] = {
        0x80U, 0x88U, 0x90U, 0x98U, 0xA0U, 0xA8U, 0xB0U, 0xB8U
    };
    unsigned op, a, b, carry;

    for (op = 0; op < sizeof(opcodes) / sizeof(opcodes[0]); ++op) {
        for (a = 0; a < 256U; ++a) {
            for (b = 0; b < 256U; ++b) {
                for (carry = 0; carry < 2U; ++carry) {
                mb_state mb;
                struct mi_dispatch mi;
                unsigned wide;
                r8 expected_a = (r8)a;
                r8 result_for_flags = (r8)a;
                r8 expected_f = 0U;
                const unsigned opcode = opcodes[op];

                setup(&mb, &mi, opcode);
                mb.reg.A = (r8)a;
                mb.reg.B = (r8)b;
                mb.reg.F = (r8)(carry ? MB_FLAG_C : 0U);
                switch (opcode) {
                case 0x80U: /* ADD A,B */
                    wide = a + b;
                    expected_a = (r8)wide;
                    result_for_flags = expected_a;
                    if (wide > 0xFFU) expected_f |= MB_FLAG_C;
                    if (((a & 0x0FU) + (b & 0x0FU)) > 0x0FU) expected_f |= MB_FLAG_H;
                    break;
                case 0x88U: /* ADC A,B */
                    wide = a + b + carry;
                    expected_a = (r8)wide;
                    result_for_flags = expected_a;
                    if (wide > 0xFFU) expected_f |= MB_FLAG_C;
                    if (((a & 0x0FU) + (b & 0x0FU) + carry) > 0x0FU)
                        expected_f |= MB_FLAG_H;
                    break;
                case 0x90U: /* SUB B */
                case 0xB8U: /* CP B */
                    wide = (a - b) & 0x1FFU;
                    expected_a = (r8)wide;
                    result_for_flags = expected_a;
                    expected_f = MB_FLAG_N;
                    if (a < b) expected_f |= MB_FLAG_C;
                    if ((a & 0x0FU) < (b & 0x0FU)) expected_f |= MB_FLAG_H;
                    if (opcode == 0xB8U) expected_a = (r8)a;
                    break;
                case 0x98U: /* SBC A,B */
                    wide = (a - b - carry) & 0x1FFU;
                    expected_a = (r8)wide;
                    result_for_flags = expected_a;
                    expected_f = MB_FLAG_N;
                    if (a < b + carry) expected_f |= MB_FLAG_C;
                    if ((a & 0x0FU) < ((b & 0x0FU) + carry))
                        expected_f |= MB_FLAG_H;
                    break;
                case 0xA0U: expected_a = (r8)(a & b); result_for_flags = expected_a; expected_f = MB_FLAG_H; break;
                case 0xA8U: expected_a = (r8)(a ^ b); result_for_flags = expected_a; break;
                case 0xB0U: expected_a = (r8)(a | b); result_for_flags = expected_a; break;
                default: return 1;
                }
                if (result_for_flags == 0U)
                    expected_f |= MB_FLAG_Z;
                if (sm83_step(&mb) != 1U)
                    return fail("ALU cycles", opcode, a, b, 1U, 0U);
                /* ADD/SUB/CP use PicoGB's lazy half-carry representation.
                 * Materialise it here, which is exactly what the native
                 * backend avoids by calculating the complete F byte itself. */
                mb.reg.F = (r8)mbh_fr_get(&mb, mb.reg.F);
                if (mb.reg.A != expected_a || mb.reg.F != expected_f ||
                    mb.FMC_MODE != MB_FMC_MODE_NONE)
                    return fail("ALU", opcode, a, b, expected_f, mb.reg.F);
                }
            }
        }
    }
    return 0;
}

static int
test_and_a(void)
{
    unsigned value;

    for (value = 0U; value < 0x100U; ++value)
    {
        mb_state mb;
        struct mi_dispatch mi;
        const r8 expected_f = (r8)(MB_FLAG_H |
            ((value == 0U) ? MB_FLAG_Z : 0U));
        const r16 expected_fr1 = 0x2345U;
        const r16 expected_fr2 = 0x6789U;

        setup(&mb, &mi, 0xA7U);
        mb.reg.A = (r8)value;
        /* A prior lazy flag calculation must be discarded, rather than
         * materialised, just as the generic AND path does. */
        mb.reg.F = MB_FLAG_C;
        mb.FR1 = expected_fr1;
        mb.FR2 = expected_fr2;
        mb.FMC_MODE = MB_FMC_MODE_SUB_r8;

        if (sm83_step(&mb) != 1U || mb.reg.A != (r8)value ||
            mb.reg.F != expected_f || mb.FMC_MODE != MB_FMC_MODE_NONE ||
            mb.FR1 != expected_fr1 || mb.FR2 != expected_fr2 ||
            mb.PC != 0x0202U || mb.IR.low != 0x00U)
            return fail("AND A", 0xA7U, value, 0U, expected_f, mb.reg.F);
    }

    return 0;
}

static int
test_jr_z_direct(void)
{
    static const struct
    {
        r8 displacement;
        r16 next_opcode;
        r16 final_pc;
    } taken_cases[] = {
        { 0x00U, 0x0202U, 0x0203U },
        { 0x7FU, 0x0281U, 0x0282U },
        { 0x80U, 0x0182U, 0x0183U },
        { 0xFCU, 0x01FEU, 0x01FFU }
    };
    const r8 taken_flags = (r8)(MB_FLAG_Z | MB_FLAG_N | MB_FLAG_C | 0x0FU);
    const r8 not_taken_flags = (r8)(MB_FLAG_N | MB_FLAG_H | MB_FLAG_C | 0x0FU);
    const word expected_fr1 = 0x2345U;
    const word expected_fr2 = 0x6789U;
    unsigned i;

    for (i = 0U; i < sizeof(taken_cases) / sizeof(taken_cases[0]); ++i)
    {
        mb_state mb;
        struct mi_dispatch mi;

        setup(&mb, &mi, 0x28U);
        mb.reg.A = 0x5AU;
        mb.reg.F = taken_flags;
        mb.FR1 = expected_fr1;
        mb.FR2 = expected_fr2;
        mb.FMC_MODE = MB_FMC_MODE_SUB_r8;
        rom[0x0201U] = taken_cases[i].displacement;
        rom[taken_cases[i].next_opcode] = 0x00U;

        if (sm83_step(&mb) != 3U || mb.PC != taken_cases[i].final_pc ||
            mb.IR.low != 0x00U || mb.reg.A != 0x5AU ||
            mb.reg.F != taken_flags || mb.FMC_MODE != MB_FMC_MODE_SUB_r8 ||
            mb.FR1 != expected_fr1 || mb.FR2 != expected_fr2)
            return fail("JR Z taken", 0x28U, taken_cases[i].displacement, 0U,
                taken_cases[i].final_pc, mb.PC);
    }

    {
        mb_state mb;
        struct mi_dispatch mi;

        /* The non-taken path skips e8 without reading it, then fetches the
         * opcode after the operand. */
        setup(&mb, &mi, 0x28U);
        mb.reg.A = 0x5AU;
        mb.reg.F = not_taken_flags;
        mb.FR1 = expected_fr1;
        mb.FR2 = expected_fr2;
        mb.FMC_MODE = MB_FMC_MODE_SUB_r8;
        rom[0x0201U] = 0xFEU;
        rom[0x0202U] = 0x00U;

        if (sm83_step(&mb) != 2U || mb.PC != 0x0203U || mb.IR.low != 0x00U ||
            mb.reg.A != 0x5AU || mb.reg.F != not_taken_flags ||
            mb.FMC_MODE != MB_FMC_MODE_SUB_r8 || mb.FR1 != expected_fr1 ||
            mb.FR2 != expected_fr2)
            return fail("JR Z not taken", 0x28U, 0xFEU, 0U, 0x0203U, mb.PC);
    }

    {
        mb_state mb;
        struct mi_dispatch mi;

        /* PicoGB deliberately wedges an unbreakable JR -2 loop. */
        setup(&mb, &mi, 0x28U);
        mb.reg.F = taken_flags;
        rom[0x0201U] = 0xFEU;
        if (sm83_step(&mb) != 0U || mb.PC != 0x0202U || mb.IR.low != 0x28U ||
            mb.reg.F != taken_flags)
            return fail("JR Z -2 wedge", 0x28U, 0xFEU, 0U, 0U, mb.PC);
    }

    {
        mb_state mb;
        struct mi_dispatch mi;

        /* With a potential wakeup, the same JR -2 follows PicoGB's regular
         * path: it returns to the opcode and fetches it again. */
        setup(&mb, &mi, 0x28U);
        mb.reg.F = taken_flags;
        mb.IME_ASK = 1U;
        mb.IE = 0x01U;
        mb.IF = 0x01U;
        rom[0x0201U] = 0xFEU;
        rom[0x0200U] = 0x00U;
        if (sm83_step(&mb) != 3U || mb.PC != 0x0201U || mb.IR.low != 0x00U ||
            mb.IME != 1U || mb.IME_ASK != 0U || mb.IF != 0x01U ||
            mb.reg.F != taken_flags)
            return fail("JR Z -2 wakeable", 0x28U, 0xFEU, 0U, 3U, 0U);
    }

    {
        mb_state mb;
        struct mi_dispatch mi;

        /* An already pending IRQ must win before JR Z can examine F. */
        setup(&mb, &mi, 0x28U);
        mb.reg.F = taken_flags;
        mb.IME = 1U;
        mb.IE = 0x01U;
        mb.IF = 0x01U;
        mb.SP = 0xFFFEU;
        rom[0x0040U] = 0x00U;
        if (sm83_step(&mb) != 5U || mb.PC != 0x0041U || mb.SP != 0xFFFCU ||
            mb.IF != 0U || mb.IME != 0U || mb.reg.F != taken_flags)
            return fail("JR Z IRQ priority", 0x28U, 0U, 0U, 5U, 0U);
    }

    {
        mb_state mb;
        struct mi_dispatch mi;

        /* The IME delay is promoted first, but the pending IRQ still waits
         * until after this JR Z instruction. */
        setup(&mb, &mi, 0x28U);
        mb.reg.F = not_taken_flags;
        mb.IME_ASK = 1U;
        mb.IE = 0x01U;
        mb.IF = 0x01U;
        rom[0x0201U] = 0x00U;
        rom[0x0202U] = 0x00U;
        if (sm83_step(&mb) != 2U || mb.PC != 0x0203U || mb.IME != 1U ||
            mb.IME_ASK != 0U || mb.IF != 0x01U)
            return fail("JR Z EI delay", 0x28U, 0U, 0U, 2U, 0U);
    }

    {
        mb_state mb;
        struct mi_dispatch mi;

        /* Executing from I/O makes the skipped-operand rule observable. */
        setup(&mb, &mi, 0x28U);
        mi.dispatch_IO = jr_contract_io;
        mb.PC = 0xFF00U;
        mb.reg.F = not_taken_flags;
        jr_io_reads = 0U;
        if (sm83_step(&mb) != 2U || mb.PC != 0xFF02U || mb.IR.low != 0x00U ||
            jr_io_reads != 1U || jr_io_addresses[0] != 0x01U)
            return fail("JR Z I/O not taken", 0x28U, 0U, 0U, 1U, jr_io_reads);

        setup(&mb, &mi, 0x28U);
        mi.dispatch_IO = jr_contract_io;
        mb.PC = 0xFF00U;
        mb.reg.F = taken_flags;
        jr_io_reads = 0U;
        if (sm83_step(&mb) != 3U || mb.PC != 0xFF02U || mb.IR.low != 0x00U ||
            jr_io_reads != 2U || jr_io_addresses[0] != 0x00U ||
            jr_io_addresses[1] != 0x01U)
            return fail("JR Z I/O taken", 0x28U, 0U, 0U, 2U, jr_io_reads);
    }

    return 0;
}

static uint64_t
fnv1a_byte(uint64_t hash, r8 value)
{
    return (hash ^ value) * UINT64_C(0x100000001B3);
}

static uint64_t
fnv1a_word(uint64_t hash, word value)
{
    hash = fnv1a_byte(hash, (r8)value);
    hash = fnv1a_byte(hash, (r8)(value >> 8));
    hash = fnv1a_byte(hash, (r8)(value >> 16));
    return fnv1a_byte(hash, (r8)(value >> 24));
}

static uint64_t
hash_cb_state(uint64_t hash, const mb_state *mb, r8 memory_value)
{
    hash = fnv1a_byte(hash, mb->reg.B);
    hash = fnv1a_byte(hash, mb->reg.C);
    hash = fnv1a_byte(hash, mb->reg.D);
    hash = fnv1a_byte(hash, mb->reg.E);
    hash = fnv1a_byte(hash, mb->reg.H);
    hash = fnv1a_byte(hash, mb->reg.L);
    hash = fnv1a_byte(hash, mb->reg.F);
    hash = fnv1a_byte(hash, mb->reg.A);
    hash = fnv1a_word(hash, mb->IR.raw);
    hash = fnv1a_word(hash, mb->PC);
    hash = fnv1a_word(hash, mb->SP);
    hash = fnv1a_byte(hash, mb->IF);
    hash = fnv1a_byte(hash, mb->IE);
    hash = fnv1a_byte(hash, mb->IME);
    hash = fnv1a_byte(hash, mb->IME_ASK);
    hash = fnv1a_word(hash, mb->DIV);
    hash = fnv1a_byte(hash, mb->HALTING);
    hash = fnv1a_word(hash, mb->FR1);
    hash = fnv1a_word(hash, mb->FR2);
    hash = fnv1a_word(hash, mb->FMC_MODE);
    return fnv1a_byte(hash, memory_value);
}

static int
test_cb_matrix(void)
{
    static const r8 source_values[] = {
        0x00U, 0x01U, 0x7FU, 0x80U, 0xFFU, 0x55U, 0xAAU
    };
    uint64_t hash = UINT64_C(0xCBF29CE484222325);
    unsigned cbir;
    unsigned seed;

    /* Exercise every CB subopcode with several values, both register and
     * (HL) targets, then emit a semantic-state hash.  The differential script
     * compiles this same matrix against the inlined reference and the cold
     * helper, so a copied operation cannot silently drift in flags, memory,
     * lazy flag state, fetch state or cycle count. */
    for (cbir = 0U; cbir < 0x100U; ++cbir)
    {
        for (seed = 0U; seed < sizeof(source_values) / sizeof(source_values[0]);
             ++seed)
        {
            mb_state mb;
            struct mi_dispatch mi;
            const r8 source = source_values[seed];
            const r16 haddr = (r16)(0xC100U + seed * 0x31U);
            const unsigned target = cbir & 7U;
            const unsigned group = cbir >> 6;
            const word expected_cycles = (target == 6U) ?
                ((group == 1U) ? 3U : 4U) : 2U;
            const word expected_fr1 = (word)(0x1357U + seed);
            const word expected_fr2 = (word)(0x2468U + cbir);
            word actual_cycles;

            setup(&mb, &mi, 0xCBU);
            mb.reg.B = (r8)(source ^ 0x11U);
            mb.reg.C = (r8)(source ^ 0x22U);
            mb.reg.D = (r8)(source ^ 0x33U);
            mb.reg.E = (r8)(source ^ 0x44U);
            mb.reg.HL = haddr;
            mb.reg.A = (r8)(source ^ 0x77U);
            mb.reg.F = (r8)((seed & 1U ? MB_FLAG_C : 0U) | MB_FLAG_N | 0x0FU);
            mb.FR1 = expected_fr1;
            mb.FR2 = expected_fr2;
            mb.FMC_MODE = MB_FMC_MODE_SUB_r8;
            rom[0x0201U] = (r8)cbir;
            rom[0x0202U] = 0x00U;
            wram[haddr - 0xC000U] = source;
            actual_cycles = sm83_step(&mb);

            if (actual_cycles != expected_cycles || mb.PC != 0x0203U ||
                mb.IR.low != 0x00U || mb.FMC_MODE != MB_FMC_MODE_SUB_r8 ||
                mb.FR1 != expected_fr1 || mb.FR2 != expected_fr2)
                return fail("CB matrix", 0xCBU, cbir, seed,
                    expected_cycles, actual_cycles);

            hash = hash_cb_state(hash, &mb, wram[haddr - 0xC000U]);
        }
    }

    /* An enabled interrupt takes priority over the prefetched CB opcode.
     * This also checks that the cold helper remains below mb_exec()'s normal
     * interrupt prologue rather than becoming a second entry path. */
    {
        mb_state mb;
        struct mi_dispatch mi;

        setup(&mb, &mi, 0xCBU);
        mb.IME = 1U;
        mb.IE = 0x01U;
        mb.IF = 0x01U;
        mb.SP = 0xFFFEU;
        mb.reg.B = 0x5AU;
        rom[0x0201U] = 0x00U; /* Would be RLC B if incorrectly executed. */
        rom[0x0040U] = 0x00U;

        if (sm83_step(&mb) != 5U || mb.PC != 0x0041U ||
            mb.SP != 0xFFFCU || mb.IR.low != 0x00U || mb.IME != 0U ||
            mb.IF != 0U || mb.reg.B != 0x5AU || hram[0xFCU] != 0x00U ||
            hram[0xFDU] != 0x02U)
            return fail("CB IRQ priority", 0xCBU, 0U, 0U, 5U, 0U);
    }

    printf("picogb_cb_matrix_fnv1a=%016llx\n", (unsigned long long)hash);
    return 0;
}

static int
test_hot_control(void)
{
    static const unsigned jr_opcodes[] = {0x20U, 0x28U, 0x30U, 0x38U};
    static const struct
    {
        r8 haddr;
        r8 hram_value;
        r8 expected;
        unsigned int expected_io_reads;
    } ldh_cases[] = {
        { 0x00U, 0x00U, 0x40U, 1U },
        { 0x7FU, 0x00U, 0xBFU, 1U },
        { 0x80U, 0x5AU, 0x5AU, 0U },
        { 0xFEU, 0xA5U, 0xA5U, 0U },
        /* FFFF deliberately retains PicoGB's current HRAM[FF] read. */
        { 0xFFU, 0xC3U, 0xC3U, 0U }
    };
    unsigned i;

    for (i = 0U; i < sizeof(ldh_cases) / sizeof(ldh_cases[0]); ++i)
    {
        mb_state mb;
        struct mi_dispatch mi;

        setup(&mb, &mi, 0xF0U);
        mi.dispatch_IO = ldh_contract_io;
        ldh_io_reads = 0U;
        rom[0x0201U] = ldh_cases[i].haddr;
        rom[0x0202U] = 0x00U;
        hram[ldh_cases[i].haddr] = ldh_cases[i].hram_value;
        mb.reg.F = MB_FLAG_C;
        mb.FR1 = 0x1357U;
        mb.FR2 = 0x2468U;
        mb.FMC_MODE = MB_FMC_MODE_SUB_r8;
        if (sm83_step(&mb) != 3U || mb.reg.A != ldh_cases[i].expected ||
            mb.PC != 0x0203U || mb.IR.low != 0x00U ||
            mb.reg.F != MB_FLAG_C || mb.FR1 != 0x1357U ||
            mb.FR2 != 0x2468U || mb.FMC_MODE != MB_FMC_MODE_SUB_r8 ||
            ldh_io_reads != ldh_cases[i].expected_io_reads)
            return fail("LDH A,(a8)", 0xF0U, ldh_cases[i].haddr,
                ldh_cases[i].expected, ldh_cases[i].expected, mb.reg.A);
    }

    {
        mb_state mb;
        struct mi_dispatch mi;

        /* The shortcut must remain below the normal interrupt prologue: a
         * pending IRQ owns this step and must not fetch F0's operand/data. */
        setup(&mb, &mi, 0xF0U);
        mi.dispatch_IO = ldh_contract_io;
        mb.PC = 0xFF00U;
        mb.reg.A = 0x5AU;
        mb.reg.F = MB_FLAG_C;
        mb.IME = 1U;
        mb.IE = 0x01U;
        mb.IF = 0x01U;
        mb.SP = 0xFFFEU;
        rom[0x0040U] = 0x00U;
        ldh_io_reads = 0U;
        if (sm83_step(&mb) != 5U || mb.PC != 0x0041U ||
            mb.SP != 0xFFFCU || mb.IME != 0U || mb.IF != 0U ||
            mb.reg.A != 0x5AU || mb.reg.F != MB_FLAG_C || ldh_io_reads != 0U)
            return fail("F0 IRQ priority", 0xF0U, 0U, 0U, 5U, 0U);
    }

    {
        mb_state mb;
        struct mi_dispatch mi;

        /* EI's delayed IME promotion happens before F0, but the IRQ is still
         * pending until the following instruction. */
        setup(&mb, &mi, 0xF0U);
        rom[0x0201U] = 0x80U;
        rom[0x0202U] = 0x00U;
        hram[0x80U] = 0x5AU;
        mb.IME_ASK = 1U;
        mb.IE = 0x01U;
        mb.IF = 0x01U;
        if (sm83_step(&mb) != 3U || mb.PC != 0x0203U ||
            mb.IR.low != 0x00U || mb.reg.A != 0x5AU || mb.IME != 1U ||
            mb.IME_ASK != 0U || mb.IF != 0x01U)
            return fail("F0 EI delay", 0xF0U, 0U, 0U, 3U, 0U);
    }

    {
        mb_state mb;
        struct mi_dispatch mi;

        /* Fetch both a8 and the next opcode through the slow I/O map. */
        setup(&mb, &mi, 0xF0U);
        mi.dispatch_IO = f0_contract_io;
        mb.PC = 0xFF00U;
        hram[0x85U] = 0xA5U;
        f0_io_reads = 0U;
        if (sm83_step(&mb) != 3U || mb.PC != 0xFF02U ||
            mb.IR.low != 0x00U || mb.reg.A != 0xA5U ||
            f0_io_reads != 2U || f0_io_addresses[0] != 0x00U ||
            f0_io_addresses[1] != 0x01U)
            return fail("F0 I/O fetch", 0xF0U, 0U, 0U, 2U, f0_io_reads);
    }

    {
        mb_state mb;
        struct mi_dispatch mi;

        /* a8 can be fetched from FFFF and the next opcode must wrap to ROM
         * address 0000 exactly as the shared fetch routine does. */
        setup(&mb, &mi, 0xF0U);
        mb.PC = 0xFFFFU;
        hram[0xFFU] = 0x80U;
        hram[0x80U] = 0x3CU;
        rom[0x0000U] = 0x00U;
        if (sm83_step(&mb) != 3U || mb.PC != 0x0001U ||
            mb.IR.low != 0x00U || mb.reg.A != 0x3CU)
            return fail("F0 PC wrap", 0xF0U, 0xFFU, 0U, 3U, 0U);
    }

    {
        mb_state mb;
        struct mi_dispatch mi;

        /* The A7 shortcut is intentionally below the IRQ prologue.  An
         * already pending interrupt must win and A7 must not execute. */
        setup(&mb, &mi, 0xA7U);
        mb.reg.A = 0x5AU;
        mb.reg.F = MB_FLAG_C;
        mb.IME = 1U;
        mb.IE = 0x01U;
        mb.IF = 0x01U;
        mb.SP = 0xFFFEU;
        rom[0x0040U] = 0x00U;
        if (sm83_step(&mb) != 5U || mb.reg.A != 0x5AU ||
            mb.reg.F != MB_FLAG_C || mb.PC != 0x0041U ||
            mb.SP != 0xFFFCU || mb.IF != 0U || mb.IME != 0U)
            return fail("A7 IRQ priority", 0xA7U, 0U, 0U, 5U, 0U);
    }

    for (i = 0U; i < sizeof(jr_opcodes) / sizeof(jr_opcodes[0]); ++i) {
        mb_state mb;
        struct mi_dispatch mi;
        const unsigned opcode = jr_opcodes[i];
        const r8 flag = (opcode == 0x20U || opcode == 0x30U) ? 0U :
            ((opcode == 0x28U) ? MB_FLAG_Z : MB_FLAG_C);

        setup(&mb, &mi, opcode);
        mb.reg.F = flag;
        rom[0x0201U] = 0x02U;
        rom[0x0204U] = 0x00U;
        if (sm83_step(&mb) != 3U || mb.PC != 0x0205U || mb.IR.low != 0x00U)
            return fail("JR cc taken", opcode, 0U, 0U, 3U, 0U);

        setup(&mb, &mi, opcode);
        mb.reg.F = (r8)(flag ^ ((opcode == 0x20U || opcode == 0x28U) ?
            MB_FLAG_Z : MB_FLAG_C));
        rom[0x0201U] = 0x7FU;
        rom[0x0202U] = 0x00U;
        if (sm83_step(&mb) != 2U || mb.PC != 0x0203U || mb.IR.low != 0x00U)
            return fail("JR cc not taken", opcode, 0U, 0U, 2U, 0U);
    }

    {
        mb_state mb;
        struct mi_dispatch mi;

        setup(&mb, &mi, 0x40U);
        mb.IME = 1U;
        mb.IE = 0x04U;
        mb.IF = 0x04U;
        mb.SP = 0xFFFEU;
        rom[0x0050U] = 0x00U;
        if (sm83_step(&mb) != 5U || mb.PC != 0x0051U ||
            mb.SP != 0xFFFCU || hram[0xFDU] != 0x02U ||
            hram[0xFCU] != 0x00U || mb.IF != 0U || mb.IME != 0U)
            return fail("IRQ", 0x40U, 0U, 0U, 5U, 0U);
    }

    {
        mb_state mb;
        struct mi_dispatch mi;

        setup(&mb, &mi, 0x40U);
        mb.IME_ASK = 1U;
        if (sm83_step(&mb) != 1U || mb.IME != 1U || mb.IME_ASK != 0U)
            return fail("EI delay", 0x40U, 0U, 0U, 1U, 0U);
    }

    {
        mb_state mb;
        struct mi_dispatch mi;

        /* NOP is deliberately outside the compact families. After EI, an
         * already-pending IRQ must wait until this one fallback instruction
         * completes; otherwise the compact prologue ran twice. */
        setup(&mb, &mi, 0x00U);
        mb.IME_ASK = 1U;
        mb.IE = 0x01U;
        mb.IF = 0x01U;
        if (sm83_step(&mb) != 1U || mb.PC != 0x0202U ||
            mb.IME != 1U || mb.IME_ASK != 0U || mb.IF != 0x01U)
            return fail("EI fallback delay", 0x00U, 0U, 0U, 1U, 0U);
    }

    return 0;
}

static int
test_immediates_and_r16(void)
{
    unsigned group;
    for (group = 0; group < 4U; ++group) {
        mb_state mb;
        struct mi_dispatch mi;
        const unsigned opcode = 0x01U | (group << 4);
        const r16 value = (r16)(0x1234U + group * 0x1111U);

        setup(&mb, &mi, opcode);
        rom[0x0201U] = (r8)value;
        rom[0x0202U] = (r8)(value >> 8);
        if (sm83_step(&mb) != 3U)
            return fail("LD rr,d16 cycles", opcode, group, 0U, 3U, 0U);
        if ((group == 0U && mb.reg.BC != value) ||
            (group == 1U && mb.reg.DE != value) ||
            (group == 2U && mb.reg.HL != value) ||
            (group == 3U && mb.SP != value))
            return fail("LD rr,d16 value", opcode, group, 0U, value, 0U);
    }
    return 0;
}

int
main(void)
{
    if (test_ld_registers() || test_inc_dec() || test_alu() || test_and_a() ||
        test_jr_z_direct() || test_cb_matrix() || test_immediates_and_r16() ||
        test_hot_control())
        return 1;

    puts("picogb_sm83_contract: PASS");
    return 0;
}
