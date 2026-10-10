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
#include "microcode.h"
#include "microcode_dispatch.h"
#include "dbg.h"

/* Only the O3 component-profiling image emits these hooks. They sample whole
 * memory dispatch operations at a low rate so the profiler does not dominate
 * the SH-2 workload it is meant to measure. */
#if defined(KNOVGBC_COMPONENT_PROFILE)
#include "mvp_profiler.h"
/* These functions intentionally use GCC's local -Os attribute. Yaul's
 * cpu_frt_count_get() is always-inline for the default target options and
 * cannot be inlined into that differing function target, so read the same
 * two FRT registers directly here. */
#define MEMORY_PROFILE_TICKS_GET() \
    ((uint16_t)((((volatile cpu_ioregs_t *)CPU_IOREG_BASE)->frch << 8) | \
        ((volatile cpu_ioregs_t *)CPU_IOREG_BASE)->frcl))
#define MEMORY_PROFILE_BEGIN() \
    struct mvp_profiler_component_bucket *const memory_profile_bucket = \
        &mvp_profiler_component_buckets[MVP_PROFILER_COMPONENT_MEMORY]; \
    const uint32_t memory_profile_calls = ++memory_profile_bucket->calls; \
    uint32_t memory_profile_token = 0U; \
    if ((memory_profile_calls & 0x7FU) == 0U) \
        memory_profile_token = (uint32_t)MEMORY_PROFILE_TICKS_GET() + 1U
#define MEMORY_PROFILE_END() \
    do { \
        if (memory_profile_token != 0U) { \
            const uint16_t memory_profile_start = \
                (uint16_t)(memory_profile_token - 1U); \
            memory_profile_bucket->sampled_ticks += \
                (uint16_t)(MEMORY_PROFILE_TICKS_GET() - memory_profile_start); \
            memory_profile_bucket->samples++; \
        } \
    } while (0)
#else
#define MEMORY_PROFILE_BEGIN()
#define MEMORY_PROFILE_END()
#endif

#if defined(KNOVGBC_OPCODE_PROFILE)
#include "picogb_opcode_profile.h"
/* Data reads and writes are sampled at the public dispatcher gateways. High
 * byte instructions that bypass those gateways record themselves in
 * microcode.c, avoiding both a blind spot and a doubled FFxx transaction. */
#define OPCODE_PROFILE_READ(address) \
    picogb_opcode_profile_memory_record( \
        PICOGB_OPCODE_PROFILE_ACCESS_READ, (uint16_t)(address))
#define OPCODE_PROFILE_WRITE(address) \
    picogb_opcode_profile_memory_record( \
        PICOGB_OPCODE_PROFILE_ACCESS_WRITE, (uint16_t)(address))
#else
#define OPCODE_PROFILE_READ(address)
#define OPCODE_PROFILE_WRITE(address)
#endif


#define self mb_state* __restrict

#define USE_MIC struct mb_mi_cache* __restrict mic = &mb->micache
#define USE_MI struct mi_dispatch* __restrict mi = mb->mi

//#define R_RELATIVE_ADDR (addr & MICACHE_R_SEL)
#define R_RELATIVE_ADDR (addr - (r_addr << MICACHE_R_BITS))

/*
 * Optional direct map for normal Game Boy memory.
 *
 * This is deliberately not a Tetris shortcut. It works with a contiguous
 * ROM image and the core's existing BANK_ROM/BANK_VRAM/BANK_WRAM/BANK_SRAM
 * fields, so MBC0, MBC1, MBC2, MBC3 and MBC5 continue to select their banks
 * through the normal mapper-write callbacks. I/O and writes to the ROM area
 * still take their original paths. Non-flat ROM loaders and boot-ROM builds
 * retain the established cache-based dispatcher below.
 */
#if defined(KNOVGBC_FAST_MEMORY) && CONFIG_USE_FLAT_ROM && \
    CONFIG_NOBOOTMEME && !CONFIG_ENABLE_LRU
