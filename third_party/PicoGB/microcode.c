/* knovGBC modification notice updated 2026-10-10.
 * This file differs from upstream PicoGB commit
 * 325a12a25e2d1f4799424cc18c0e69a7810695b0.
 * The preserved local code changes predate the 2026-10-07 inventory.
 * Exact historical code-edit dates are not established by surviving records.
 * Only this source-handoff notice was changed on 2026-10-10.
 * See third_party/PicoGB/KNOVGBC_CHANGES.md for dated validation evidence;
 * report dates are not claims that every code change occurred on those dates.
 * PicoGB and this modified library remain under GNU LGPL v2.1.
 * This notice does not replace upstream license or copyright notices.
 */
#include <stdio.h>

#include "microcode.h"
#include "microcode_dispatch.h"
#include "dbg.h"

#if defined(KNOVGBC_LDH_F0_PROFILE)
/* Host-test hook only.  Keep it at the real operand fetch so the report
 * describes what LDH A,(a8) actually reads, rather than inferring it from
 * the program counter. */
void knovgbc_ldh_f0_profile_record(word haddr);
#endif

#if defined(KNOVGBC_OPCODE_PROFILE)
#include "picogb_opcode_profile.h"
#define OPCODE_PROFILE_HADDR_READ(haddr) \
    picogb_opcode_profile_memory_record( \
        PICOGB_OPCODE_PROFILE_ACCESS_READ, (uint16_t)(0xFF00U | (haddr)))
#define OPCODE_PROFILE_HADDR_WRITE(haddr) \
    picogb_opcode_profile_memory_record( \
        PICOGB_OPCODE_PROFILE_ACCESS_WRITE, (uint16_t)(0xFF00U | (haddr)))
#else
#define OPCODE_PROFILE_HADDR_READ(haddr)
#define OPCODE_PROFILE_HADDR_WRITE(haddr)
#endif


#define self mb_state* __restrict

#if GBA
#define IR_F_COL (IR & 7)
#define IR_F_ROW ((IR >> 3) & 7)
#else
#define IR_F_COL IR_column
#define IR_F_ROW IR_row
#endif

#define MB_AF_R ((mb->reg.A << 8) + mb->reg.F)
#define MB_AF_W(v) {mb->reg.A = ((v) >> 8) & 0xFF; mb->reg.F = (v) & MB_FLAG_BITS;}
#define MB_CC_CHECK (mbh_cc_check(IR, mb->reg.F))

#if defined(KNOVGBC_STACK_PAIR_WRAM) || defined(KNOVGBC_STACK_PROFILE)
#if defined(KNOVGBC_STACK_PAIR_WRAM) && defined(KNOVGBC_FAST_MEMORY) && \
    CONFIG_USE_FLAT_ROM && CONFIG_NOBOOTMEME && !CONFIG_ENABLE_LRU && \
    !CONFIG_DBG && !defined(KNOVGBC_OPCODE_PROFILE) && \
    !defined(KNOVGBC_COMPONENT_PROFILE)
#define PGB_STACK_PAIR_DIRECT 1
#else
#define PGB_STACK_PAIR_DIRECT 0
#endif
#if defined(KNOVGBC_STACK_PROFILE)
/* The host probe records every actual stack pair, including fallbacks. */
void knovgbc_stack_profile_record(word first, word second, unsigned is_write);
#endif

/* The fixed C000-CFFF WRAM bank is independent of the cartridge and CGB
 * bank register. A pair must remain wholly inside it; boundary crossings,
 * HRAM and every other address retain the complete memory dispatcher. */
PGB_FUNC static inline __attribute__((always_inline)) word
mb_stack_read_pair(self mb, word sp)
{
    const word first = sp & 0xFFFFU;
    const word second = (sp + 1U) & 0xFFFFU;

#if defined(KNOVGBC_STACK_PROFILE)
    knovgbc_stack_profile_record(first, second, 0U);
#endif
#if PGB_STACK_PAIR_DIRECT
    if ((sp >= 0xC000U) && (sp <= 0xCFFEU)) {
        const r8 *const wram = mb->mi->WRAM;
        return (word)wram[first & 0x0FFFU] |
            ((word)wram[second & 0x0FFFU] << 8);
    }
#endif
    {
        const word low = mch_memory_dispatch_read(mb, first);
        const word high = mch_memory_dispatch_read(mb, second);
        return low + (high << 8);
    }
}

PGB_FUNC static inline __attribute__((always_inline)) void
mb_stack_write_pair(self mb, word sp, word value)
{
    const word first = (sp - 1U) & 0xFFFFU;
    const word second = (sp - 2U) & 0xFFFFU;

#if defined(KNOVGBC_STACK_PROFILE)
    knovgbc_stack_profile_record(first, second, 1U);
#endif
#if PGB_STACK_PAIR_DIRECT
    if ((sp >= 0xC002U) && (sp <= 0xD000U)) {
        r8 *const wram = mb->mi->WRAM;
        wram[first & 0x0FFFU] = (r8)(value >> 8);
        wram[second & 0x0FFFU] = (r8)value;
        return;
    }
#endif
    mch_memory_dispatch_write(mb, first, value >> 8);
    mch_memory_dispatch_write(mb, second, value & 0xFFU);
}
#endif

#if defined(KNOVGBC_F0_ROM_OPERAND_INLINE)
/* F0 executes often, so keep its ordinary ROM operand fetch at the call
 * site.  The complete fetcher still handles code executed outside ROM. */
PGB_FUNC static inline __attribute__((always_inline)) word
mb_f0_fetch_operand(self mb)
{
    const word addr = mb->PC;
    const struct mi_dispatch *const mi = mb->mi;

    if(addr < 0x4000U)
    {
        mb->PC = (addr + 1U) & 0xFFFFU;
        return mi->ROM[addr];
    }
    if(addr < 0x8000U)
    {
        mb->PC = (addr + 1U) & 0xFFFFU;
        return mi->ROM[(mi->BANK_ROM << 14) | (addr & 0x3FFFU)];
    }

    return mch_memory_fetch_PC_op_1(mb);
}
#endif


#pragma region Flag mode control

PGB_FUNC static inline void mbh_fr_set_r8_add(self mb, word left, word right)
{
    mb->FR1 = left;
    mb->FR2 = right;
    mb->FMC_MODE = MB_FMC_MODE_ADD_r8;
}

PGB_FUNC static inline void mbh_fr_set_r8_adc(self mb, word left, word right)
{
    mb->FR1 = left;
    mb->FR2 = right;
    mb->FMC_MODE = MB_FMC_MODE_ADC_r8;
}

PGB_FUNC static inline void mbh_fr_set_r8_sub(self mb, word left, word right)
{
    mb->FR1 = left;
    mb->FR2 = right;
    mb->FMC_MODE = MB_FMC_MODE_SUB_r8;
}

PGB_FUNC static inline void mbh_fr_set_r8_sbc(self mb, word left, word right)
{
    mb->FR1 = left;
    mb->FR2 = right;
    mb->FMC_MODE = MB_FMC_MODE_SBC_r8;
}

PGB_FUNC static inline void mbh_fr_set_r16_add(self mb, word left, word right)
{
    mb->FR1 = left;
    mb->FR2 = right;
    mb->FMC_MODE = MB_FMC_MODE_ADD_r16;
}

PGB_FUNC static inline void mbh_fr_set_r16_add_r8(self mb, word left, word right)
{
    mb->FR1 = left;
    mb->FR2 = right;
    mb->FMC_MODE = MB_FMC_MODE_ADD_r16_r8;
}

