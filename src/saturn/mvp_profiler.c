#include "mvp_profiler.h"

#include "platform.h"

#if defined(KNOVGBC_OPCODE_PROFILE)
#include "picogb_opcode_profile.h"
#endif

#include <yaul.h>

#include <stdint.h>

#if defined(KNOVGBC_OPCODE_PROFILE)
/* A longer window gives the 1-in-64 memory sample a useful distribution.
 * This is a diagnostic build only; its timing is never a performance result. */
#define PROFILE_WINDOW_FRAMES 120U
#else
#define PROFILE_WINDOW_FRAMES 60U
#endif
#define GB_FPS_NUMERATOR      5973U
#define GB_FPS_DENOMINATOR     100U

static volatile uint32_t frt_overflows;
static uint32_t ticks_per_ms;

static uint32_t pacer_deadline;
static uint32_t pacer_base_ticks;
static uint32_t pacer_remainder_ticks;
static uint32_t pacer_remainder;

static uint32_t sample_count;
static uint32_t presented_count;
static uint32_t window_start_ticks;
#if !defined(KNOVGBC_FPS_ONLY)
static uint32_t total_sum;
static uint32_t core_sum;
static uint32_t conversion_sum;
static uint32_t wait_sum;
static uint32_t upload_sum;
#endif

#if defined(KNOVGBC_OPCODE_PROFILE)
static uint32_t opcode_profile_page;
#endif

#if defined(KNOVGBC_SLAVE_PPU_SHADOW)
static uint32_t shadow_status;

void
mvp_profiler_shadow_status_set(uint32_t status)
{
    shadow_status = status;
}
#endif

#if defined(KNOVGBC_COMPONENT_PROFILE)
struct mvp_profiler_component_bucket
    mvp_profiler_component_buckets[MVP_PROFILER_COMPONENT_COUNT];

static uint32_t cpu_sum;
static uint32_t memory_sum;
static uint32_t ppu_sum;
static uint32_t timer_sum;

static uint32_t
component_estimate_ticks(enum mvp_profiler_component component)
{
    const struct mvp_profiler_component_bucket *const bucket =
        &mvp_profiler_component_buckets[component];

    if ((bucket->calls == 0U) || (bucket->samples == 0U)) {
        return 0U;
    }

    /* Both values are per logical frame. Their product fits in 32 bits for
     * this compact MVP and avoids pulling a 64-bit helper into the hot build. */
    return (bucket->sampled_ticks * bucket->calls) / bucket->samples;
}

void
mvp_profiler_component_frame_begin(void)
{
    for (uint32_t i = 0; i < MVP_PROFILER_COMPONENT_COUNT; i++) {
        mvp_profiler_component_buckets[i].calls = 0U;
        mvp_profiler_component_buckets[i].samples = 0U;
        mvp_profiler_component_buckets[i].sampled_ticks = 0U;
    }
}

void
mvp_profiler_component_frame_commit(void)
{
    const uint32_t cpu_inclusive =
        component_estimate_ticks(MVP_PROFILER_COMPONENT_CPU);
    const uint32_t memory =
        component_estimate_ticks(MVP_PROFILER_COMPONENT_MEMORY);

    /* The interpreter sample includes its nested fetch/read/write routines.
     * Display CPU as the remaining arithmetic/dispatch/flags work so the
     * memory column is not counted twice. Sampling makes this an estimate,
     * not an exact accounting identity. */
    cpu_sum += (cpu_inclusive > memory) ? cpu_inclusive - memory : 0U;
    memory_sum += memory;
    ppu_sum += component_estimate_ticks(MVP_PROFILER_COMPONENT_PPU);
    timer_sum += component_estimate_ticks(MVP_PROFILER_COMPONENT_TIMER);
}
#endif

static void
frt_overflow_handler(void)
{
    frt_overflows++;
}

uint32_t
mvp_profiler_ticks_get(void)
{
    uint32_t high_before;
    uint32_t high_after;
    uint16_t low;

    /* The overflow handler may run between any two reads. Retry until the
     * high word is stable so a wrap can never make a duration go backwards. */
    do {
        high_before = frt_overflows;
        low = cpu_frt_count_get();
        high_after = frt_overflows;
    } while (high_before != high_after);

    return (high_before << 16) | low;
}