#define PGB_FAST_MEMORY 1
#define FAST_MEMORY_FUNCTION_OPT __attribute__((optimize("O3")))
#else
#define PGB_FAST_MEMORY 0
#define FAST_MEMORY_FUNCTION_OPT __attribute__((optimize("Os")))
#endif


#pragma region Resolve uncached region + fabric interface

#if CONFIG_ENABLE_LRU
// Resolve an aligned(!) pointer to a ROM bank,
//  based on an input address and current banking settings.
// addr < 0x8000
//TODO: get rid of this
PGB_FUNC static inline const r8* __restrict mch_resolve_mic_bank_internal(const self mb, word r_addr)
{
    USE_MI;
    
    const r8* __restrict ret = mi->dispatch_ROM_Bank(mi->userdata, r_addr << MICACHE_R_BITS, mi->BANK_ROM);
    
    return ret;
}
#endif

PGB_FUNC static r8* __restrict mch_resolve_mic_r_direct(const self mb, word r_addr)
{
    USE_MI;
    
    if(r_addr < MICACHE_R_VALUE(0x8000))
    {
        // ROM area is unpredictable, can't calculate it here
        __builtin_unreachable();
    }
    else if(r_addr < MICACHE_R_VALUE(0xA000))
    {
        r8* __restrict ptr = &mi->VRAM[mi->BANK_VRAM << 13];
        
        r_addr -= MICACHE_R_VALUE(0x8000);
        return &(ptr[r_addr << MICACHE_R_BITS]);
    }
    else if(r_addr < MICACHE_R_VALUE(0xC000))
    {
        r8* __restrict ptr = &mi->SRAM[mi->BANK_SRAM << 13];
        
        r_addr -= MICACHE_R_VALUE(0xA000);
        return &(ptr[r_addr << MICACHE_R_BITS]);
    }
    else // WRAM only [$C000; $FFFF] for OAMDMA
    {
        if(COMPILER_UNLIKELY(r_addr >= MICACHE_R_VALUE(0x10000)))
            __builtin_unreachable();
        
        if(r_addr < MICACHE_R_VALUE(0xE000))
            r_addr -= MICACHE_R_VALUE(0xC000);
        else
            r_addr -= MICACHE_R_VALUE(0xE000);
        
        if(r_addr < MICACHE_R_VALUE(0x1000))
        {
            return &(mi->WRAM[r_addr << MICACHE_R_BITS]);
        }
        else
        {
            var bank = mi->BANK_WRAM;
            if(!bank)
                bank = 1;
            
            r_addr -= MICACHE_R_VALUE(0x1000);
            return &(mi->WRAM[(bank << 12) + (r_addr << MICACHE_R_BITS)]);
        }
    }
}

PGB_FUNC static const r8* __restrict mch_resolve_mic_r_direct_ROM(const self mb, word r_addr)
{
    USE_MI;
    
    const r8* __restrict ret = NULL;
    
    //TODO: optimize this for LRU
#if CONFIG_ENABLE_LRU
    if(mi->ROM != NULL)
#endif
    {
        if(r_addr < MICACHE_R_VALUE(0x4000))
        {
        #if CONFIG_USE_FLAT_ROM
            ret = &mi->ROM[0];
            return &ret[r_addr << MICACHE_R_BITS];
        #else
            ret = mi->ROM[0];
            if(ret != NULL)
                return &ret[r_addr << MICACHE_R_BITS];
        #endif
        }
        else
        {
        #if CONFIG_USE_FLAT_ROM
            r_addr -= MICACHE_R_VALUE(0x4000);
            ret = &mi->ROM[mi->BANK_ROM << 14];
            return &ret[r_addr << MICACHE_R_BITS];
        #else
            ret = mi->ROM[mi->BANK_ROM];
            if(ret != NULL)
            {
                r_addr -= MICACHE_R_VALUE(0x4000);
                return &ret[r_addr << MICACHE_R_BITS];
            }
        #endif
        }
    }
    
#if CONFIG_ENABLE_LRU
    ret = mch_resolve_mic_bank_internal(mb, r_addr);
#endif
    return ret;
}

