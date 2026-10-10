#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(KNOVGBC_COMPONENT_PROFILE)
#include <cpu/frt.h>

/* The diagnostic build samples hot subcomponents instead of timestamping
 * every access. Sampling keeps the profiler from becoming the dominant cost
 * on a 28 MHz SH-2 while still producing thousands of observations per HUD
 * window. */
enum mvp_profiler_component {
    MVP_PROFILER_COMPONENT_CPU = 0,
    MVP_PROFILER_COMPONENT_MEMORY,
    MVP_PROFILER_COMPONENT_PPU,
    MVP_PROFILER_COMPONENT_TIMER,
    MVP_PROFILER_COMPONENT_COUNT
};

struct mvp_profiler_component_bucket {
    uint32_t calls;
    uint32_t samples;
    uint32_t sampled_ticks;
};

extern struct mvp_profiler_component_bucket
    mvp_profiler_component_buckets[MVP_PROFILER_COMPONENT_COUNT];

/* Return a nonzero timestamp token only for a sampled call. FRT samples are
 * deliberately 16-bit: every individual component call is far shorter than
 * the 65,536-tick wrap interval, while the normal profiler still provides the
 * 32-bit wall-clock time used for frame totals. */
static inline __attribute__((always_inline)) uint32_t
mvp_profiler_component_begin(enum mvp_profiler_component component)
{
    struct mvp_profiler_component_bucket *const bucket =
        &mvp_profiler_component_buckets[component];
    const uint32_t calls = ++bucket->calls;
    uint32_t mask;

    switch (component) {
        case MVP_PROFILER_COMPONENT_CPU:
        case MVP_PROFILER_COMPONENT_PPU:
            mask = 0x1FU; /* one in 32 */
            break;
        case MVP_PROFILER_COMPONENT_MEMORY:
            mask = 0x7FU; /* one in 128 */
            break;
        default:
            mask = 0x3FU; /* timer: one in 64 */
            break;
    }

    if ((calls & mask) != 0U) {
        return 0U;
    }

    /* Add one so a valid FRT count of zero remains distinguishable from an
     * unsampled call. */
    return (uint32_t)cpu_frt_count_get() + 1U;
}

static inline __attribute__((always_inline)) void
mvp_profiler_component_end(enum mvp_profiler_component component,
    uint32_t token)
{
    if (token == 0U) {
        return;
    }

    struct mvp_profiler_component_bucket *const bucket =
        &mvp_profiler_component_buckets[component];
    const uint16_t start = (uint16_t)(token - 1U);

    bucket->sampled_ticks += (uint16_t)(cpu_frt_count_get() - start);
    bucket->samples++;
}

/* Reset sampled-call counters at the beginning of one logical GB frame and
 * fold their estimates into the next HUD window after that frame finishes. */
void mvp_profiler_component_frame_begin(void);
void mvp_profiler_component_frame_commit(void);
#endif

/* Start the Master SH-2 free-running timer and the 59.73 Hz frame pacer. */
void mvp_profiler_init(void);

/* Monotonic FRT tick counter, extended from 16 to 32 bits. */
uint32_t mvp_profiler_ticks_get(void);

/* Wait only while the emulator is ahead of the next logical-frame deadline.
 * If it is late, return immediately instead of blindly waiting another
 * Saturn VBlank. Normal builds return the FRT ticks spent waiting; the
 * FPS-only build omits that measurement and returns zero. */
uint32_t mvp_profiler_frame_pace(void);

/* Accumulate one logical frame. Conversion and upload are averaged only over
 * the frames actually presented, so frameskip does not halve those costs.
 * KNOVGBC_FPS_ONLY ignores all timing arguments and counts only logical and
 * presented frames against elapsed wall time. */
void mvp_profiler_frame_record(uint32_t total_ticks, uint32_t core_ticks,
    uint32_t conversion_ticks, uint32_t wait_ticks, uint32_t upload_ticks,
    bool presented);

#if defined(KNOVGBC_SLAVE_PPU_SHADOW)
/* Shadow target HUD: S0000 waiting, S0001 match, S0002 mismatch, S0003 drop,
 * S0004 timeout, S0005 worker error. This is diagnostic only. */
void mvp_profiler_shadow_status_set(uint32_t status);
#endif