void
mvp_profiler_init(void)
{
    const uint8_t old_mask = cpu_intc_mask_get();
    cpu_intc_mask_set(15);

    frt_overflows = 0;
    cpu_frt_init(CPU_FRT_CLOCK_DIV_128);
    cpu_frt_ovi_set(frt_overflow_handler);
    cpu_frt_count_set(0);

    cpu_intc_mask_set(old_mask);

    ticks_per_ms = (vdp2_tvmd_tv_standard_get() ==
        VDP2_TVMD_TV_STANDARD_PAL)
        ? CPU_FRT_PAL_320_128_COUNT_1MS
        : CPU_FRT_NTSC_320_128_COUNT_1MS;

    /* ticks/frame = ticks/ms * 1000 / 59.73. Keep the fractional remainder
     * so the long-term rate does not drift by rounding to a whole tick. */
    const uint32_t numerator =
        ticks_per_ms * 1000U * GB_FPS_DENOMINATOR;
    pacer_base_ticks = numerator / GB_FPS_NUMERATOR;
    pacer_remainder_ticks = numerator % GB_FPS_NUMERATOR;
    pacer_remainder = 0;
    pacer_deadline = mvp_profiler_ticks_get();

    sample_count = 0;
    presented_count = 0;
#if !defined(KNOVGBC_FPS_ONLY)
    total_sum = 0;
    core_sum = 0;
    conversion_sum = 0;
    wait_sum = 0;
    upload_sum = 0;
#endif

#if defined(KNOVGBC_SLAVE_PPU_SHADOW)
    shadow_status = 0U;
#endif

#if defined(KNOVGBC_COMPONENT_PROFILE)
    cpu_sum = 0;
    memory_sum = 0;
    ppu_sum = 0;
    timer_sum = 0;
#endif

#if defined(KNOVGBC_FPS_ONLY)
    saturn_platform_hud_draw("L00.0 V00.0", "", "");
#elif defined(KNOVGBC_OPCODE_PROFILE)
    opcode_profile_page = 0U;
    picogb_opcode_profile_reset();
    saturn_platform_hud_draw("OPCODE PROFILE", "WAIT 120 FRAMES",
        "PAGES O M T CYCLE");
#elif defined(KNOVGBC_COMPONENT_PROFILE)
    saturn_platform_hud_draw("L00.0 V00.0", "G0000 C0000 M0000",
        "P0000 R0000 U0000");
#else
#if defined(KNOVGBC_NO_FRAMESKIP)
    saturn_platform_hud_draw("L00.0", "T0000 G0000",
        "C0000 W0000 P0000");
#else
    saturn_platform_hud_draw("L00.0 V00.0", "T0000 G0000",
#if defined(KNOVGBC_SLAVE_PPU_SHADOW)
        "S0000 W0000 P0000");
#else
        "C0000 W0000 P0000");
#endif
#endif
#endif
    window_start_ticks = mvp_profiler_ticks_get();
}

uint32_t
mvp_profiler_frame_pace(void)
{
    const uint32_t wait_start = mvp_profiler_ticks_get();

    pacer_deadline += pacer_base_ticks;
    pacer_remainder += pacer_remainder_ticks;
    if (pacer_remainder >= GB_FPS_NUMERATOR) {
        pacer_remainder -= GB_FPS_NUMERATOR;
        pacer_deadline++;
    }

    uint32_t now = wait_start;
    const int32_t lateness = (int32_t)(now - pacer_deadline);

    /* Do not build an ever-growing backlog when the core is slower than real
     * time. A one-frame grace period still lets a short spike catch up. */
    if (lateness > (int32_t)pacer_base_ticks) {
        pacer_deadline = now;
        pacer_remainder = 0;
        return 0;
    }

    while ((int32_t)(now - pacer_deadline) < 0) {
        now = mvp_profiler_ticks_get();
    }

#if defined(KNOVGBC_FPS_ONLY)
    return 0U;
#else
    return now - wait_start;
#endif
}

#if !defined(KNOVGBC_OPCODE_PROFILE)
#if !defined(KNOVGBC_FPS_ONLY)
static uint32_t
average_ms(uint32_t tick_sum, uint32_t count)
{
    if ((count == 0U) || (ticks_per_ms == 0U)) {
        return 0;
    }

    const uint32_t divisor = count * ticks_per_ms;
    return (tick_sum + (divisor / 2U)) / divisor;
}
#endif