// Uncached resolve aligned readable const memory area, based on address
PGB_FUNC ATTR_FORCE_NOINLINE static const r8* __restrict mch_resolve_mic_r_direct_read(const self mb, word r_addr)
{
    if(r_addr < MICACHE_R_VALUE(0x8000))
    {
        return mch_resolve_mic_r_direct_ROM(mb, r_addr);
    }
    
    return mch_resolve_mic_r_direct(mb, r_addr);
}

PGB_FUNC ATTR_FORCE_NOINLINE static r8* __restrict mch_resolve_mic_r_direct_write(const self mb, word r_addr)
{
    if(r_addr < MICACHE_R_VALUE(0x8000))
    {
        __builtin_unreachable();
        
        // ROM is *Read-Only* Memory, can't write to it normally
        return NULL;
    }
    
    return mch_resolve_mic_r_direct(mb, r_addr);
}

#pragma endregion

#pragma region Resolve cached memory region (by address)
/*
    All functions in this region: cached memory resolve
    - addr < 0xE000
*/

#pragma region Resolve (read)

PGB_FUNC ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) static const r8* __restrict mch_resolve_mic_read_slow(self mb, word addr)
{
    const r8* __restrict ptr;
    
    var r_addr = MICACHE_R_VALUE(addr);
    
    ptr = mch_resolve_mic_r_direct_read(mb, r_addr);
    if(COMPILER_LIKELY(ptr != NULL))
    {
#if !CONFIG_MIC_CACHE_BYPASS
        USE_MIC;
        mic->mc_read[r_addr] = ptr;
#endif
        
        return &ptr[R_RELATIVE_ADDR];
    }
    
    return NULL;
}

PGB_FUNC ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) static word mch_resolve_mic_read_slow_deref(self mb, word addr)
{
    return *mch_resolve_mic_read_slow(mb, addr);
}

PGB_FUNC ATTR_HOT __attribute__((optimize("O2"))) static word mch_resolve_mic_read_deref(self mb, word addr)
{
#if !CONFIG_MIC_CACHE_BYPASS
    var r_addr = MICACHE_R_VALUE(addr);
    
    const r8* __restrict ptr;
    
    ptr = mb->micache.mc_read[r_addr];
    if(COMPILER_LIKELY(ptr != NULL))
        return ptr[R_RELATIVE_ADDR];
    else
#endif
    
    return mch_resolve_mic_read_slow_deref(mb, addr);
}

#pragma endregion

#pragma region Resolve (write)

PGB_FUNC ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) static void mch_resolve_mic_write_slow_deref(self mb, word addr, word data)
{
    USE_MIC;
    
    var r_addr = MICACHE_R_VALUE(addr);
    
    r8* __restrict ptr;
    ptr = mch_resolve_mic_r_direct_write(mb, r_addr);
    if(COMPILER_LIKELY(ptr != NULL))
    {
        mic->mc_write[r_addr] = ptr;
        ptr[R_RELATIVE_ADDR] = data;
        return;
    }
    
    __builtin_unreachable();
    *ptr = data;
}

PGB_FUNC ATTR_HOT __attribute__((optimize("Os"))) static void mch_resolve_mic_write_deref_helper(r8* __restrict ptr, word addr, word data)
{
    var r_addr = MICACHE_R_VALUE(addr);
    addr = R_RELATIVE_ADDR;
    ptr[addr] = data;
}

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("O2"))) static void mch_resolve_mic_write_deref(self mb, word addr, word data)
{
#if !CONFIG_MIC_CACHE_BYPASS
    var r_addr = MICACHE_R_VALUE(addr);
    
    r8* __restrict ptr;
    
    ptr = mb->micache.mc_write[r_addr];
    if(ptr != NULL)
    {
        mch_resolve_mic_write_deref_helper(ptr, addr, data); //HACK: prevent register allocator spill, as it's slower than a tail call
        return;
    }
    else
#endif
    
    mch_resolve_mic_write_slow_deref(mb, addr, data);
}