PGB_FUNC word mbh_fr_get(self mb, word Fin)
{
    if(mb->FMC_MODE == MB_FMC_MODE_NONE)
        return Fin;
    
    var n1 = mb->FR1;
    var n2 = mb->FR2;
    
    Fin &= ~MB_FLAG_H; //TODO: why clear HC here? explain.
    
    switch(mb->FMC_MODE & 0xF)
    {
        default:
            return Fin;
        
        case MB_FMC_MODE_ADD_r16_r8:
        {
            //if(((n1 & 0xF00) + (n2 & 0xF00)) > 0xFFF)
            if(((n1 & 0xF) + (n2 & 0xF)) > 0xF) // ???
                Fin |= MB_FLAG_H;
            
            break;
        }
        
        case MB_FMC_MODE_ADD_r16:
        {
            if(((n1 & 0xFFF) + (n2 & 0xFFF)) > 0xFFF)
                Fin |= MB_FLAG_H;
            
            break;
        }
        
        
        case MB_FMC_MODE_ADD_r8:
        {
            if(((n1 & 0xF) + (n2 & 0xF)) > 0xF)
                Fin |= MB_FLAG_H;
            
            break;
        }
        
        case MB_FMC_MODE_ADC_r8:
        {
            if(((n1 & 0xF) + (n2 & 0xF) + 1) > 0xF)
                Fin |= MB_FLAG_H;
            
            break;
        }
        
        case MB_FMC_MODE_SUB_r8:
        {
            if((n1 & 0xF) < (n2 & 0xF))
                Fin |= MB_FLAG_H;
            
            break;
        }
        
        case MB_FMC_MODE_SBC_r8:
        {
            if((n1 & 0xF) < ((n2 & 0xF) + 1))
                Fin |= MB_FLAG_H;
            
            break;
        }
    }
    
    mb->FMC_MODE = MB_FMC_MODE_NONE;
    return Fin;
}

PGB_FUNC static inline wbool mbh_cc_check_0(word F)
{
    return ~F & MB_FLAG_Z; // NZ
}

PGB_FUNC static inline wbool mbh_cc_check_1(word F)
{
    return F & MB_FLAG_Z; // Z
}

PGB_FUNC static inline wbool mbh_cc_check_2(word F)
{
    return ~F & MB_FLAG_C; // NC
}

PGB_FUNC static inline wbool mbh_cc_check_3(word F)
{
    return F & MB_FLAG_C; // C
}

PGB_FUNC static wbool mbh_cc_check(word IR, word F)
{
    register word IR_r = (IR >> 3) & 3;
    
    if(IR_r == MB_CC_NZ)
        return mbh_cc_check_0(F); // NZ
    else if(IR_r == MB_CC_Z)
        return mbh_cc_check_1(F); // Z
    else if(IR_r == MB_CC_NC)
        return mbh_cc_check_2(F); // NC
    else if(IR_r == MB_CC_C)
        return mbh_cc_check_3(F); // C
    
    __builtin_unreachable();
}

#pragma endregion

#pragma region disasm (unfinished)
#if CONFIG_DBG

static const char* str_r8[8] = {"B", "C", "D", "E", "H", "L", "[HL]", "A"};
static const char* str_aluop[8] = {"ADD", "ADC", "SUB", "SBC", "AND", "XOR", "ORR", "CMP"};

void mb_disasm(const struct mb_state* __restrict mb)
{
    var IR = mb->IR.low;
    
    switch(IR >> 6)
    {
        case 1:
            printf("LD %s, %s", str_r8[(IR >> 3) & 7], str_r8[IR & 7]);
            break;
        
        case 2:
            printf("%s A, %s", str_aluop[(IR >> 3) & 7], str_r8[IR & 7]);
            break;
    }
    
    puts("");
}

static const char* str_cbop[4] = {0, "BIT", "SET", "RES"};
static const char* str_cbop0[8] = {"ROL", "ROR", "RCL", "RCR", "LSL", "ASR", "SWAP", "LSR"};

void mb_disasm_CB(const struct mb_state* __restrict mb, word CBIR)
{
    var IR = CBIR;
    
    if(IR >> 6)
    {
        printf("%s %s.%u", str_cbop[(IR >> 6) & 3], str_r8[IR & 7], (IR >> 3) & 7);
    }
    else
    {
        printf("%s %s", str_cbop0[(IR >> 3) & 7], str_r8[IR & 7]);
    }
    
    puts("");
}

#endif
#pragma endregion

/*
 * The compact-C experiment keeps the proven complete decoder available as a
 * cold-compatible fallback.  Prevent LTO from folding it back into the
 * frontend's giant frame loop: the SH-2 has only a 4 KiB cache, and the
 * experiment is specifically measuring whether a smaller, separate CPU
 * working set helps.  Other build modes retain the original inlining policy.
 */
#if defined(KNOVGBC_SM83_COMPACT)
#define MB_EXEC_COMPACT_ATTRIBUTES \
    __attribute__((noinline, noclone, externally_visible, optimize("Os")))
#else
#define MB_EXEC_COMPACT_ATTRIBUTES
#endif

#if defined(KNOVGBC_CB_COLD) || defined(KNOVGBC_HOT_COLD_SPLIT)
#if defined(KNOVGBC_HOT_COLD_SPLIT)
/* Keep the already isolated CB decoder behind the main interpreter as well.
 * CB_COLD alone preserves the historical layout of the previous candidate;
 * the new split moves both cold helpers together. */
#define MB_EXEC_CB_SECTION __attribute__((section(".knovgbc.coldtext")))
#else
#define MB_EXEC_CB_SECTION
#endif
/* CB-prefixed instructions are rare in the measured Tetris workload.  Keep
 * their complete, generic implementation out of the LTO-inlined mb_exec()
 * body so the ordinary instruction stream has a smaller cache footprint.
 * This helper owns the CB byte fetch and the final ordinary opcode fetch. */
