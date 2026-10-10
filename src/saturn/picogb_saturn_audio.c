/*
 * Single-slot PCM output for the DMG APU. No SH-2 slave, ADX, prerecorded
 * music, sound-RAM DMA, resampling, or time stretching is used here.
 *
 * Register layout: Saturn SCSP User's Manual ST-077-R2-052594, pp. 25-29,
 * 35-38, 41-43, 73-75, 80-89. Initialization: Technical Bulletin #51.
 * Slot key/pitch/envelope setup was cross-checked against the project-local
 * poneSound driver (MIT, third_party/SCSP_poneSound/PROJ/main.c).
 */
#include "picogb_saturn_audio.h"
#include "mvp_profiler.h"

#include <stddef.h>
#include <yaul.h>

#ifndef PICOGB_AUDIO_SOUND_RAM_BASE
#define PICOGB_AUDIO_SOUND_RAM_BASE 0x25A00000UL
#endif
#ifndef PICOGB_AUDIO_SCSP_REG_BASE
#define PICOGB_AUDIO_SCSP_REG_BASE 0x25B00000UL
#endif

enum {
    /* Keep the reset vectors/idle program away from the sample ring. */
    AUDIO_PCM_RAM_OFFSET = 0x10000U,
    AUDIO_SLOT_WORDS = 16U,
    AUDIO_KYONB = 0x0800U,
    AUDIO_KYONEX = 0x1000U,
    AUDIO_FORWARD_LOOP = 0x0020U,
    AUDIO_STWINH = 0x0200U,
    AUDIO_RATE_PITCH = 0x79F2U, /* OCT=-1, FNS=498 */
    AUDIO_MONITOR_SHIFT = 7U,
    AUDIO_MASTER_VOLUME = 0x020FU, /* 4-Mbit RAM, 16-bit DAC, MVOL=15 */
    AUDIO_MONITOR_TIMEOUT_MS = 100U
};

_Static_assert(PICOGB_SATURN_AUDIO_BUFFER_SAMPLES ==
    2U * PICOGB_SATURN_AUDIO_SEGMENT_SAMPLES,
    "SCSP coarse-monitor ring must contain two segments");
_Static_assert((AUDIO_PCM_RAM_OFFSET +
    PICOGB_SATURN_AUDIO_BUFFER_SAMPLES * 2U) <= 0x80000U,
    "PCM ring must fit in the SCSP's 512-KiB sound RAM");

static struct {
    uint32_t write_cursor;
    uint32_t read_floor;
    uint32_t accepted_samples;
    uint32_t underrun_count;
    uint32_t recovery_count;
    uint32_t last_pump_ticks;
    uint32_t timeout_ticks;
    uint16_t last_page;
    bool initialized;
    bool running;
} audio;

static volatile uint16_t *
scsp_word(uint32_t offset)
{
    return (volatile uint16_t *)(PICOGB_AUDIO_SCSP_REG_BASE + offset);
}

static volatile uint16_t *
sound_word(uint32_t offset)
{
    return (volatile uint16_t *)(PICOGB_AUDIO_SOUND_RAM_BASE + offset);
}

static void
clear_samples(uint32_t first, uint32_t count)
{
    volatile uint16_t *const destination =
        sound_word(AUDIO_PCM_RAM_OFFSET + first * 2U);
    for (uint32_t i = 0; i < count; i++) {
        destination[i] = 0U;
    }
}

static void
stream_stop(void)
{
    *scsp_word(0U) = AUDIO_FORWARD_LOOP |
        (AUDIO_PCM_RAM_OFFSET >> 16) | AUDIO_KYONEX;
    audio.running = false;
}

static void
stream_start(void)
{
    /* Execute KEY_ON last, after all sample writes and slot parameters. */
    *scsp_word(0U) = AUDIO_KYONB | AUDIO_FORWARD_LOOP |
        (AUDIO_PCM_RAM_OFFSET >> 16) | AUDIO_KYONEX;
    audio.last_page = 0U;
    audio.last_pump_ticks = mvp_profiler_ticks_get();
    audio.running = true;
}