#pragma endregion

#pragma region Resolve (execute)

PGB_FUNC ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) static const r8* __restrict mch_resolve_mic_execute_slow(self mb, word addr)
{
    const r8* __restrict ptr;
    
    var r_addr = MICACHE_R_VALUE(addr);
    
    ptr = mch_resolve_mic_r_direct_read(mb, r_addr);
    if(COMPILER_LIKELY(ptr != NULL))
    {
#if !CONFIG_MIC_CACHE_BYPASS
        USE_MIC;
        mic->mc_execute[r_addr] = ptr;
#endif
        
        return &ptr[R_RELATIVE_ADDR];
    }
    
    return NULL;
}

PGB_FUNC ATTR_HOT ATTR_FORCE_INLINE __attribute__((optimize("O2"))) static inline const r8* __restrict mch_resolve_mic_execute(self mb, word addr)
{
    
#if !CONFIG_MIC_CACHE_BYPASS
    var r_addr = MICACHE_R_VALUE(addr);
    
    const r8* __restrict ptr;
    
    ptr = mb->micache.mc_execute[r_addr];
    if(COMPILER_LIKELY(ptr != NULL))
        return &ptr[R_RELATIVE_ADDR];
#endif
    
    return mch_resolve_mic_execute_slow(mb, addr);
}

PGB_FUNC ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) static word mch_resolve_mic_execute_slow_deref(self mb, word addr)
{
    return *mch_resolve_mic_execute_slow(mb, addr);
}

PGB_FUNC ATTR_HOT ATTR_FORCE_INLINE __attribute__((optimize("O2"))) static inline word mch_resolve_mic_execute_deref(self mb, word addr)
{
#if !CONFIG_MIC_CACHE_BYPASS
    var r_addr = MICACHE_R_VALUE(addr);
    
    const r8* __restrict ptr;
    
    ptr = mb->micache.mc_execute[r_addr];
    if(COMPILER_LIKELY(ptr != NULL))
        return ptr[R_RELATIVE_ADDR];
    else
#endif
    
    return mch_resolve_mic_execute_slow_deref(mb, addr);
}

#pragma endregion

#pragma endregion

#pragma region Dispatch

#pragma region Dispatch special (IO + ROM)

PGB_FUNC ATTR_FORCE_NOINLINE  __attribute__((optimize("O2"))) static void mch_memory_dispatch_write_ROM(const self mb, word addr, word data)
{
    USE_MI;
    mi->dispatch_ROM(mi->userdata, addr, data, MB_TYPE_WRITE);
}

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("O2"))) static word mch_memory_dispatch_read_IO(const self mb, word haddr)
{
    USE_MI;
    return mi->dispatch_IO(mi->userdata, haddr, MB_DATA_DONTCARE, MB_TYPE_READ);
}

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE  __attribute__((optimize("O2"))) static void mch_memory_dispatch_write_IO(const self mb, word haddr, word data)
{
    USE_MI;
    mi->dispatch_IO(mi->userdata, haddr, data, MB_TYPE_WRITE);
}

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("O2"))) static word mch_memory_dispatch_read_HRAM(const self mb, word haddr)
{
    return mb->mi->HRAM[haddr];
}

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) word mch_memory_dispatch_read_Haddr(const self mb, word haddr)
{
    if(haddr < 0x80)
        return mch_memory_dispatch_read_IO(mb, haddr);
    else
        return mch_memory_dispatch_read_HRAM(mb, haddr);
}

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) void mch_memory_dispatch_write_Haddr(self mb, word haddr, word data)
{
    if(haddr < 0x80)
        mch_memory_dispatch_write_IO(mb, haddr, data);
    else
    {
        if(haddr < 0xFF)
            ;
        else
            mb->IE = data;
        
        mb->mi->HRAM[haddr] = data;
    }
}