PGB_FUNC __attribute__((noinline, noclone, cold, optimize("Os"))) MB_EXEC_CB_SECTION static word
mb_exec_cb(self mb)
{
    var ncycles = 1; /* $CB opcode fetch */
    var CBIR = mch_memory_fetch_PC_op_1(mb);
    var i_src;
    var i_dst;
    var data_reg;
    var data_flags;

#if CONFIG_DBG
    if(_IS_DBG)
    {
        printf("               (%01o:%01o:%01o) ", CBIR >> 6, CBIR & 7,
            (CBIR >> 3) & 7);
        mb_disasm_CB(mb, CBIR);
    }
#endif

    i_src = (CBIR >> 3) & 7;
    i_dst = (CBIR & 7) ^ 1;

    if(i_dst != 7)
        data_reg = MB_REG8(mb, i_dst);
    else
    {
        ++ncycles; /* memory source read */
        data_reg = mch_memory_dispatch_read(mb, mb->reg.HL);
    }

    switch(CBIR >> 6)
    {
        case 0: /* rotate/shift */
            data_flags = mb->reg.F & MB_FLAG_C;

            switch(i_src)
            {
                case 0: /* RLC */
                    data_reg = (data_reg << 1) | (data_reg >> 7);
                    data_flags = (data_reg >> 4) & 0x10;
                    break;

                case 1: /* RRC */
                    data_reg = (data_reg << 7) | (data_reg >> 1);
                    data_flags = (data_reg >> 3) & 0x10;
                    break;

                case 2: /* RL */
                    data_reg = (data_reg << 1) | ((data_flags >> 4) & 1);
                    data_flags = (data_reg >> 4) & 0x10;
                    break;

                case 3: /* RR */
                    data_reg = (data_reg << 8) | (data_reg >> 1) |
                        ((data_flags & 0x10) << 3);
                    data_flags = (data_reg >> 4) & 0x10;
                    break;

                case 4: /* SLA */
                    data_reg <<= 1;
                    data_flags = (data_reg >> 4) & 0x10;
                    break;

                case 5: /* SRA */
                    data_flags = (data_reg & 1) << 4;
                    data_reg = (data_reg >> 1) | (data_reg & 0x80);
                    break;

                case 6: /* SWAP */
                    data_flags = 0;
                    data_reg = (data_reg >> 4) | (data_reg << 4);
                    break;

                case 7: /* SRL */
                    data_flags = (data_reg & 1) << 4;
                    data_reg >>= 1;
                    break;
            }

            data_reg &= 0xFF;
            if(!data_reg)
                data_flags += MB_FLAG_Z;

            mb->reg.F = data_flags;
            break;

        case 1: /* BIT */
            data_flags = mb->reg.F;
            if(data_reg & (1 << i_src))
                data_flags = (data_flags & MB_FLAG_C) + MB_FLAG_H;
            else
                data_flags = (data_flags & MB_FLAG_C) +
                    (MB_FLAG_H | MB_FLAG_Z);

            mb->reg.F = data_flags;
            mb->IR.raw = mch_memory_fetch_PC(mb);
            return ncycles + 1;

        case 2: /* RES */
            data_reg &= ~(1 << i_src);
            break;

        case 3: /* SET */
            data_reg |= 1 << i_src;
            break;

        default:
            return 0;
    }

    if(i_dst != 7)
        MB_REG8(mb, i_dst) = data_reg;
    else
    {
        ++ncycles; /* memory result write */
        mch_memory_dispatch_write(mb, mb->reg.HL, data_reg);
    }

    mb->IR.raw = mch_memory_fetch_PC(mb);
    return ncycles + 1;
}
#undef MB_EXEC_CB_SECTION
#endif

#if defined(KNOVGBC_HOT_COLD_SPLIT)
/*
 * The eight x7 accumulator/flag opcodes below are complete one-byte
 * instructions, but their generic implementation occupies a disproportionate
 * part of mb_exec().  They are deliberately kept together in a cold function
 * instead of building a second decoder: IRQ/IME handling remains in mb_exec,
 * and this helper owns the full instruction tail (including the prefetched
 * next opcode and its one machine cycle).
 *
 * This is a general SM83 split, not a Tetris address or opcode-sequence
 * shortcut.  DAA continues to use PicoGB's deferred-flag materialisation,
 * which is important for code that follows ADD/ADC/SUB/SBC.
 */
PGB_FUNC __attribute__((noinline, noclone, cold, optimize("Os"), \
    section(".knovgbc.coldtext"))) static word
mb_exec_acc_misc_cold(self mb, word IR)
{
    word data_reg;
    word data_flags;

    switch((IR >> 3) & 7U)
    {
        case 0: /* RLCA */
            data_reg = mb->reg.A;
            data_flags = mb->reg.F;
            data_flags &= MB_FLAG_C;
            data_reg = (data_reg << 1) | (data_reg >> 7);
            data_flags = (data_reg >> 4) & 0x10;
            mb->reg.A = data_reg & 0xFF;
            mb->reg.F = data_flags;
            mb->FMC_MODE = MB_FMC_MODE_NONE;
            break;

        case 1: /* RRCA */
            data_reg = mb->reg.A;
            data_flags = mb->reg.F;
            data_flags &= MB_FLAG_C;
            data_reg = (data_reg << 7) | (data_reg >> 1);
            data_flags = (data_reg >> 3) & 0x10;
            mb->reg.A = data_reg & 0xFF;
            mb->reg.F = data_flags;
            mb->FMC_MODE = MB_FMC_MODE_NONE;
            break;

        case 2: /* RLA */
            data_reg = mb->reg.A;
            data_flags = mb->reg.F;
            data_flags &= MB_FLAG_C;
            data_reg = (data_reg << 1) | ((data_flags >> 4) & 1);
            data_flags = (data_reg >> 4) & 0x10;
            mb->reg.A = data_reg & 0xFF;
            mb->reg.F = data_flags;
            mb->FMC_MODE = MB_FMC_MODE_NONE;
            break;

        case 3: /* RRA */
            data_reg = mb->reg.A;
            data_flags = mb->reg.F;
            data_flags &= MB_FLAG_C;
            data_reg = (data_reg << 8) | (data_reg >> 1) |
                ((data_flags & 0x10) << 3);
            data_flags = (data_reg >> 4) & 0x10;
            mb->reg.A = data_reg & 0xFF;
            mb->reg.F = data_flags;
            mb->FMC_MODE = MB_FMC_MODE_NONE;
            break;

        case 4: /* DAA */
            data_reg = mb->reg.A;
            data_flags = mb->reg.F;
            data_flags &= ~MB_FLAG_Z;
            data_flags = mbh_fr_get(mb, data_flags);

            if(!(data_flags & MB_FLAG_N))
            {
                if((data_flags & MB_FLAG_H) || ((data_reg & 0xF) > 9))
                {
                    data_reg += 6;
                    data_flags |= MB_FLAG_H;
                }

                if((data_flags & MB_FLAG_C) ||
                    (((data_reg >> 4) & 0x1F) > 9))
                {
                    data_reg += 6 << 4;
                    data_flags |= MB_FLAG_C;
                }
            }
            else
            {
                if(data_flags & MB_FLAG_H)
                {
                    data_reg -= 6;
                    data_flags |= MB_FLAG_H;
                }

                if(data_flags & MB_FLAG_C)
                {
                    data_reg -= 6 << 4;
                    data_flags |= MB_FLAG_C;
                }
            }

            data_flags &= (MB_FLAG_C | MB_FLAG_N);
            data_reg &= 0xFF;
            if(!data_reg)
                data_flags |= MB_FLAG_Z;
            mb->reg.A = data_reg;
            mb->reg.F = data_flags;
            break;

        case 5: /* CPL A */
            mb->reg.F |= MB_FLAG_N | MB_FLAG_H;
            mb->reg.A = ~mb->reg.A;
            mb->FMC_MODE = MB_FMC_MODE_NONE;
            break;

        case 6: /* SCF */
            mb->reg.F = (mb->reg.F & MB_FLAG_Z) + MB_FLAG_C;
            mb->FMC_MODE = MB_FMC_MODE_NONE;
            break;

        case 7: /* CCF */
            mb->reg.F = (mb->reg.F ^ MB_FLAG_C) &
                (MB_FLAG_C | MB_FLAG_Z);
            mb->FMC_MODE = MB_FMC_MODE_NONE;
            break;

        default:
            __builtin_unreachable();
    }

    /* Match generic_fetch exactly: raw, rather than low, clears IR.high. */
    mb->IR.raw = mch_memory_fetch_PC(mb);
    return 1U;
}
#endif