static uint32_t
fps_tenths(uint32_t frames, uint32_t elapsed_ticks)
{
    if ((frames == 0U) || (elapsed_ticks == 0U)) {
        return 0;
    }

    /* ticks_per_ms * 1000 gives ticks/s; multiply by ten for one decimal.
     * With the 60-frame window the numerator remains safely within 32 bits. */
    uint32_t result =
        ((ticks_per_ms * 10000U * frames) + (elapsed_ticks / 2U)) /
        elapsed_ticks;
    return (result > 999U) ? 999U : result;
}

#if !defined(KNOVGBC_FPS_ONLY)
static uint32_t
clamp_four_digits(uint32_t value)
{
    return (value > 9999U) ? 9999U : value;
}
#endif

static void
format_fps(char *destination, char label, uint32_t value)
{
    destination[0] = label;
    destination[1] = (char)('0' + ((value / 100U) % 10U));
    destination[2] = (char)('0' + ((value / 10U) % 10U));
    destination[3] = '.';
    destination[4] = (char)('0' + (value % 10U));
}

#if !defined(KNOVGBC_FPS_ONLY)
static void
format_ms(char *destination, char label, uint32_t value)
{
    value = clamp_four_digits(value);
    destination[0] = label;
    destination[1] = (char)('0' + ((value / 1000U) % 10U));
    destination[2] = (char)('0' + ((value / 100U) % 10U));
    destination[3] = (char)('0' + ((value / 10U) % 10U));
    destination[4] = (char)('0' + (value % 10U));
}
#endif
#endif

#if defined(KNOVGBC_OPCODE_PROFILE)
static void
format_count_k3(char *destination, uint32_t value)
{
    uint32_t thousands = value / 1000U;

    if ((value % 1000U) >= 500U) {
        thousands++;
    }

    if (thousands > 999U) {
        thousands = 999U;
    }

    destination[0] = (char)('0' + ((thousands / 100U) % 10U));
    destination[1] = (char)('0' + ((thousands / 10U) % 10U));
    destination[2] = (char)('0' + (thousands % 10U));
    destination[3] = 'K';
}

static void
format_count_k4(char *destination, uint32_t value)
{
    uint32_t thousands = value / 1000U;

    if ((value % 1000U) >= 500U) {
        thousands++;
    }

    if (thousands > 9999U) {
        thousands = 9999U;
    }

    destination[0] = (char)('0' + ((thousands / 1000U) % 10U));
    destination[1] = (char)('0' + ((thousands / 100U) % 10U));
    destination[2] = (char)('0' + ((thousands / 10U) % 10U));
    destination[3] = (char)('0' + (thousands % 10U));
    destination[4] = 'K';
}

static uint32_t
profile_estimated_count(uint32_t sampled_transactions)
{
    if (sampled_transactions >
        (UINT32_MAX / PICOGB_OPCODE_PROFILE_MEMORY_SAMPLE_RATE)) {
        return UINT32_MAX;
    }

    return sampled_transactions * PICOGB_OPCODE_PROFILE_MEMORY_SAMPLE_RATE;
}

static char
profile_memory_access_tag(uint8_t access)
{
    return (access == PICOGB_OPCODE_PROFILE_ACCESS_WRITE) ? 'W' : 'R';
}

static char
profile_memory_class_tag(uint8_t memory_class)
{
    static const char tags[PICOGB_OPCODE_PROFILE_MEMORY_CLASS_COUNT] = {
        '0', 'X', 'V', 'S', 'W', 'E', 'O', 'I', 'H', 'J'
    };

    if (memory_class >= PICOGB_OPCODE_PROFILE_MEMORY_CLASS_COUNT) {
        return '?';
    }

    return tags[memory_class];
}

static void
format_profile_opcode_pair(char *destination,
    const struct picogb_opcode_profile_opcode_entry *first,
    const struct picogb_opcode_profile_opcode_entry *second)
{
    destination[0] = 'O';
    destination[1] = "0123456789ABCDEF"[first->opcode >> 4U];
    destination[2] = "0123456789ABCDEF"[first->opcode & 0x0FU];
    destination[3] = ' ';
    format_count_k3(&destination[4], first->count);
    destination[8] = ' ';
    destination[9] = 'O';
    destination[10] = "0123456789ABCDEF"[second->opcode >> 4U];
    destination[11] = "0123456789ABCDEF"[second->opcode & 0x0FU];
    destination[12] = ' ';
    format_count_k3(&destination[13], second->count);
    destination[17] = '\0';
}