PGB_FUNC ATTR_FORCE_NOINLINE __attribute__((optimize("O2"))) static word mch_memory_dispatch_read_fexx_ffxx(const self mb, word addr)
{
    if(addr & 0x100)
        return mch_memory_dispatch_read_Haddr(mb, addr & 0xFF);
    
    return mb->mi->OAM[addr & 0xFF];
}

PGB_FUNC ATTR_FORCE_NOINLINE __attribute__((optimize("O2"))) static void mch_memory_dispatch_write_fexx_ffxx(self mb, word addr, word data)
{
    if(addr & 0x100)
    {
        mch_memory_dispatch_write_Haddr(mb, addr & 0xFF, data);
        return;
    }
    
    mb->mi->OAM[addr & 0xFF] = data;
}

#if PGB_FAST_MEMORY
/* The direct path keeps the observable mapping of the normal dispatcher but
 * avoids a 4 KiB page-cache lookup and its resolver call for every ordinary
 * byte. The switchable ROM area, VRAM/WRAM/SRAM banks and the E000-FDFF echo
 * all continue to use the state selected by the regular mapper and I/O code.
 */
PGB_FUNC ATTR_HOT static inline FAST_MEMORY_FUNCTION_OPT word
mch_fast_memory_read(self mb, word addr)
{
    const struct mi_dispatch *const mi = mb->mi;

    if (addr < 0x4000U) {
        return mi->ROM[addr];
    }
    if (addr < 0x8000U) {
        return mi->ROM[(mi->BANK_ROM << 14) | (addr & 0x3FFFU)];
    }
    if (addr < 0xA000U) {
        return mi->VRAM[(mi->BANK_VRAM << 13) | (addr & 0x1FFFU)];
    }
    if (addr < 0xC000U) {
        return mi->SRAM[(mi->BANK_SRAM << 13) | (addr & 0x1FFFU)];
    }
    if (addr < 0xD000U) {
        return mi->WRAM[addr & 0x0FFFU];
    }
    if (addr < 0xE000U) {
        word bank = mi->BANK_WRAM;
        if (bank == 0U) {
            bank = 1U;
        }
        return mi->WRAM[(bank << 12) | (addr & 0x0FFFU)];
    }
    if (addr < 0xF000U) {
        return mi->WRAM[addr & 0x0FFFU];
    }
    if (addr < 0xFE00U) {
        word bank = mi->BANK_WRAM;
        if (bank == 0U) {
            bank = 1U;
        }
        return mi->WRAM[(bank << 12) | (addr & 0x0FFFU)];
    }

    return mch_memory_dispatch_read_fexx_ffxx(mb, addr);
}

/* Keep the uncommon RAM/HRAM opcode case out of the instruction-fetch
 * routine.  It still uses the exact same complete map as a data read. */
PGB_FUNC __attribute__((noinline)) FAST_MEMORY_FUNCTION_OPT static word
mch_fast_memory_fetch_byte_slow(self mb, word addr)
{
    return mch_fast_memory_read(mb, addr);
}

/* Opcode bytes overwhelmingly live in ROM.  Keep this classifier deliberately
 * smaller than the general data map so the most frequent memory operation
 * does not pull every VRAM/WRAM/I/O branch into the interpreter's cache
 * footprint.  Code running from RAM or HRAM remains valid through the full
 * map below. */
PGB_FUNC ATTR_HOT static inline FAST_MEMORY_FUNCTION_OPT word
mch_fast_memory_fetch_byte(self mb, word addr)
{
    const struct mi_dispatch *const mi = mb->mi;

    if (addr < 0x4000U) {
        return mi->ROM[addr];
    }
    if (addr < 0x8000U) {
        return mi->ROM[(mi->BANK_ROM << 14) | (addr & 0x3FFFU)];
    }

    return mch_fast_memory_fetch_byte_slow(mb, addr);
}

