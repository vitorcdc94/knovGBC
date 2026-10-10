#pragma once

#include <stdbool.h>
#include <stdint.h>

/* APU output rate. The SCSP's nearest OCT/FNS setting is 32773.535 Hz. */
#define PICOGB_SATURN_AUDIO_RATE 32768U
#define PICOGB_SATURN_AUDIO_BUFFER_SAMPLES 8192U
#define PICOGB_SATURN_AUDIO_SEGMENT_SAMPLES 4096U

typedef struct picogb_saturn_audio_status {
    /* Upper bound: CA exposes positions only at 4096-sample boundaries. */
    uint32_t queued_samples;
    uint32_t accepted_samples;
    uint32_t underrun_count;
    uint32_t recovery_count;
    bool running;
} picogb_saturn_audio_status_t;

/* Call after mvp_profiler_init(), before starting asynchronous pad INTBACK.
 * Owns SCSP slot 0, 16 KiB of sound RAM, and installs an idle sound-CPU loop.
 * The sound CPU is restarted immediately after loading that 14-byte stub.
 */
bool picogb_saturn_audio_init(void);

/* Nonblocking PCM producer. Returns the number accepted; retain the rest.
 * Samples are signed 16-bit mono. Stereo panning is centered at the SCSP.
 * Call pump at least once per emulated frame (less than 100 ms apart).
 */
uint32_t picogb_saturn_audio_submit(const int16_t *samples, uint32_t count);
void picogb_saturn_audio_pump(void);
void picogb_saturn_audio_status(picogb_saturn_audio_status_t *out);
