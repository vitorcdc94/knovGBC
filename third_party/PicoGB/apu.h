/* knovGBC modification notice updated 2026-10-10.
 * This file differs from upstream PicoGB commit
 * 325a12a25e2d1f4799424cc18c0e69a7810695b0.
 * The preserved local code changes predate the 2026-10-07 inventory.
 * Exact historical code-edit dates are not established by surviving records.
 * Only this source-handoff notice was changed on 2026-10-10.
 * APU evidence: tools/tests/picogb_056_validation.md and
 * tools/tests/picogb_057_validation.md, completed 2026-10-04.
 * See third_party/PicoGB/KNOVGBC_CHANGES.md for dated validation evidence;
 * report dates are not claims that every code change occurred on those dates.
 * PicoGB and this modified library remain under GNU LGPL v2.1.
 * This notice does not replace upstream license or copyright notices.
 */
#pragma once

#include "types.h"

#define APU_N_PER_TICK CONFIG_APU_N_PER_TICK
#if CONFIG_APU_MONO && defined(KNOVGBC_APU_MONO_FAST) && KNOVGBC_APU_MONO_FAST
#define APU_MONO_FAST 1
#else
#define APU_MONO_FAST 0
#endif
#if CONFIG_APU_MONO
#define APU_N_CHANNELS 1
#else
#define APU_N_CHANNELS 2
#endif

/* CPU timing is in machine cycles; channel timers retain 4 MHz dot precision. */
struct apu_ch_t
{
    r32 ctr;
    r16 raw;
    r16 length_ctr;
    r8 NR_RAW[5];
    r8 raw_out;
    r8 sample_no;
    r8 sweep_ctr;
    r8 env_ctr;
    r8 vol;
    r8 is_on;
    r8 started;
    r8 env_on;
    r8 sweep_on;
    r8 sweep_negated;
};

struct apu_t
{
    struct apu_ch_t ch[4];
    var MASTER_CFG; /* NR50, NR51, and NR52 power bit at bit 23. */
    var CTR_INT;
    var CTR_INT_FRAC; /* Pending machine cycles, flushed before sound I/O. */
    var CTR_DIV;
    var outbuf_pos; /* Monotonic producer cursor, in s16 entries, wraps at UINT_MAX. */
    r8 WVRAM[16];
    s16* outbuf;
    word outbuf_size; /* Ring capacity in s16 entries; stereo must be even. */
    word sample_cycles;
    s32 sample_sum_l;
    s32 sample_sum_r;
    s32 capacitor_l;
    s32 capacitor_r;
    r8 frame_step;
    r8 pending_render;
    r8 sample_dacs;
    r8 wave_access;
#if APU_MONO_FAST
    /* Routing, gain and DAC-derived coefficients; full words avoid byte-
     * extension work on SH-2. Oscillator state is still advanced normally. */
    word mono_weights[4];
    word mono_bias;
    word mono_dacs;
#endif
};
typedef struct apu_t apu_t;

void apu_reset(apu_t* __restrict pp);
void apu_initialize(apu_t* __restrict pp);
void apu_write(apu_t* __restrict pp, word addr, word data);
word apu_read(apu_t* __restrict pp, word addr);
void apu_write_wave(apu_t* __restrict pp, word addr, word data);
word apu_read_wave(apu_t* __restrict pp, word addr);
void apu_div_reset(apu_t* __restrict pp);
void apu_flush(apu_t* __restrict pp, wbool render);

/* Legacy rendering entry points now use the same countdown engine. */
void apu_render_faster(apu_t* __restrict pp, s16* outbuf, word ncounts);
void apu_render(apu_t* __restrict pp, s16* outbuf, word ncounts);
void apu_tick_internal_internals(apu_t* __restrict pp);
void apu_tick_internal(apu_t* __restrict pp);

PGB_FUNC static inline void apu_tick(apu_t* __restrict pp, word ncycles, wbool render)
{
    /* Most CPU instructions only add to this accumulator. Sound I/O flushes
     * sub-sample time, preserving the CPU core's instruction-boundary I/O
     * timing instead of delaying writes to the next PCM sample boundary. */
    if(pp->CTR_INT_FRAC && pp->pending_render != !!render)
        apu_flush(pp, pp->pending_render);
    pp->pending_render = !!render;
    pp->CTR_INT_FRAC += ncycles;
    if(pp->CTR_INT_FRAC >= APU_N_PER_TICK)
        apu_flush(pp, render);
}