static void
format_profile_memory_pair(char *destination,
    const struct picogb_opcode_profile_memory_entry *first,
    const struct picogb_opcode_profile_memory_entry *second)
{
    destination[0] = profile_memory_access_tag(first->access);
    destination[1] = profile_memory_class_tag(first->memory_class);
    destination[2] = ' ';
    format_count_k4(&destination[3], profile_estimated_count(first->count));
    destination[8] = ' ';
    destination[9] = profile_memory_access_tag(second->access);
    destination[10] = profile_memory_class_tag(second->memory_class);
    destination[11] = ' ';
    format_count_k4(&destination[12], profile_estimated_count(second->count));
    destination[17] = '\0';
}

static void
format_profile_total_pair(char *destination, const char *first_label,
    uint32_t first_count, const char *second_label, uint32_t second_count)
{
    destination[0] = first_label[0];
    destination[1] = first_label[1];
    destination[2] = first_label[2];
    destination[3] = ' ';
    format_count_k3(&destination[4], first_count);
    destination[8] = ' ';
    destination[9] = second_label[0];
    destination[10] = second_label[1];
    destination[11] = second_label[2];
    destination[12] = ' ';
    format_count_k3(&destination[13], second_count);
    destination[17] = '\0';
}

static void
format_profile_access_totals(char *destination,
    const struct picogb_opcode_profile_snapshot *snapshot)
{
    destination[0] = 'R';
    format_count_k3(&destination[1], profile_estimated_count(
        picogb_opcode_profile_memory_access_total(snapshot,
            PICOGB_OPCODE_PROFILE_ACCESS_READ)));
    destination[5] = ' ';
    destination[6] = 'W';
    format_count_k3(&destination[7], profile_estimated_count(
        picogb_opcode_profile_memory_access_total(snapshot,
            PICOGB_OPCODE_PROFILE_ACCESS_WRITE)));
    destination[11] = ' ';
    destination[12] = 'H';
    format_count_k3(&destination[13], profile_estimated_count(
        picogb_opcode_profile_memory_class_total(snapshot,
            PICOGB_OPCODE_PROFILE_MEMORY_HRAM)));
    destination[17] = '\0';
}

static void
publish_opcode_profile(void)
{
    struct picogb_opcode_profile_snapshot snapshot;
    char line_1[18];
    char line_2[18];
    char line_3[18];

    picogb_opcode_profile_snapshot_take(&snapshot);

    switch (opcode_profile_page) {
        case 0U:
        {
            struct picogb_opcode_profile_opcode_entry
                entries[PICOGB_OPCODE_PROFILE_TOP_COUNT];

            picogb_opcode_profile_top_opcodes(&snapshot, entries);
            format_profile_opcode_pair(line_1, &entries[0], &entries[1]);
            format_profile_opcode_pair(line_2, &entries[2], &entries[3]);
            format_profile_opcode_pair(line_3, &entries[4], &entries[5]);
            break;
        }
        case 1U:
        {
            struct picogb_opcode_profile_memory_entry
                entries[PICOGB_OPCODE_PROFILE_TOP_COUNT];

            picogb_opcode_profile_top_memory(&snapshot, entries);
            format_profile_memory_pair(line_1, &entries[0], &entries[1]);
            format_profile_memory_pair(line_2, &entries[2], &entries[3]);
            format_profile_memory_pair(line_3, &entries[4], &entries[5]);
            break;
        }
        default:
            format_profile_total_pair(line_1, "OPS", snapshot.instructions,
                "INT", snapshot.interrupts);
            format_profile_total_pair(line_2, "DAT", snapshot.memory_accesses,
                "SMP", snapshot.memory_samples);
            format_profile_access_totals(line_3, &snapshot);
            break;
    }

    opcode_profile_page = (opcode_profile_page + 1U) % 3U;
    saturn_platform_hud_draw(line_1, line_2, line_3);
}
#endif

