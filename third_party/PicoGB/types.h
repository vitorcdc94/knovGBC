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
#pragma once

#include "config.h"


#define ATTR_HOT __attribute__((hot))

//#define ATTR_FORCE_INLINE __attribute__((always_inline))
#define ATTR_FORCE_INLINE
//#define ATTR_FORCE_NOINLINE __attribute__((noinline))
#define ATTR_FORCE_NOINLINE 
//#define COMPILER_VARIABLE_BARRIER(var) (void)(var)
#define COMPILER_VARIABLE_BARRIER(var) __asm volatile(""::"r"(var))
//#define COMPILER_MEMORY_BARRIER() 
#define COMPILER_MEMORY_BARRIER() __asm volatile("":::"memory")
//#define COMPILER_UNLIKELY(cond) cond
#define COMPILER_UNLIKELY(cond) __builtin_expect(cond, 0)
//#define COMPILER_LIKELY(cond) cond
#define COMPILER_LIKELY(cond) __builtin_expect(cond, 1)


#ifndef NULL
#define NULL ((void*)0)
#endif

#ifndef count_of
#define count_of(h) (sizeof(h)/sizeof((h)[0]))
#endif


#if PPU_IS_MONOCHROME
typedef unsigned char pixel_t;
#elif PPU_MODE == 4 || PPU_MODE == 8
typedef unsigned char pixel_t;
#elif PPU_MODE == 6 || PPU_MODE == 7
typedef unsigned short pixel_t;
#else
typedef unsigned int pixel_t;
#endif

typedef unsigned int word;
typedef signed int sword;
#if !GBA
typedef unsigned int var;
typedef signed int svar;
#else
//typedef unsigned short var;
//typedef signed short svar;
typedef unsigned int var;
typedef signed int svar;
#endif

typedef word wbool;
typedef var vbool;

typedef unsigned char r8;
typedef unsigned short r16;
typedef unsigned int r32;

typedef signed short s16;
typedef signed int s32;

/*
 * PicoGB aliases byte and 16-bit views of a few registers.  Keep that fast
 * representation, but make its byte order explicit so the core also works on
 * big-endian CPUs such as the Saturn's SH-2.
 *
 * A port may define PICOGB_LITTLE_ENDIAN to 0 or 1.  GCC and Clang targets are
 * detected automatically; MSVC targets are little-endian.
 */
#ifndef PICOGB_LITTLE_ENDIAN
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && defined(__ORDER_BIG_ENDIAN__)
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define PICOGB_LITTLE_ENDIAN 1
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define PICOGB_LITTLE_ENDIAN 0
#else
#error "PicoGB: unsupported mixed-endian target"
#endif
#elif defined(_WIN32)
#define PICOGB_LITTLE_ENDIAN 1
#else
#error "PicoGB: cannot determine target byte order; define PICOGB_LITTLE_ENDIAN"
#endif
#endif

typedef char picogb_assert_r8_is_8_bits[(sizeof(r8) == 1) ? 1 : -1];
typedef char picogb_assert_r16_is_16_bits[(sizeof(r16) == 2) ? 1 : -1];
typedef char picogb_assert_r32_is_32_bits[(sizeof(r32) == 4) ? 1 : -1];

struct hilow16_t
{
    union
    {
        struct
        {
#if PICOGB_LITTLE_ENDIAN
        r8 low, high;
#else
        r8 high, low;
#endif
        };
        r16 raw;
    };
};
typedef struct hilow16_t hilow16_t;