bool
picogb_saturn_audio_init(void)
{
    audio.initialized = false;
    audio.running = false;
    audio.write_cursor = 0U;
    audio.read_floor = 0U;
    audio.accepted_samples = 0U;
    audio.underrun_count = 0U;
    audio.recovery_count = 0U;
    audio.last_page = 0U;
    audio.timeout_ticks = AUDIO_MONITOR_TIMEOUT_MS *
        ((vdp2_tvmd_tv_standard_get() == VDP2_TVMD_TV_STANDARD_PAL)
            ? CPU_FRT_PAL_320_128_COUNT_1MS
            : CPU_FRT_NTSC_320_128_COUNT_1MS);

    /* MC68EC000 reset vector + MOVE.W #$2700,SR; BRA.S .
     * No STOP, TAS or RESET instruction. Bulletin #51 requires an endless
     * running loop when the sound CPU has no work, not leaving it stopped.
     */
    static const uint16_t idle_program[] = {
        0x0007U, 0xFF00U, 0x0000U, 0x0008U,
        0x46FCU, 0x2700U, 0x60FEU
    };
    const uint8_t old_mask = cpu_intc_mask_get();
    cpu_intc_mask_set(15);
    (void)smpc_smc_sndoff_call();
    *scsp_word(0x400U) = 0x0200U; /* MEM4MB before downloading the program. */
    for (uint32_t i = 0; i < sizeof(idle_program) / sizeof(idle_program[0]);
         i++) {
        sound_word(0U)[i] = idle_program[i];
    }
    (void)smpc_smc_sndon_call();
    cpu_intc_mask_set(old_mask);

    /* The manual forbids SCSP accesses for 30 us after reset is released.
     * Even 1024 single-cycle NOPs exceed that at the Saturn's maximum clock.
     * Volatile also prevents the compiler from removing the settling loop.
     */
    for (volatile uint32_t settle = 0U; settle < 1024U; settle++) {
        __asm__ volatile("nop");
    }

    *scsp_word(0x400U) = 0x0200U; /* Mute while removing loader sound state. */
    *scsp_word(0x41EU) = 0U;      /* SCIEB: no sound-CPU interrupts. */
    *scsp_word(0x42AU) = 0U;      /* MCIEB: no main-CPU sound interrupts. */
    *scsp_word(0x422U) = 0x07FFU;
    *scsp_word(0x42EU) = 0x07FFU;
    for (uint32_t slot = 0U; slot < 32U; slot++) {
        for (uint32_t word = 0U; word < AUDIO_SLOT_WORDS; word++) {
            *scsp_word(slot * 0x20U + word * 2U) = 0U;
        }
    }
    *scsp_word(0U) = AUDIO_KYONEX; /* Commit KEY_OFF for all 32 slots. */
    clear_samples(0U, PICOGB_SATURN_AUDIO_BUFFER_SAMPLES);

    *scsp_word(0x02U) = AUDIO_PCM_RAM_OFFSET & 0xFFFFU;
    *scsp_word(0x04U) = 0U;
    /* poneSound uses count-1 for the SCSP's last-sample pipeline. */
    *scsp_word(0x06U) = PICOGB_SATURN_AUDIO_BUFFER_SAMPLES - 1U;
    *scsp_word(0x08U) = 0x001FU; /* Instant attack, no decay. */
    *scsp_word(0x0AU) = 0x3C1FU; /* KRS disabled, fastest KEY_OFF release. */
    *scsp_word(0x0CU) = AUDIO_STWINH; /* No FM stack writes; TL=0. */
    *scsp_word(0x10U) = AUDIO_RATE_PITCH;
    *scsp_word(0x16U) = 0xE000U; /* DISDL=7, center pan, no DSP effects. */
    *scsp_word(0x408U) = 0U; /* MSLC=0 selects our PCM slot for CA reads. */
    *scsp_word(0x400U) = AUDIO_MASTER_VOLUME;
    audio.last_pump_ticks = mvp_profiler_ticks_get();
    audio.initialized = true;
    return true;
}