PGB_FUNC ATTR_HOT static inline FAST_MEMORY_FUNCTION_OPT void
mch_fast_memory_write(self mb, word addr, word data)
{
    struct mi_dispatch *const mi = mb->mi;

    if (addr < 0x8000U) {
        /* Mapper registers occupy the apparent ROM address range. */
        mch_memory_dispatch_write_ROM(mb, addr, data);
        return;
    }
    if (addr < 0xA000U) {
#if defined(KNOVGBC_PPU_DIRTY_SCANLINES)
        r8 *const target = &mi->VRAM[(mi->BANK_VRAM << 13) |
            (addr & 0x1FFFU)];
        const r8 previous = *target;
        *target = (r8)data;
        if ((previous != (r8)data) &&
            (mi->dispatch_VRAM_write != NULL)) {
            mi->dispatch_VRAM_write(mi->userdata, addr & 0x1FFFU);
        }
#else
        mi->VRAM[(mi->BANK_VRAM << 13) | (addr & 0x1FFFU)] = data;
#endif
        return;
    }
    if (addr < 0xC000U) {
        mi->SRAM[(mi->BANK_SRAM << 13) | (addr & 0x1FFFU)] = data;
        return;
    }
    if (addr < 0xD000U) {
        mi->WRAM[addr & 0x0FFFU] = data;
        return;
    }
    if (addr < 0xE000U) {
        word bank = mi->BANK_WRAM;
        if (bank == 0U) {
            bank = 1U;
        }
        mi->WRAM[(bank << 12) | (addr & 0x0FFFU)] = data;
        return;
    }
    if (addr < 0xF000U) {
        mi->WRAM[addr & 0x0FFFU] = data;
        return;
    }
    if (addr < 0xFE00U) {
        word bank = mi->BANK_WRAM;
        if (bank == 0U) {
            bank = 1U;
        }
        mi->WRAM[(bank << 12) | (addr & 0x0FFFU)] = data;
        return;
    }

    mch_memory_dispatch_write_fexx_ffxx(mb, addr, data);
}

/* Keep the rare cross-region case out of the opcode-fetch hot path.  Inlining
 * the complete 64 KiB map twice made this helper larger than the useful
 * portion of the SH-2 instruction cache, even though normal operands live in
 * a single ROM bank. */
PGB_FUNC __attribute__((noinline)) FAST_MEMORY_FUNCTION_OPT static word
mch_fast_memory_fetch_word_slow(self mb, word addr)
{
    return mch_fast_memory_read(mb, addr) |
        ((word)mch_fast_memory_read(mb, (addr + 1U) & 0xFFFFU) << 8);
}

PGB_FUNC ATTR_HOT static inline FAST_MEMORY_FUNCTION_OPT word
mch_fast_memory_fetch_word(self mb, word addr)
{
    const struct mi_dispatch *const mi = mb->mi;

    /* Most immediate operands are consecutive bytes in the currently mapped
     * ROM bank. Keep that overwhelmingly common case to two byte loads. */
    if (addr < 0x3FFFU) {
        return mi->ROM[addr] | ((word)mi->ROM[addr + 1U] << 8);
    }
    if ((addr >= 0x4000U) && (addr < 0x7FFFU)) {
        const word offset = (mi->BANK_ROM << 14) | (addr & 0x3FFFU);
        return mi->ROM[offset] | ((word)mi->ROM[offset + 1U] << 8);
    }

    return mch_fast_memory_fetch_word_slow(mb, addr);
}
#endif

#pragma endregion

#pragma region Dispatch fetch (1)

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) static word mch_memory_fetch_decode_1(self mb, word addr)
{
    if(COMPILER_LIKELY(addr < 0xFE00))
        return mch_resolve_mic_execute_deref(mb, addr);
    else
        return mch_memory_dispatch_read_fexx_ffxx(mb, addr);
}