static void
publish_hud(uint32_t elapsed_ticks)
{
#if defined(KNOVGBC_OPCODE_PROFILE)
    /* The profile image intentionally replaces frame-time reporting: its
     * counters add work, so reporting its FPS beside the normal baseline
     * would be misleading. */
    (void)elapsed_ticks;
    publish_opcode_profile();
#else
    char line_1[12];
#if !defined(KNOVGBC_FPS_ONLY) && defined(KNOVGBC_COMPONENT_PROFILE)
    char line_2[18];
    char line_3[18];
#elif !defined(KNOVGBC_FPS_ONLY)
    char line_2[12];
    char line_3[18];
#endif

    /* Use actual wall time rather than the sum of instrumented regions. This
     * includes per-frame bookkeeping and the previous HUD update, preventing
     * the diagnostic itself from making the reported FPS look better. */
    format_fps(&line_1[0], 'L', fps_tenths(sample_count, elapsed_ticks));
#if defined(KNOVGBC_NO_FRAMESKIP)
    line_1[5] = '\0';
#else
    line_1[5] = ' ';
    format_fps(&line_1[6], 'V', fps_tenths(presented_count, elapsed_ticks));
    line_1[11] = '\0';
#endif

#if defined(KNOVGBC_FPS_ONLY)
    saturn_platform_hud_draw(line_1, "", "");
#elif defined(KNOVGBC_COMPONENT_PROFILE)
    format_ms(&line_2[0], 'G', average_ms(core_sum, sample_count));
    line_2[5] = ' ';
    format_ms(&line_2[6], 'C', average_ms(cpu_sum, sample_count));
    line_2[11] = ' ';
    format_ms(&line_2[12], 'M', average_ms(memory_sum, sample_count));
    line_2[17] = '\0';

    format_ms(&line_3[0], 'P', average_ms(ppu_sum, sample_count));
    line_3[5] = ' ';
    format_ms(&line_3[6], 'R', average_ms(timer_sum, sample_count));
    line_3[11] = ' ';
    format_ms(&line_3[12], 'U', average_ms(upload_sum, presented_count));
    line_3[17] = '\0';
#else
    format_ms(&line_2[0], 'T', average_ms(total_sum, sample_count));
    line_2[5] = ' ';
    format_ms(&line_2[6], 'G', average_ms(core_sum, sample_count));
    line_2[11] = '\0';

#if defined(KNOVGBC_SLAVE_PPU_SHADOW)
    format_ms(&line_3[0], 'S', shadow_status);
#else
    format_ms(&line_3[0], 'C', average_ms(conversion_sum, presented_count));
#endif
    line_3[5] = ' ';
    format_ms(&line_3[6], 'W', average_ms(wait_sum, sample_count));
    line_3[11] = ' ';
    format_ms(&line_3[12], 'P', average_ms(upload_sum, presented_count));
    line_3[17] = '\0';
#endif

#if !defined(KNOVGBC_FPS_ONLY)
    saturn_platform_hud_draw(line_1, line_2, line_3);
#endif
#endif
}

void
mvp_profiler_frame_record(uint32_t total_ticks, uint32_t core_ticks,
    uint32_t conversion_ticks, uint32_t wait_ticks, uint32_t upload_ticks,
    bool presented)
{
#if defined(KNOVGBC_FPS_ONLY)
    (void)total_ticks;
    (void)core_ticks;
    (void)conversion_ticks;
    (void)wait_ticks;
    (void)upload_ticks;
#endif
    sample_count++;
#if !defined(KNOVGBC_FPS_ONLY)
    total_sum += total_ticks;
    core_sum += core_ticks;
    wait_sum += wait_ticks;
#endif

    if (presented) {
        presented_count++;
#if !defined(KNOVGBC_FPS_ONLY)
        conversion_sum += conversion_ticks;
        upload_sum += upload_ticks;
#endif
    }

    if (sample_count < PROFILE_WINDOW_FRAMES) {
        return;
    }

    const uint32_t window_end_ticks = mvp_profiler_ticks_get();
    const uint32_t elapsed_ticks = window_end_ticks - window_start_ticks;

    /* Start the next wall-time window before drawing. Its elapsed time will
     * therefore include this update of the on-screen diagnostic. */
    window_start_ticks = window_end_ticks;
    publish_hud(elapsed_ticks);

    sample_count = 0;
    presented_count = 0;
#if !defined(KNOVGBC_FPS_ONLY)
    total_sum = 0;
    core_sum = 0;
    conversion_sum = 0;
    wait_sum = 0;
    upload_sum = 0;
#endif

#if defined(KNOVGBC_COMPONENT_PROFILE)
    cpu_sum = 0;
    memory_sum = 0;
    ppu_sum = 0;
    timer_sum = 0;
#endif
}