void
picogb_saturn_audio_pump(void)
{
    if (!audio.initialized || !audio.running) {
        return;
    }
    const uint32_t now = mvp_profiler_ticks_get();
    if ((now - audio.last_pump_ticks) >= audio.timeout_ticks) {
        /* CA cannot distinguish a whole missed ring rotation. Do not reuse
         * stale samples after a long stall: silence and prime the ring again.
         * This resets only the PCM slot, never stops the sound CPU/SCSP.
         */
        stream_stop();
        clear_samples(0U, PICOGB_SATURN_AUDIO_BUFFER_SAMPLES);
        audio.write_cursor = 0U;
        audio.read_floor = 0U;
        audio.underrun_count++;
        audio.recovery_count++;
        return;
    }
    audio.last_pump_ticks = now;

    const uint16_t page = (*scsp_word(0x408U) >> AUDIO_MONITOR_SHIFT) & 1U;
    if (page == audio.last_page) {
        return;
    }

    /* CA's low bit advances every 4096 waveform samples (not bytes). Only
     * the segment just left is safe to clear or overwrite. Never write the
     * currently playing segment, since CA cannot reveal its exact cursor.
     */
    clear_samples(audio.last_page * PICOGB_SATURN_AUDIO_SEGMENT_SAMPLES,
        PICOGB_SATURN_AUDIO_SEGMENT_SAMPLES);
    audio.last_page = page;
    audio.read_floor += PICOGB_SATURN_AUDIO_SEGMENT_SAMPLES;
    const uint32_t next_segment = audio.read_floor +
        PICOGB_SATURN_AUDIO_SEGMENT_SAMPLES;
    if ((int32_t)(audio.write_cursor - next_segment) < 0) {
        /* Unwritten samples were already zero. Skip that missing tail and
         * accept new samples only into the following safe segment. This is
         * silence on starvation, not replay or stretching of old samples.
         */
        audio.write_cursor = next_segment;
        audio.underrun_count++;
    }
}

uint32_t
picogb_saturn_audio_submit(const int16_t *samples, uint32_t count)
{
    if (!audio.initialized || samples == NULL || count == 0U) {
        return 0U;
    }
    picogb_saturn_audio_pump();

    uint32_t available;
    if (audio.running) {
        available = audio.read_floor + PICOGB_SATURN_AUDIO_BUFFER_SAMPLES -
            audio.write_cursor;
    } else {
        available = PICOGB_SATURN_AUDIO_BUFFER_SAMPLES - audio.write_cursor;
    }
    if (count > available) {
        count = available;
    }
    const uint32_t position = audio.write_cursor &
        (PICOGB_SATURN_AUDIO_BUFFER_SAMPLES - 1U);
    volatile uint16_t *const destination =
        sound_word(AUDIO_PCM_RAM_OFFSET + position * 2U);
    /* One safe segment never crosses the physical end of this power-of-two
     * ring. The pre-start path can copy the whole contiguous ring as well.
     */
    for (uint32_t i = 0U; i < count; i++) {
        destination[i] = (uint16_t)samples[i];
    }
    audio.write_cursor += count;
    audio.accepted_samples += count;
    if (!audio.running &&
        audio.write_cursor >= PICOGB_SATURN_AUDIO_SEGMENT_SAMPLES) {
        stream_start();
    }
    return count;
}

void
picogb_saturn_audio_status(picogb_saturn_audio_status_t *out)
{
    if (out == NULL) {
        return;
    }
    out->queued_samples = audio.write_cursor - audio.read_floor;
    out->accepted_samples = audio.accepted_samples;
    out->underrun_count = audio.underrun_count;
    out->recovery_count = audio.recovery_count;
    out->running = audio.running;
}