PGB_FUNC ATTR_HOT MB_EXEC_COMPACT_ATTRIBUTES word mb_exec(self mb)
{
    register var IR = mb->IR.low;
    r16* __restrict p_reg16_ptr;
    
    // Instruction column left to right
    var IR_column = IR & 7;
    // Instruction row top to bottom
    var IR_row = (IR >> 3) & 7;
    
    // Cycle count
    var ncycles = 0;
    
    // Index of source or read-only register
    var i_src;
    // Index of destination or read-modify-write register index
    var i_dst;
    
    // Data for 8bit registers
    var data_reg;
    // Data for 16bit registers
    var data_wide;
    // Contains flags where necessary
    var data_flags;
    // Contains result data to be written back eventually
    var data_result;
    
    if(mb->IME) // Interrupts are enabled
    {
        var F = mbh_irq_get_pending(mb);
        if(F) // Handle IREQ if there is any
        {
            ++ncycles; // IDU decrement PC penalty cycle
            
            data_wide = (mb->PC - 1) & 0xFFFF;
            
            var i = 0;
            for(;;)
            {
                if(F & (1 << i))
                {
                    mb->PC = 0x40 + (i << 3);
                    break;
                }
                
                ++i;
            }
            
            mb->IME = 0;
            mb->IME_ASK = 0;
            
            mb->IF &= ~(1 << i);
        
        #if CONFIG_DBG
            DBGF("IRQ #%u\n", i);
        #endif
            
            goto generic_push;
        }
    }
    else if(mb->IME_ASK) // IME was asked to be turned on
    {
        mb->IME = 1;
        mb->IME_ASK = 0;
    }
    
#if CONFIG_DBG
    if(_IS_DBG)
    {
        DBGF("Instruction %02X (%01o:%01o:%01o) ", IR, IR >> 6, IR & 7, (IR >> 3) & 7);
        mb_disasm(mb);
    }
#endif

    #if 1
    if(IR >= 0xC0)
    {
#if defined(KNOVGBC_LDH_F0_TIGHT)
        /*
         * The normal F0 route reaches instr_360 below, adds its two extra
         * cycles, then falls through the shared next-opcode fetch.  F0 is
         * common enough to make that internal control flow worth avoiding,
         * but only after the interrupt/IME prologue above has run.
         *
         * This is deliberately not a new memory map.  FF80-FFFE retains the
         * already-validated HRAM load; FF00-FF7F and FFFF still call the
         * original high-address dispatcher.  The explicit return of three
         * machine cycles matches the old a8 fetch + read + prefetch path.
         */
        if(IR == 0xF0U)
        {
#if defined(KNOVGBC_F0_ROM_OPERAND_INLINE)
            data_wide = mb_f0_fetch_operand(mb);
#else
            data_wide = mch_memory_fetch_PC_op_1(mb);
#endif
#if defined(KNOVGBC_LDH_F0_PROFILE)
            knovgbc_ldh_f0_profile_record(data_wide);
#endif
            DBGF("- /HR %04X -> ", data_wide + 0xFF00);
            OPCODE_PROFILE_HADDR_READ(data_wide);
#if defined(KNOVGBC_LDH_HRAM_DIRECT)
            if((data_wide >= 0x80U) && (data_wide < 0xFFU))
                mb->reg.A = mb->mi->HRAM[data_wide];
            else
#endif
                mb->reg.A = mch_memory_dispatch_read_Haddr(mb, data_wide);
            DBGF("%02X\n", mb->reg.A);

            mb->IR.raw = mch_memory_fetch_PC(mb);
            return 3U;
        }
#endif
        if(IR == 0xF0)
        {
#if defined(KNOVGBC_F0_ROM_OPERAND_INLINE)
            data_wide = mb_f0_fetch_operand(mb);
#else
            data_wide = mch_memory_fetch_PC_op_1(mb);
#endif
#if defined(KNOVGBC_LDH_F0_PROFILE)
            knovgbc_ldh_f0_profile_record(data_wide);
#endif
            goto instr_360;
        }
        else if(IR == 0xFA)
        {
            data_wide = mch_memory_fetch_PC_op_2(mb);
            goto instr_372;
        }
#if defined(KNOVGBC_CP_N8_DIRECT)
        else if(IR == 0xFEU)
        {
            /* CP n8 only changes flags.  Keep PicoGB's deferred half-borrow
             * bookkeeping and its ordinary operand/prefetch memory paths. */
            const word operand = mch_memory_fetch_PC_op_1(mb);
            const word accumulator = mb->reg.A;
            word flags = MB_FLAG_N;

            mbh_fr_set_r8_sub(mb, accumulator, operand);
            if(accumulator < operand)
                flags |= MB_FLAG_C;
            if(accumulator == operand)
                flags |= MB_FLAG_Z;
            mb->reg.F = (r8)flags;
            mb->IR.raw = mch_memory_fetch_PC(mb);
            return 2U;
        }
#endif
        else
            goto IR_case_3;
    }
    else if(IR >= 0x80)
    {
#if defined(KNOVGBC_A7_DIRECT)
        /* AND A,A has no data dependency beyond A itself.  Keep the check
         * inside the ALU range so unrelated opcodes do not pay for it.  The
         * shortcut remains inside mb_exec(), after the IRQ/IME prologue, and
         * keeps the normal timing and next-opcode-fetch contract. */
        if(IR == 0xA7U)
        {
            hilow16_t fa;

            /* Match the regular ALU path's endian-safe single write of FA. */
            fa.low = mb->reg.A;
            fa.high = MB_FLAG_H + (mb->reg.A ? 0U : MB_FLAG_Z);
            mb->reg.hilo16[3] = fa;
            mb->FMC_MODE = MB_FMC_MODE_NONE;
            goto generic_fetch;
        }
#endif
        goto IR_case_2;
    }
    else if(IR < 0x40)
    {
        if(IR_F_COL == 0)
            goto instr_0x0;
        else
            goto IR_case_0;
    }
    else
        goto IR_case_1;
    #endif
    
    switch((IR >> 6) & 3)
    {
        IR_case_0:
        case 0: // Top bullshit
            switch(IR_column)
            {
                instr_0x0:
                case 0: // whatever
                    switch(IR_row)
                    {
                        case 0: // NOP
                            goto generic_fetch;
                        case 2: // STOP
                            goto generic_fetch_stop; // STOP is just bugged NOP, lol
                        
                        case 1: // LD a16, SP
                        {
                            data_wide = mch_memory_fetch_PC_op_2(mb);
                            
                            var SP = mb->SP;
                            
                            mch_memory_dispatch_write(mb, data_wide + 0, SP & 0xFF);
                            mch_memory_dispatch_write(mb, (data_wide + 1) & 0xFFFF, SP >> 8);
                            
                            ncycles += 2 + 2; // a16 op + n16 write
                            goto generic_fetch;
                        }
                        
                        case 3: // JR e8
                        generic_jr:
                        {
                            var PC = mb->PC;
                            data_wide = mch_memory_fetch_PC_op_1(mb);
                            
                            // Wedge if unbreakable spinloop is detected
                            // TODO: unfuck this statement
                            if(data_wide != 0xFE)
                            {
                                if(data_wide >= 0x80)
                                    data_wide += 0xFF00;
                                
                                mb->PC = (data_wide + PC + 1) & 0xFFFF;
                            }
                            else
                            {
                                if((!mb->IME && !mb->IME_ASK) || (!mb->IE && !mb->IF))
                                    return 0; // wedge until NMI
                                
                                mb->PC = PC - 1;
                            }
                            
                            ncycles += 2; // op fetch + ALU IDU Cy magic
                            goto generic_fetch;
                        }
                        
                        case 4: if(mbh_cc_check_0(mb->reg.F)) goto generic_jr; else goto instr_JNR_cc_e8_fail;
                        case 5:
#if defined(KNOVGBC_JR_Z_DIRECT)
                        {
                            /* Keep this inside the existing JR row: no
                             * unrelated opcode pays an extra comparison. */
                            if(mb->reg.F & MB_FLAG_Z)
                            {
                                var PC = mb->PC;
                                data_wide = mch_memory_fetch_PC_op_1(mb);

                                if(data_wide != 0xFE)
                                {
                                    if(data_wide >= 0x80)
                                        data_wide += 0xFF00;

                                    mb->PC = (data_wide + PC + 1) & 0xFFFF;
                                }
                                else
                                {
                                    if((!mb->IME && !mb->IME_ASK) || (!mb->IE && !mb->IF))
                                        return 0;

                                    mb->PC = PC - 1;
                                }

                                ncycles += 2;
                            }
                            else
                            {
                                /* A non-taken JR cc skips the operand. */
                                mb->PC = (mb->PC + 1) & 0xFFFF;
                                ncycles += 1;
                            }

                            goto generic_fetch;
                        }
#else
                            if(mbh_cc_check_1(mb->reg.F)) goto generic_jr; else goto instr_JNR_cc_e8_fail;
#endif
                        case 6: if(mbh_cc_check_2(mb->reg.F)) goto generic_jr; else goto instr_JNR_cc_e8_fail;
                        case 7: if(mbh_cc_check_3(mb->reg.F)) goto generic_jr; else goto instr_JNR_cc_e8_fail;
                            
                            {
                            instr_JNR_cc_e8_fail:
                                mb->PC = (mb->PC + 1) & 0xFFFF;
                                ncycles += 1; // op fetch only
                                goto generic_fetch;
                            }
                        
                        default:
                            __builtin_unreachable();
                    }
                
                case 1: // random r16 bullshit
                {
                    i_dst = (IR >> 4) & 3;
                    
                    if(i_dst != 3)
                        p_reg16_ptr = &mb->reg.raw16[i_dst];
                    else
                        p_reg16_ptr = &mb->SP;
                    
                    if(!(IR & 8)) // LD r16, n16
                    {
                        data_wide = mch_memory_fetch_PC_op_2(mb);
                        
                        *p_reg16_ptr = data_wide;
                        
                        ncycles += 2; // n16 op fetch
                        goto generic_fetch;
                    }
                    else // ADD HL, r16
                    {
                        data_reg = *p_reg16_ptr;
                        data_flags = mb->reg.F & MB_FLAG_Z;
                        
                        data_result = mb->reg.HL;
                        word mres = data_result + data_reg;
                        if(mres >> 16)
                            data_flags += MB_FLAG_C;
                        
                        mb->reg.HL = mres;
                        mb->reg.F = data_flags;
                        
                        mbh_fr_set_r16_add(mb, data_result, data_reg);
                        
                        ncycles += 2 - 1; // 2nd ALU cycle parallel with fetch
                        goto generic_fetch;
                    }
                }
                
                case 2: // LD r16 ptr
                {
                    i_dst = (IR >> 4) & 3;
                    
                    if(i_dst < 2)
                        p_reg16_ptr = &mb->reg.raw16[i_dst];
                    else
                        p_reg16_ptr = &mb->reg.HL;
                    
                    if(IR & 8) // load ptr
                    {
                        mb->reg.A = mch_memory_dispatch_read(mb, *p_reg16_ptr);
                    }
                    else // store ptr
                    {
                        mch_memory_dispatch_write(mb, *p_reg16_ptr, mb->reg.A);
                    }
                    
                    if(i_dst < 2)
                        ;
                    else
                    {
                        if(!(i_dst & 1))
                            ++*p_reg16_ptr;
                        else
                            --*p_reg16_ptr;
                    }
                    
                    ncycles += 1; // memory op
                    goto generic_fetch;
                }
                
                case 3: // INCDEC r16
                {
                    i_dst = (IR >> 4) & 3;
                    
                    if(i_dst != 3)
                        p_reg16_ptr = &mb->reg.raw16[i_dst];
                    else
                        p_reg16_ptr = &mb->SP;
                    
                    if(!(IR & 8)) // INC r16
                    {
                        ++*p_reg16_ptr;
                    }
                    else // DEC r16
                    {
                        --*p_reg16_ptr;
                    }
                    
                    ncycles += 1; // IDU post-incdec
                    goto generic_fetch;
                }
                
                case 4: // INC r8
                case 5: // DEC r8
                {
                    // Z1H-
                    
                    i_dst = IR_F_ROW ^ 1;
                    
                    data_flags = mb->reg.F & MB_FLAG_C;
                    
                    if(i_dst != 7)
                        data_reg = MB_REG8(mb, i_dst);
                    else
                    {
                        ++ncycles; // memory access
                        data_reg = mch_memory_dispatch_read(mb, mb->reg.HL);
                    }
                    
                    if(IR & 1) // DEC
                    {
                        data_flags += MB_FLAG_N;
                        
                        if((data_reg & 0xF) == 0)
                            data_flags += MB_FLAG_H;
                        
                        data_reg = (data_reg - 1) & 0xFF;
                    }
                    else // INC
                    {
                        data_reg = (data_reg + 1) & 0xFF;
                        
                        if((data_reg & 0xF) == 0)
                            data_flags += MB_FLAG_H;
                    }
                    
                    if(!data_reg)
                        data_flags += MB_FLAG_Z;
                    
                    mb->reg.F = data_flags;
                    mb->FMC_MODE = MB_FMC_MODE_NONE; // we calculate flags in-place
                    
                    if(i_dst != 7)
                        MB_REG8(mb, i_dst) = data_reg;
                    else
                    {
                        ++ncycles; // memory access
                        mch_memory_dispatch_write(mb, mb->reg.HL, data_reg);
                    }
                    
                    goto generic_fetch;
                }
                
                case 6: // LD r8, n8
                {
                    data_reg = mch_memory_fetch_PC_op_1(mb);
                    i_dst = IR_F_ROW ^ 1;
                    ncycles += 1; // n8 operand fetch
                    goto generic_r8_write;
                }
                
                case 7: // accumulator/flag operations
#if defined(KNOVGBC_HOT_COLD_SPLIT)
                    return mb_exec_acc_misc_cold(mb, IR);
#else
                    // just reimpl $CB func here, too much hassle to use goto
                    
                    switch(IR_row) 
                    {
                        case 0: // RLCA
                            data_reg = mb->reg.A;
                            data_flags = mb->reg.F;
                            data_flags &= MB_FLAG_C;
                            data_reg = (data_reg << 1) | (data_reg >> 7);
                            data_flags = (data_reg >> 4) & 0x10; // Cy flag
                            mb->reg.A = data_reg & 0xFF;
                            mb->reg.F = data_flags;
                            
                            mb->FMC_MODE = MB_FMC_MODE_NONE;
                            goto generic_fetch;
                        
                        case 1: // RRCA
                            data_reg = mb->reg.A;
                            data_flags = mb->reg.F;
                            data_flags &= MB_FLAG_C;
                            data_reg = (data_reg << 7) | (data_reg >> 1);
                            data_flags = (data_reg >> 3) & 0x10; // Cy flag
                            mb->reg.A = data_reg & 0xFF;
                            mb->reg.F = data_flags;
                            
                            mb->FMC_MODE = MB_FMC_MODE_NONE;
                            goto generic_fetch;
                        
                        case 2: // RLA
                            data_reg = mb->reg.A;
                            data_flags = mb->reg.F;
                            data_flags &= MB_FLAG_C;
                            data_reg = (data_reg << 1) | ((data_flags >> 4) & 1);
                            data_flags = (data_reg >> 4) & 0x10; // Cy flag
                            mb->reg.A = data_reg & 0xFF;
                            mb->reg.F = data_flags;
                            
                            mb->FMC_MODE = MB_FMC_MODE_NONE;
                            goto generic_fetch;
                        
                        case 3: // RRA
                            data_reg = mb->reg.A;
                            data_flags = mb->reg.F;
                            data_flags &= MB_FLAG_C;
                            data_reg = (data_reg << 8) | (data_reg >> 1) | ((data_flags & 0x10) << 3);
                            data_flags = (data_reg >> 4) & 0x10; // Cy flag
                            mb->reg.A = data_reg & 0xFF;
                            mb->reg.F = data_flags;
                            
                            mb->FMC_MODE = MB_FMC_MODE_NONE;
                            goto generic_fetch;
                        
                        case 4: // fuck DAA
                            data_reg = mb->reg.A;
                            data_flags = mb->reg.F;
                            data_flags &= ~MB_FLAG_Z;
                            
                            data_flags = mbh_fr_get(mb, data_flags);
                            
                            if(!(data_flags & MB_FLAG_N))
                            {
                                if((data_flags & MB_FLAG_H) || ((data_reg & 0xF) > 9))
                                {
                                    data_reg += 6;
                                    data_flags |= MB_FLAG_H;
                                }
                                
                                if((data_flags & MB_FLAG_C) || (((data_reg >> 4) & 0x1F) > 9))
                                {
                                    data_reg += 6 << 4;
                                    data_flags |= MB_FLAG_C;
                                }
                            }
                            else
                            {
                                // why the assymmetry???
                                
                                if((data_flags & MB_FLAG_H))// || ((wdat & 0xF) > 9))
                                {
                                    data_reg -= 6;
                                    data_flags |= MB_FLAG_H;
                                }
                                
                                if((data_flags & MB_FLAG_C))// || (((wdat >> 4) & 0x1F) > 9))
                                {
                                    data_reg -= 6 << 4;
                                    data_flags |= MB_FLAG_C;
                                }
                            }
                            
                            data_flags &= (MB_FLAG_C | MB_FLAG_N);
                            
                            data_reg &= 0xFF;
                            if(!data_reg)
                                data_flags |= MB_FLAG_Z;
                            mb->reg.A = data_reg;
                            mb->reg.F = data_flags;
                            
                            goto generic_fetch;
                        
                        case 5: // CPL A
                            mb->reg.F |= MB_FLAG_N | MB_FLAG_H;
                            mb->reg.A = ~mb->reg.A;
                            
                            mb->FMC_MODE = MB_FMC_MODE_NONE;
                            goto generic_fetch;
                        
                        case 6: // SET Cy
                            mb->reg.F = (mb->reg.F & MB_FLAG_Z) + MB_FLAG_C;
                            
                            mb->FMC_MODE = MB_FMC_MODE_NONE;
                            goto generic_fetch;
                        
                        case 7: // CPL Cy
                            mb->reg.F = (mb->reg.F ^ MB_FLAG_C) & (MB_FLAG_C | MB_FLAG_Z);
                            
                            mb->FMC_MODE = MB_FMC_MODE_NONE;
                            goto generic_fetch;
                        
                        default:
                            __builtin_unreachable();
                    }
#endif
            }
            return 0;
        
        IR_case_1:
        case 1: // MOV
            if(COMPILER_LIKELY(IR != 0x76))
            {
                {
                #if GBA
                    var vIR = IR ^ 9;
                    isrc = vIR & 7;
                    idst = (vIR >> 3) & 7;
                #else
                    i_src = IR_column ^ 1;
                    i_dst = IR_row ^ 1;
                #endif
                }
                
                if(i_src != 7)
                {
                    data_reg = MB_REG8(mb, i_src);
                }
                else
                {
                    ++ncycles; // memory access
                    data_reg = mch_memory_dispatch_read(mb, mb->reg.HL);
                }
                
            generic_r8_write:
                if(i_dst != 7)
                {
                    MB_REG8(mb, i_dst) = data_reg;
                }
                else
                {
                    ++ncycles; // memory access
                    mch_memory_dispatch_write(mb, mb->reg.HL, data_reg);
                }
                
                goto generic_fetch;
            }
            else
            {
                goto generic_fetch_halt;
            }
        
        IR_case_2:
        case 2: // ALU r8
        {
            // A is always the src and dst, thank fuck
            
            i_src = IR_F_COL ^ 1;
            
            if(i_src != 7)
            {
                data_reg = MB_REG8(mb, i_src);
            }
            else
            {
                ++ncycles; // memory access
                data_reg = mch_memory_dispatch_read(mb, mb->reg.HL);
            }
            
            alu_op_begin:
            data_result = mb->reg.A;
            
            switch(IR_F_ROW)
            {
                instr_ALU_0:
                case 0: // ADD Z0HC
                    mbh_fr_set_r8_add(mb, data_result, data_reg);
                    
                instr_ALU_0_cont:
                    data_result = data_result + data_reg;
                    if(data_result >> 8)
                        data_flags = MB_FLAG_C;
                    else
                        data_flags = 0;
                    
                    
                    break;
                
                case 1: // ADC Z0HC
                    if(mb->reg.F & 0x10)
                    {
                        mbh_fr_set_r8_adc(mb, data_result, data_reg);
                        data_reg += 1;
                        goto instr_ALU_0_cont;
                    }
                    else
                    {
                        goto instr_ALU_0;
                    }
                
                instr_ALU_2:
                case 2: // SUB Z1HC
                    mbh_fr_set_r8_sub(mb, data_result, data_reg);
                    
                instr_ALU_2_cont:
                    data_result = data_result - data_reg;
                    if(data_result >> 8)
                        data_flags = MB_FLAG_N | MB_FLAG_C;
                    else
                        data_flags = MB_FLAG_N;
                    
                    break;
                
                case 3: // SBC Z1HC
                    if(mb->reg.F & MB_FLAG_C)
                    {
                        mbh_fr_set_r8_sbc(mb, data_result, data_reg);
                        data_reg += 1;
                        goto instr_ALU_2_cont;
                    }
                    else
                    {
                        goto instr_ALU_2;
                    }
                
                case 7: // CMP Z1HC
                    mbh_fr_set_r8_sub(mb, data_result, data_reg);
                    
                    data_result = data_result - data_reg;
                    if(data_result >> 8)
                        data_flags = MB_FLAG_N | MB_FLAG_C;
                    else
                        data_flags = MB_FLAG_N;
                    
                    if(!(data_result & 0xFF))
                        data_flags += MB_FLAG_Z;
                    
                    mb->reg.F = data_flags;
                    
                    goto generic_fetch;
                
                case 4: // AND Z010
                    mb->FMC_MODE = MB_FMC_MODE_NONE;
                    
                    data_flags = MB_FLAG_H;
                    data_result &= data_reg;
                    break;
                
                case 5: // XOR Z000
                    mb->FMC_MODE = MB_FMC_MODE_NONE;
                    
                    data_flags = 0;
                    data_result ^= data_reg;
                    break;
                
                case 6: // ORR Z000
                    mb->FMC_MODE = MB_FMC_MODE_NONE;
                    
                    data_flags = 0;
                    data_result |= data_reg;
                    break;
                
                default:
                    __builtin_unreachable();
            }
            
            if(!(data_result & 0xFF))
                data_flags += MB_FLAG_Z;
            
            {
                hilow16_t sta;
                sta.low = data_result;
                sta.high = data_flags;
                mb->reg.hilo16[3] = sta; // REG_FA (yes, not AF)
            }
            
            goto generic_fetch;
        }
        
        IR_case_3:
        case 3: // Bottom bullshit
            switch(IR_column)
            {
                case 0: // misc junk and RET cc
                {
                    if(IR & 0x20) // misc junk (bottom 4)
                    {
                        if(!(IR & 8)) // LDH a8
                        {
                            data_wide = mch_memory_fetch_PC_op_1(mb);
                            
                            if(IR & 0x10)
                            {
                                instr_360:
                                DBGF("- /HR %04X -> ", data_wide + 0xFF00);
                                OPCODE_PROFILE_HADDR_READ(data_wide);
#if defined(KNOVGBC_LDH_HRAM_DIRECT)
                                /* LDH A,(a8) is a very frequent Tetris path.
                                 * Keep FF00-FF7F on the normal I/O callback
                                 * and FF on the established fallback; only
                                 * the ordinary HRAM window is a direct load. */
                                if((data_wide >= 0x80U) && (data_wide < 0xFFU))
                                    mb->reg.A = mb->mi->HRAM[data_wide];
                                else
#endif
                                mb->reg.A = mch_memory_dispatch_read_Haddr(mb, data_wide);
                                DBGF("%02X\n", mb->reg.A);
                            }
                            else
                            {
                                DBGF("- /HW %04X <- %02X\n", data_wide + 0xFF00, mb->reg.A);
                                OPCODE_PROFILE_HADDR_WRITE(data_wide);
                                mch_memory_dispatch_write_Haddr(mb, data_wide, mb->reg.A);
                            }
                            
                            ncycles += 2; // a8 op + memory access
                            goto generic_fetch;
                        }
                        else
                        {
                            data_wide = mch_memory_fetch_PC_op_1(mb);
                            if(data_wide >= 0x80)
                                data_wide += 0xFF00;
                            
                            data_reg = mb->SP;
                            //mbh_fr_set_r16_add_r8(mb, data_reg, data_wide);
                            mb->FMC_MODE = MB_FMC_MODE_NONE; // fuck this, the call rate is so low that it's cheaper to do this in-place
                            
                            data_flags = 0;
                            
                            if(((data_wide & 0xFF) + (data_reg & 0xFF)) >> 8)
                                data_flags += MB_FLAG_C;
                            
                            if(((data_wide & 0xF) + (data_reg & 0xF)) >> 4)
                                data_flags += MB_FLAG_H;
                            
                            data_wide = (data_wide + data_reg) & 0xFFFF;
                            
                            mb->reg.F = data_flags;
                            
                            if(IR & 0x10) // HL, SP+e8
                            {
                                mb->reg.HL = data_wide;
                                ncycles += 2; // operand + magic
                            }
                            else // SP, SP+e8
                            {
                                mb->SP = data_wide;
                                ncycles += 3; // operand + 2x ALU
                            }
                            
                            goto generic_fetch;
                        }
                    }
                    else // RET cc
                    {
                        ncycles += 1; // cc_check penalty cycle
                        
                        if(!MB_CC_CHECK)
                        {
                            goto generic_fetch;
                        }
                        else
                        {
                            goto generic_ret;
                        }
                    }
                }
                
                case 1: // POP r16 and junk
                {
                    if(IR & 8) // junk
                    {
                        switch((IR >> 4) & 3)
                        {
                            case 0: // RET
                            generic_ret:
                            {
                                var SP = mb->SP;
#if defined(KNOVGBC_STACK_PAIR_WRAM) || defined(KNOVGBC_STACK_PROFILE)
                                data_wide = mb_stack_read_pair(mb, SP);
                                SP += 2U;
#else
                                data_wide = mch_memory_dispatch_read(mb, SP++);
                                data_wide += mch_memory_dispatch_read(mb, (SP++) & 0xFFFF) << 8;
#endif
                                mb->SP = SP;
                                
                                mb->PC = data_wide;
                                
                                ncycles += 2 + 1; // 2x LD Imm.(L|H), [SP+] + MOV PC, Imm
                                goto generic_fetch;
                            }
                            
                            case 1: // IRET
                                mb->IME = 1;
                                mb->IME_ASK = 1;
                                goto generic_ret;
                            
                            case 2: // JP HL
                                mb->PC = mb->reg.HL;
                                goto generic_fetch; // no cycle penalty, IDU magic
                            
                            case 3: // MOV SP, HL
                                mb->SP = mb->reg.HL;
                                ncycles += 1; // cross-IDU wide bus move penalty
                                goto generic_fetch;
                            
                            default:
                                __builtin_unreachable();
                        }
                    }
                    else // POP r16
                    {
                        var SP = mb->SP;
#if defined(KNOVGBC_STACK_PAIR_WRAM) || defined(KNOVGBC_STACK_PROFILE)
                        data_wide = mb_stack_read_pair(mb, SP);
                        SP += 2U;
#else
                        data_wide = mch_memory_dispatch_read(mb, SP++);
                        data_wide += mch_memory_dispatch_read(mb, (SP++) & 0xFFFF) << 8;
#endif
                        mb->SP = SP;
                        
                        i_src = (IR >> 4) & 3;
                        if(i_src != 3)
                        {
                            mb->reg.raw16[i_src] = data_wide;
                        }
                        else
                        {
                            MB_AF_W(data_wide);
                            mb->FMC_MODE = MB_FMC_MODE_NONE; // overwritten manually from stack
                        }
                        
                        ncycles += 2; // 2x LD rD.(L|H), [SP+]
                        goto generic_fetch;
                    }
                }
                
                case 2: // LD mem or JP cc, a16
                {
                    if(IR & 0x20) // LD mem
                    {
                        if(IR & 8) // LD a16
                        {
                            data_wide = mch_memory_fetch_PC_op_2(mb);
                            
                            if(IR & 0x10)
                            {
                                instr_372:
                                mb->reg.A = mch_memory_dispatch_read(mb, data_wide);
                            }
                            else
                            {
                                mch_memory_dispatch_write(mb, data_wide, mb->reg.A);
                            }
                            
                            ncycles += 2 + 1; // a16 operand + memory access
                        }
                        else // LDH C
                        {
                            data_wide = mb->reg.C;
                            
                            if(!(IR & 0x10))
                            {
                                DBGF("- /HW %04X <- %02X\n", data_wide + 0xFF00, mb->reg.A);
                                OPCODE_PROFILE_HADDR_WRITE(data_wide);
                                mch_memory_dispatch_write_Haddr(mb, data_wide, mb->reg.A);
                            }
                            else
                            {
                                DBGF("- /HR %04X -> ", data_wide + 0xFF00);
                                OPCODE_PROFILE_HADDR_READ(data_wide);
                                mb->reg.A = mch_memory_dispatch_read_Haddr(mb, data_wide);
                                DBGF("%02X\n", mb->reg.A);
                            }
                            
                            ncycles += 1; // memory access
                        }
                        
                        goto generic_fetch;
                    }
                    else // JP cc, a16
                    {
                        //TODO: optimize this if possible
                        // small optimization, no imm fetch if no match
                        
                        if(MB_CC_CHECK)
                            goto generic_jp_abs;
                        else
                        {
                            mb->PC = (mb->PC + 2) & 0xFFFF;
                            ncycles += 2; // a16 fetch/"skip"
                            goto generic_fetch;
                        }
                    }
                }
                
                case 3: // junk
                {
                    switch(IR_F_ROW)
                    {
                        case 0: // JP a16
                        generic_jp_abs:
                        {
                            data_wide = mch_memory_fetch_PC_op_2(mb);
                            mb->PC = data_wide;
                            ncycles += 2 + 1; // a16 fetch + MOV PC, Imm through IDU
                            goto generic_fetch;
                        }
                        
                        case 1: // $CB
                            // Do not increment ncycles here,
                            //  it's handled in the $CB opcode handler
#if defined(KNOVGBC_CB_COLD) || defined(KNOVGBC_HOT_COLD_SPLIT)
                            return mb_exec_cb(mb);
#else
                            goto handle_cb;
#endif
                        
                        case 6: // DI
                            mb->IME = 0;
                            mb->IME_ASK = 0;
                            goto generic_fetch;
                        
                        case 7: // EI
                            mb->IME_ASK = 1;
                            goto generic_fetch;
                        
                        default:
                            return 0; //ILL
                    }
                }
                
                case 4: // CALL cc, a16
                {
                    // small optimization, do not fetch imm unless match
                    
                    if(!(IR & 32))
                    {
                        //TODO: optimize
                        
                        if(MB_CC_CHECK)
                            goto generic_call;
                        else
                        {
                            mb->PC = (mb->PC + 2) & 0xFFFF;
                            ncycles += 2; // a16 fetch/"skip"
                            goto generic_fetch;
                        }
                    }
                    else
                    {
                        return 0; //ILL
                    }
                }
                    
                case 5: // PUSH r16 / CALL a16
                {
                    if(!(IR & 8)) // PUSH r16
                    {
                        i_src = (IR >> 4) & 3;
                        if(i_src != 3)
                        {
                            data_wide = mb->reg.raw16[i_src];
                        }
                        else
                        {
                            mb->reg.F = mbh_fr_get(mb, mb->reg.F);
                            
                            data_wide = MB_AF_R;
                        }
                        
                    generic_push:
                        if(1)
                        {
                            var SP = mb->SP;
#if defined(KNOVGBC_STACK_PAIR_WRAM) || defined(KNOVGBC_STACK_PROFILE)
                            mb_stack_write_pair(mb, SP, data_wide);
                            SP -= 2U;
#else
                            mch_memory_dispatch_write(mb, (--SP) & 0xFFFF, data_wide >> 8);
                            mch_memory_dispatch_write(mb, (--SP) & 0xFFFF, data_wide & 0xFF);
#endif
                            mb->SP = SP;
                            ncycles += 2 + 1; // SP- + [SP-] = rS.H + [SP] = rS.L
                        }
                        
                        goto generic_fetch;
                    }
                    else
                    {
                        if(IR_row == 1) // CALL a16
                        {
                        generic_call:
                        {
                            var PC = mb->PC;
                            data_wide = mch_memory_fetch_PC_op_2(mb);
                            mb->PC = data_wide;
                            data_wide = (PC + 2) & 0xFFFF;
                            ncycles += 2; // a16 fetch
                            
                            goto generic_push;
                        }
                        }
                        else
                        {
                            return 0; // ILL
                        }
                    }
                }
                
                case 6: // ALU n8
                {
                    ncycles += 1; // n8 operand fetch
                    data_reg = mch_memory_fetch_PC_op_1(mb);
                    goto alu_op_begin;
                }
                
                case 7: // RST
                {
                    data_wide = mb->PC;
                    mb->PC = IR & (7 << 3);
                    goto generic_push; // ncycle penalty applied at label
                }
            }
            return 0;
    }
    
    //goto generic_fetch;
    return 0; // WTF
    
    generic_fetch:
    mb->IR.raw = mch_memory_fetch_PC(mb);
    return ncycles + 1;
    
    generic_fetch_halt:
    {
        mb->IR.raw = mch_memory_fetch_PC(mb);
        
        mb->IMM.high = 0x76;
        
        mb->HALTING = !(mb->IE & mb->IF);
        if(mb->HALTING)
        {
            //mb->IR.raw = 0; // NOP
            return 1;
        }
    }
        
    generic_fetch_stop:
    {
        mb->IR.raw = mch_memory_fetch_PC(mb);
        
        mb->IMM.high = 0x10;
        
        mb->HALTING = 1;
        if(mb->HALTING)
        {
            mb->IR.raw = 0; // NOP
            return 1;
        }
    }
    
#if !defined(KNOVGBC_CB_COLD) && !defined(KNOVGBC_HOT_COLD_SPLIT)
    handle_cb:
    if(1)
    {
        ncycles += 1; // $CB opcode fetch
        var CBIR = mch_memory_fetch_PC_op_1(mb);
        
    #if CONFIG_DBG
        if(_IS_DBG)
        {
            printf("               (%01o:%01o:%01o) ", CBIR >> 6, CBIR & 7, (CBIR >> 3) & 7);
            mb_disasm_CB(mb, CBIR);
        }
    #endif
    
        i_src = (CBIR >> 3) & 7;
        i_dst = (CBIR & 7) ^ 1;
        
        if(i_dst != 7)
            data_reg = MB_REG8(mb, i_dst);
        else
        {
            ++ncycles; // memory op fetch src
            data_reg = mch_memory_dispatch_read(mb, mb->reg.HL);
        }
        
        switch(CBIR >> 6)
        {
            case 0: // CB OP
                data_flags = mb->reg.F;
                data_flags &= MB_FLAG_C;
                
                switch(i_src)
                {
                    case 0: // RLC
                        data_reg = (data_reg << 1) | (data_reg >> 7);
                        data_flags = (data_reg >> 4) & 0x10; // Cy flag
                        break;
                    
                    case 1: // RRC
                        data_reg = (data_reg << 7) | (data_reg >> 1);
                        data_flags = (data_reg >> 3) & 0x10; // Cy flag
                        break;
                    
                    case 2: // RL
                        data_reg = (data_reg << 1) | ((data_flags >> 4) & 1);
                        data_flags = (data_reg >> 4) & 0x10; // Cy flag
                        break;
                    
                    case 3: // RR
                        data_reg = (data_reg << 8) | (data_reg >> 1) | ((data_flags & 0x10) << 3);
                        data_flags = (data_reg >> 4) & 0x10; // Cy flag
                        break;
                    
                    case 4: // LSL
                        data_reg = (data_reg << 1);
                        data_flags = (data_reg >> 4) & 0x10; // Cy flag
                        break;
                    
                    case 5: // ASR
                        data_flags = (data_reg & 1) << 4; // Cy flag
                        data_reg = (data_reg >> 1) | (data_reg & 0x80);
                        break;
                    
                    case 6: // SWAP
                        data_flags = 0;
                        data_reg = (data_reg >> 4) | (data_reg << 4);
                        break;
                    
                    case 7: // LSR
                        data_flags = (data_reg & 1) << 4; // Cy flag
                        data_reg = (data_reg >> 1);
                        break;
                }
                
                data_reg &= 0xFF;
                if(!data_reg)
                    data_flags += MB_FLAG_Z;
                
                mb->reg.F = data_flags;
                goto cb_writeback;
            
            case 1: // BTT
                data_flags = mb->reg.F;
                
                if(data_reg & (1 << i_src))
                    data_flags = (data_flags & MB_FLAG_C) + (MB_FLAG_H);
                else
                    data_flags = (data_flags & MB_FLAG_C) + (MB_FLAG_H | MB_FLAG_Z);
                
                mb->reg.F = data_flags;
                
                goto generic_fetch;
            
            case 2: // RES
                data_reg &= ~(1 << i_src);
                goto cb_writeback;
            
            case 3: // SET
                data_reg |= (1 << i_src);
                goto cb_writeback;
        }
        
        return 0; // WTF
        
        cb_writeback:
        
        if(i_dst != 7)
            MB_REG8(mb, i_dst) = data_reg;
        else
        {
            ++ncycles; // memory op src writeback
            mch_memory_dispatch_write(mb, mb->reg.HL, data_reg);
        }
        
        goto generic_fetch;
    }
#endif
    /* All normal paths return through generic_fetch or the CB handler.
     * Keep an explicit fallback for the compiler when the inline handler is
     * omitted in the KNOVGBC_CB_COLD build. */
    return 0; // WTF
}

#undef MB_EXEC_COMPACT_ATTRIBUTES