PGB_FUNC ATTR_HOT word mch_memory_fetch_PC_op_1(self mb)
{
    MEMORY_PROFILE_BEGIN();
    word addr = mb->PC;
    mb->PC = (addr + 1) & 0xFFFF;
    
#if PGB_FAST_MEMORY
    var res = mch_fast_memory_fetch_byte(mb, addr);
#else
    var res = mch_memory_fetch_decode_1(mb, addr);
#endif
    DBGF("- /O1 %04X <> %02X\n", addr, res);
    MEMORY_PROFILE_END();
    return res;
}

PGB_FUNC ATTR_HOT word mch_memory_fetch_PC(self mb)
{
    MEMORY_PROFILE_BEGIN();
    word addr = mb->PC;
    mb->PC = (addr + 1) & 0xFFFF;
    
#if PGB_FAST_MEMORY && defined(KNOVGBC_OPCODE_FETCH_INLINE)
    /*
     * The normal fast classifier below is deliberately a small callable
     * helper, because operand fetches use it from several places.  The
     * prefetched opcode is different: every completed SM83 instruction
     * reaches this one shared site.  Keep the two ROM cases here so the
     * Master SH-2 does not pay a JSR/RTS pair for every next opcode.
     *
     * The addresses and bank calculation intentionally match
     * mch_fast_memory_fetch_byte() exactly.  Code executed from VRAM, WRAM,
     * HRAM or an I/O-adjacent address still takes the complete slow map.
     */
    const struct mi_dispatch *const mi = mb->mi;
    var res;

    if (addr < 0x4000U) {
        res = mi->ROM[addr];
    }
    else if (addr < 0x8000U) {
        res = mi->ROM[(mi->BANK_ROM << 14) | (addr & 0x3FFFU)];
    }
    else {
        res = mch_fast_memory_fetch_byte_slow(mb, addr);
    }
#elif PGB_FAST_MEMORY
    var res = mch_fast_memory_fetch_byte(mb, addr);
#else
    var res = mch_memory_fetch_decode_1(mb, addr);
#endif
    DBGF("- /M1 %04X <> %02X\n", addr, res);
    MEMORY_PROFILE_END();
    return res;
}

#pragma endregion

#pragma region Dispatch fetch (2)

PGB_FUNC ATTR_FORCE_NOINLINE __attribute__((optimize("O1"))) static word mch_memory_fetch_decode_2_slow(self mb, word addr)
{
    word addr2 = (addr + 1) & 0xFFFF;
    
    word res1 = mch_memory_fetch_decode_1(mb, addr);
    word res2 = mch_memory_fetch_decode_1(mb, addr2);
    
    var res = res1;
    res += (res2) << 8;
    
    return res;
}

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("O1"))) static word mch_resolve_mic_execute_deref_2_aligned(self mb, word addr)
{
    // This may have to be volatile, otherwise
    //  an LDRH is emitted, which is no cool, as
    //  the pointer will very likely be not aligned.
    // This sadly wastes precious CPU cycles,
    //  but it's necessary to avoid unaligned data abort.
    const r8* __restrict ptr = mch_resolve_mic_execute(mb, addr);
    
    word nres = ptr[0];
    nres += ptr[1] << 8;
    
    return nres;
}

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE __attribute__((optimize("Os"))) static word mch_memory_fetch_decode_2(self mb, word addr)
{
#if !CONFIG_MIC_CACHE_BYPASS
    
    if(COMPILER_LIKELY(addr < 0xFE00)) // yeah, this is an off-by-one error, and I don't care
    {
        //word r1 = MICACHE_R_VALUE(addr);
        //word r2 = MICACHE_R_VALUE(addr + 1);
        //if(r1 == r2)
        
        if(COMPILER_LIKELY((addr + 1) & MICACHE_R_SEL)) // same as above commented code
        {
            return mch_resolve_mic_execute_deref_2_aligned(mb, addr);
        }
    }
#endif
    
    return mch_memory_fetch_decode_2_slow(mb, addr);
}

PGB_FUNC ATTR_HOT word mch_memory_fetch_PC_op_2(self mb)
{
    MEMORY_PROFILE_BEGIN();
    word addr = mb->PC;
    mb->PC = (addr + 2) & 0xFFFF;
    
#if PGB_FAST_MEMORY
    word resp = mch_fast_memory_fetch_word(mb, addr);
#else
    word resp = mch_memory_fetch_decode_2(mb, addr);
#endif
    DBGF("- /O2 %04X <> %04X\n", addr, resp);
    MEMORY_PROFILE_END();
    return resp;
}


#pragma endregion

#pragma region Dispatch read and write

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE FAST_MEMORY_FUNCTION_OPT void mch_memory_dispatch_write(self mb, word addr, word data)
{
    MEMORY_PROFILE_BEGIN();
    OPCODE_PROFILE_WRITE(addr);
    DBGF("- /WR %04X <- %02X\n", addr, data);

#if PGB_FAST_MEMORY
    mch_fast_memory_write(mb, addr, data);
#else
    if(COMPILER_LIKELY(addr >= 0x8000))
    {
        if(COMPILER_LIKELY(addr < 0xFE00))
            mch_resolve_mic_write_deref(mb, addr, data);
        else
            mch_memory_dispatch_write_fexx_ffxx(mb, addr, data);
    }
    else
        mch_memory_dispatch_write_ROM(mb, addr, data);
#endif

#if defined(KNOVGBC_PPU_FULL_OPT) || defined(KNOVGBC_PPU_DIRTY_SCANLINES) || defined(KNOVGBC_SLAVE_PPU_SHADOW)
    /* The fast-memory route writes VRAM/OAM directly, so cache invalidation
     * cannot live only in the fabric I/O callback. Notify the renderer after
     * the architectural write has completed. */
#if !defined(KNOVGBC_PPU_DIRTY_SCANLINES) || !PGB_FAST_MEMORY
    if ((addr >= 0x8000U) && (addr < 0xA000U)) {
        struct mi_dispatch *const mi = mb->mi;
        if (mi->dispatch_VRAM_write != NULL) {
            mi->dispatch_VRAM_write(mi->userdata, addr & 0x1FFFU);
        }
    }
    else
#endif
    if ((addr >= 0xFE00U) && (addr < 0xFF00U)) {
        struct mi_dispatch *const mi = mb->mi;
        if (mi->dispatch_OAM_write != NULL) {
            mi->dispatch_OAM_write(mi->userdata, addr & 0x00FFU);
        }
    }
#endif

    MEMORY_PROFILE_END();
}

#if !CONFIG_DBG
#define mch_memory_dispatch_read_ mch_memory_dispatch_read
#endif

PGB_FUNC ATTR_HOT ATTR_FORCE_NOINLINE FAST_MEMORY_FUNCTION_OPT word mch_memory_dispatch_read_(self mb, word addr)
{
    MEMORY_PROFILE_BEGIN();
    OPCODE_PROFILE_READ(addr);
    word result;

#if PGB_FAST_MEMORY
    result = mch_fast_memory_read(mb, addr);
#else
    if(COMPILER_LIKELY(addr < 0xFE00))
        result = mch_resolve_mic_read_deref(mb, addr);
    else
        result = mch_memory_dispatch_read_fexx_ffxx(mb, addr);
#endif

    MEMORY_PROFILE_END();
    return result;
}

#if CONFIG_DBG
PGB_FUNC ATTR_HOT word mch_memory_dispatch_read(self mb, word addr)
{
    DBGF("- /RD %04X -> ", addr);
    word res = mch_memory_dispatch_read_(mb, addr);
    DBGF("%02X\n", res);
    return res;
}
#endif

#pragma endregion

#pragma endregion

