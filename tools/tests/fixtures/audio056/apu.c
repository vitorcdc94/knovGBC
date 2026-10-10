/* knovGBC PicoGB library extension/snapshot; notice updated 2026-10-10.
 * This file preserves an earlier modified APU snapshot based on commit
 * 325a12a25e2d1f4799424cc18c0e69a7810695b0.
 * The preserved local implementation predates the 2026-10-07 inventory.
 * Exact historical code-edit dates are not established by surviving records.
 * Only this source-handoff notice was changed on 2026-10-10.
 * Pre-mono-mixer snapshot evidence: tools/tests/picogb_057_validation.md,
 * completed 2026-10-04, identifies this fixture as the earlier APU code.
 * See third_party/PicoGB/KNOVGBC_CHANGES.md for dated validation evidence;
 * report dates are not claims that every code change occurred on those dates.
 * Distributed with the modified library under GNU LGPL v2.1.
 * See third_party/PicoGB/LICENSE.txt for the library license.
 * Original upstream license and copyright notices remain applicable.
 */
#include "apu.h"
#include <string.h>

/* Register masks and digital waveform timing follow Pan Docs:
 * https://gbdev.io/pandocs/Audio_Registers.html
 * https://gbdev.io/pandocs/Audio_details.html
 * Mixing uses box integration followed by a fixed-point DMG high-pass filter.
 * It does not model analog DAC discharge or model-specific envelope glitches. */
#define APU_POWER (1u << 23)
#define APU_SAMPLE_DOTS (APU_N_PER_TICK * 4u)

static const r8 duty_patterns[4] PGB_DATA = { 0x80, 0x81, 0xE1, 0x7E };
static const r8 duty_high_steps[4] PGB_DATA = { 1, 2, 4, 6 };
static const r8 read_masks[23] PGB_DATA =
{
    0x80, 0x3F, 0x00, 0xFF, 0xBF,
    0xFF, 0x3F, 0x00, 0xFF, 0xBF,
    0x7F, 0xFF, 0x9F, 0xFF, 0xBF,
    0xFF, 0xFF, 0x00, 0x00, 0xBF,
    0x00, 0x00, 0x70
};
static const r8 noise_divisors[8] PGB_DATA = { 8, 16, 32, 48, 64, 80, 96, 112 };

static word channel_frequency(const struct apu_ch_t* ch)
{
    return ch->NR_RAW[3] | ((ch->NR_RAW[4] & 7u) << 8);
}

static word channel_period(const struct apu_ch_t* ch, word index)
{
    if(index == 3)
        return (word)noise_divisors[ch->NR_RAW[3] & 7u] << (ch->NR_RAW[3] >> 4);
    return (2048u - channel_frequency(ch)) << (index == 2 ? 1 : 2);
}

static wbool channel_dac(const struct apu_ch_t* ch, word index)
{
    return index == 2 ? (ch->NR_RAW[0] & 0x80u) : (ch->NR_RAW[2] & 0xF8u);
}

static void clock_length(struct apu_ch_t* ch)
{
    if((ch->NR_RAW[4] & 0x40u) && ch->length_ctr && !--ch->length_ctr)
        ch->is_on = 0;
}

static void clock_envelope(struct apu_ch_t* ch)
{
    word pace = ch->NR_RAW[2] & 7u;
    if(!ch->env_on || !ch->env_ctr || --ch->env_ctr)
        return;
    ch->env_ctr = pace ? pace : 8;
    if(!pace)
        return;
    if(ch->NR_RAW[2] & 8u)
    {
        if(ch->vol < 15)
            ++ch->vol;
        else
            ch->env_on = 0;
    }
    else
    {
        if(ch->vol)
            --ch->vol;
        else
            ch->env_on = 0;
    }
}

static word sweep_calculate(struct apu_ch_t* ch)
{
    word delta = ch->raw >> (ch->NR_RAW[0] & 7u);
    if(ch->NR_RAW[0] & 8u)
    {
        ch->sweep_negated = 1;
        return ch->raw - delta;
    }
    return ch->raw + delta;
}

static void clock_sweep(struct apu_ch_t* ch)
{
    word pace = (ch->NR_RAW[0] >> 4) & 7u;
    word frequency;
    if(!ch->sweep_ctr || --ch->sweep_ctr)
        return;
    ch->sweep_ctr = pace ? pace : 8;
    if(!ch->sweep_on || !pace)
        return;
    frequency = sweep_calculate(ch);
    if(frequency > 2047)
    {
        ch->is_on = 0;
        return;
    }
    if(ch->NR_RAW[0] & 7u)
    {
        ch->raw = frequency;
        ch->NR_RAW[3] = frequency & 0xFFu;
        ch->NR_RAW[4] = (ch->NR_RAW[4] & 0xF8u) | (frequency >> 8);
        if(sweep_calculate(ch) > 2047)
            ch->is_on = 0;
    }
}

static void clock_frame(apu_t* pp)
{
    word i;
    if(!(pp->MASTER_CFG & APU_POWER))
        return;
    if(!(pp->frame_step & 1u))
        for(i = 0; i < 4; ++i)
            clock_length(&pp->ch[i]);
    if(pp->frame_step == 2 || pp->frame_step == 6)
        clock_sweep(&pp->ch[0]);
    if(pp->frame_step == 7)
    {
        clock_envelope(&pp->ch[0]);
        clock_envelope(&pp->ch[1]);
        clock_envelope(&pp->ch[3]);
    }
    pp->frame_step = (pp->frame_step + 1u) & 7u;
}

static sword advance_pulse(struct apu_ch_t* ch, word dots)
{
    word period, count, cycles, duty = ch->NR_RAW[1] >> 6;
    sword sum = 0;
    if(!ch->started)
        return 0;
    if(dots < ch->ctr)
    {
        ch->ctr -= dots;
        return ch->is_on ? (sword)(dots * ch->raw_out * ch->vol) : 0;
    }
    sum = ch->ctr * ch->raw_out;
    dots -= ch->ctr;
    ch->sample_no = (ch->sample_no + 1u) & 7u;
    ch->raw_out = (duty_patterns[duty] >> ch->sample_no) & 1u;
    period = channel_period(ch, 0);
    ch->ctr = period;
    /* Complete duty periods return to the same step. This also bounds work
     * for inaudible high pulse frequencies without ticking every CPU cycle. */
    if(dots >= period * 8u)
    {
        cycles = dots / (period * 8u);
        sum += cycles * period * duty_high_steps[duty];
        dots -= cycles * period * 8u;
    }
    while(dots)
    {
        count = dots < ch->ctr ? dots : ch->ctr;
        sum += count * ch->raw_out;
        dots -= count;
        ch->ctr -= count;
        if(!ch->ctr)
        {
            ch->sample_no = (ch->sample_no + 1u) & 7u;
            ch->raw_out = (duty_patterns[duty] >> ch->sample_no) & 1u;
            ch->ctr = period;
        }
    }
    return ch->is_on ? sum * ch->vol : 0;
}

static sword advance_wave(apu_t* pp, word dots)
{
    struct apu_ch_t* ch = &pp->ch[2];
    word count, volume = (ch->NR_RAW[2] >> 5) & 3u;
    sword sum = 0;
    if(!ch->is_on)
    {
        pp->wave_access = 0;
        return 0;
    }
    while(dots)
    {
        count = dots < ch->ctr ? dots : ch->ctr;
        if(volume)
            sum += count * (ch->raw_out >> (volume - 1u));
        dots -= count;
        ch->ctr -= count;
        pp->wave_access = count >= pp->wave_access ? 0 : pp->wave_access - count;
        if(!ch->ctr)
        {
            ch->sample_no = (ch->sample_no + 1u) & 31u;
            ch->raw = pp->WVRAM[ch->sample_no >> 1];
            ch->raw_out = (ch->sample_no & 1u) ? (ch->raw & 15u) : (ch->raw >> 4);
            ch->ctr = channel_period(ch, 2);
            pp->wave_access = 2; /* DMG access window is two 4 MHz dots. */
        }
    }
    return sum;
}

static sword advance_noise(struct apu_ch_t* ch, word dots)
{
    sword sum = 0;
    word count, bit;
    if(!ch->is_on)
        return 0;
    if((ch->NR_RAW[3] >> 4) >= 14)
        return dots * ch->raw_out * ch->vol;
    while(dots)
    {
        count = dots < ch->ctr ? dots : ch->ctr;
        sum += count * ch->raw_out;
        dots -= count;
        ch->ctr -= count;
        if(!ch->ctr)
        {
            /* XNOR with a zero seed is equivalent to the inverted output
             * of the usual XOR/0x7FFF LFSR representation. */
            bit = (~(ch->raw ^ (ch->raw >> 1))) & 1u;
            ch->raw = (ch->raw >> 1) | (bit << 14);
            if(ch->NR_RAW[3] & 8u)
                ch->raw = (ch->raw & ~(1u << 6)) | (bit << 6);
            ch->raw_out = ch->raw & 1u;
            ch->ctr = channel_period(ch, 3);
        }
    }
    return sum * ch->vol;
}

static void advance_mix(apu_t* pp, word dots)
{
    sword digital[4], analog;
    word i, pan = (pp->MASTER_CFG >> 8) & 0xFFu;
    sword left = 0, right = 0;
    if(!(pp->MASTER_CFG & APU_POWER))
        return;
    digital[0] = advance_pulse(&pp->ch[0], dots);
    digital[1] = advance_pulse(&pp->ch[1], dots);
    digital[2] = advance_wave(pp, dots);
    digital[3] = advance_noise(&pp->ch[3], dots);
    for(i = 0; i < 4; ++i)
    {
        if(!channel_dac(&pp->ch[i], i))
            continue;
        pp->sample_dacs = 1;
        analog = (sword)(15u * dots) - digital[i] * 2;
        if(pan & (1u << i))
            right += analog;
        if(pan & (0x10u << i))
            left += analog;
    }
    pp->sample_sum_r += right * (sword)((pp->MASTER_CFG & 7u) + 1u);
    pp->sample_sum_l += left * (sword)(((pp->MASTER_CFG >> 4) & 7u) + 1u);
}

static s16 filter_sample(sword input, s32* capacitor, wbool dacs)
{
    sword output;
    if(!dacs)
        return 0;
    output = input - *capacitor / 256;
    /* Q8 capacitor; charge factor 1-11/2048 approximates the DMG factor
     * 0.999958^128 at 32768 Hz. No floating point or 64-bit arithmetic. */
#if APU_N_PER_TICK == 32
    *capacitor += (input * 256 - *capacitor) * 11 / 2048;
#else
    /* Close approximation for the other PicoGB sample intervals. */
    *capacitor += (input * 256 - *capacitor) / (sword)(65536u / (APU_N_PER_TICK * 11u));
#endif
    if(output > 32767)
        output = 32767;
    if(output < -32768)
        output = -32768;
    return (s16)output;
}

static void emit_sample(apu_t* pp, wbool render, s16* destination)
{
    s16 output[APU_N_CHANNELS];
    word i;
#if CONFIG_APU_MONO
    output[0] = filter_sample((pp->sample_sum_l + pp->sample_sum_r) * 32 / (sword)APU_SAMPLE_DOTS,
                              &pp->capacitor_l, pp->sample_dacs);
#else
    output[0] = filter_sample(pp->sample_sum_l * 64 / (sword)APU_SAMPLE_DOTS,
                              &pp->capacitor_l, pp->sample_dacs);
    output[1] = filter_sample(pp->sample_sum_r * 64 / (sword)APU_SAMPLE_DOTS,
                              &pp->capacitor_r, pp->sample_dacs);
#endif
    if(render)
    {
        for(i = 0; i < APU_N_CHANNELS; ++i)
        {
            if(destination)
                destination[i] = output[i];
            else if(pp->outbuf && pp->outbuf_size >= APU_N_CHANNELS)
            {
                word size = pp->outbuf_size;
                word index = (size & (size - 1u)) ? pp->outbuf_pos % size
                                                  : pp->outbuf_pos & (size - 1u);
                pp->outbuf[index] = output[i];
            }
            ++pp->outbuf_pos;
        }
    }
    pp->sample_sum_l = pp->sample_sum_r = 0;
    pp->sample_dacs = 0;
}

static void advance_cycles(apu_t* pp, word cycles, wbool render, s16* destination)
{
    word count, sample_left, frame_left;
    while(cycles)
    {
        sample_left = APU_N_PER_TICK - pp->sample_cycles;
        frame_left = 2048u - (pp->CTR_DIV & 2047u);
        count = cycles < sample_left ? cycles : sample_left;
        if(count > frame_left)
            count = frame_left;
        advance_mix(pp, count * 4u);
        cycles -= count;
        pp->CTR_DIV += count;
        pp->CTR_INT += count;
        pp->sample_cycles += count;
        if(!(pp->CTR_DIV & 2047u))
            clock_frame(pp);
        if(pp->sample_cycles == APU_N_PER_TICK)
        {
            emit_sample(pp, render, destination);
            if(destination)
                destination += APU_N_CHANNELS;
            pp->sample_cycles = 0;
        }
    }
}

void apu_flush(apu_t* __restrict pp, wbool render)
{
    word cycles = pp->CTR_INT_FRAC;
    pp->CTR_INT_FRAC = 0;
    advance_cycles(pp, cycles, render, NULL);
}

static void synchronize(apu_t* pp)
{
    if(pp->CTR_INT_FRAC)
        apu_flush(pp, pp->pending_render);
}

static void power_off(apu_t* pp)
{
    word i;
    for(i = 0; i < 4; ++i)
    {
        word length = pp->ch[i].length_ctr; /* DMG length counters survive power off. */
        memset(&pp->ch[i], 0, sizeof(pp->ch[i]));
        pp->ch[i].length_ctr = length;
    }
    pp->MASTER_CFG = 0;
    pp->frame_step = 0;
    pp->wave_access = 0;
    pp->capacitor_l = pp->capacitor_r = 0;
}

void apu_reset(apu_t* __restrict pp)
{
    synchronize(pp);
    power_off(pp);
}

void apu_initialize(apu_t* __restrict pp)
{
    memset(pp->ch, 0, sizeof(pp->ch));
    memset(pp->WVRAM, 0, sizeof(pp->WVRAM));
    pp->MASTER_CFG = pp->CTR_INT = pp->CTR_INT_FRAC = pp->CTR_DIV = 0;
    pp->outbuf_pos = pp->sample_cycles = 0;
    pp->sample_sum_l = pp->sample_sum_r = 0;
    pp->capacitor_l = pp->capacitor_r = 0;
    pp->frame_step = pp->sample_dacs = pp->wave_access = 0;
    pp->pending_render = 1;
}

void apu_div_reset(apu_t* __restrict pp)
{
    synchronize(pp);
    if(pp->CTR_DIV & 1024u)
        clock_frame(pp);
    pp->CTR_DIV = 0;
}

static void trigger_channel(apu_t* pp, word index)
{
    struct apu_ch_t* ch = &pp->ch[index];
    word pace;
    ch->is_on = !!channel_dac(ch, index);
    if(!ch->length_ctr)
    {
        ch->length_ctr = index == 2 ? 256 : 64;
        if((ch->NR_RAW[4] & 0x40u) && (pp->frame_step & 1u))
            --ch->length_ctr;
    }
    if(index < 2)
    {
        ch->ctr = channel_period(ch, index) | (ch->ctr & 3u);
        ch->started = 1;
    }
    else if(index == 2)
    {
        ch->ctr = channel_period(ch, index);
        ch->sample_no = 0;
        pp->wave_access = 0;
    }
    else
    {
        ch->ctr = channel_period(ch, index);
        ch->raw = 0;
        ch->raw_out = 0;
    }
    if(index != 2)
    {
        ch->vol = ch->NR_RAW[2] >> 4;
        pace = ch->NR_RAW[2] & 7u;
        ch->env_ctr = (pace ? pace : 8) + (pp->frame_step == 7);
        ch->env_on = 1;
    }
    if(index == 0)
    {
        ch->raw = channel_frequency(ch);
        pace = (ch->NR_RAW[0] >> 4) & 7u;
        ch->sweep_ctr = pace ? pace : 8;
        ch->sweep_on = !!(pace || (ch->NR_RAW[0] & 7u));
        ch->sweep_negated = 0;
        if((ch->NR_RAW[0] & 7u) && sweep_calculate(ch) > 2047)
            ch->is_on = 0;
    }
}

void apu_write(apu_t* __restrict pp, word addr, word data)
{
    struct apu_ch_t* ch;
    word index, reg, old;
    synchronize(pp);
    addr &= 0xFFu;
    data &= 0xFFu;
    if(addr < 0x10 || addr > 0x26 || addr == 0x15 || addr == 0x1F)
        return;
    if(addr == 0x26)
    {
        if(!(data & 0x80u))
            power_off(pp);
        else if(!(pp->MASTER_CFG & APU_POWER))
        {
            pp->MASTER_CFG |= APU_POWER;
            pp->frame_step = 0;
            pp->ch[2].raw_out = 0;
        }
        return;
    }
    if(addr >= 0x24)
    {
        if(pp->MASTER_CFG & APU_POWER)
        {
            word shift = (addr - 0x24) * 8u;
            pp->MASTER_CFG = (pp->MASTER_CFG & ~(0xFFu << shift)) | (data << shift);
        }
        return;
    }
    index = (addr - 0x10) / 5;
    reg = (addr - 0x10) % 5;
    ch = &pp->ch[index];
    if(!(pp->MASTER_CFG & APU_POWER))
    {
        if(reg == 1)
            ch->length_ctr = index == 2 ? 256u - data : 64u - (data & 63u);
        return;
    }
    old = ch->NR_RAW[reg];
    ch->NR_RAW[reg] = data;
    if(reg == 0 && index == 0 && (old & 8u) && !(data & 8u) && ch->sweep_negated)
        ch->is_on = 0;
    if(reg == 1)
        ch->length_ctr = index == 2 ? 256u - data : 64u - (data & 63u);
    if((reg == 2 && index != 2) || (reg == 0 && index == 2))
        if(!channel_dac(ch, index))
            ch->is_on = 0;
    if(reg == 4)
    {
        if(!(old & 0x40u) && (data & 0x40u) && (pp->frame_step & 1u))
            clock_length(ch);
        if(data & 0x80u)
        {
            if(index == 2 && ch->is_on && pp->wave_access)
            {
                word current = ch->sample_no >> 1;
                if(current < 4)
                    pp->WVRAM[0] = pp->WVRAM[current];
                else
                {
                    current &= ~3u;
                    memmove(pp->WVRAM, &pp->WVRAM[current], 4);
                }
            }
            trigger_channel(pp, index);
        }
        ch->NR_RAW[4] &= 0x7Fu;
    }
}

word apu_read(apu_t* __restrict pp, word addr)
{
    word i, data;
    synchronize(pp);
    addr &= 0xFFu;
    if(addr < 0x10 || addr > 0x26)
        return 0xFF;
    addr -= 0x10;
    if(addr == 22)
    {
        data = (pp->MASTER_CFG & APU_POWER) ? 0x80 : 0;
        for(i = 0; i < 4; ++i)
            if(pp->ch[i].is_on)
                data |= 1u << i;
    }
    else if(addr >= 20)
        data = (pp->MASTER_CFG >> ((addr - 20) * 8u)) & 0xFFu;
    else
        data = pp->ch[addr / 5].NR_RAW[addr % 5];
    return data | read_masks[addr];
}

void apu_write_wave(apu_t* __restrict pp, word addr, word data)
{
    synchronize(pp);
    if(pp->ch[2].is_on)
    {
        if(!pp->wave_access)
            return;
        addr = pp->ch[2].sample_no >> 1;
    }
    pp->WVRAM[addr & 15u] = data & 0xFFu;
}

word apu_read_wave(apu_t* __restrict pp, word addr)
{
    synchronize(pp);
    if(pp->ch[2].is_on)
    {
        if(!pp->wave_access)
            return 0xFF;
        addr = pp->ch[2].sample_no >> 1;
    }
    return pp->WVRAM[addr & 15u];
}

void apu_tick_internal_internals(apu_t* __restrict pp)
{
    synchronize(pp);
    advance_cycles(pp, APU_N_PER_TICK, 0, NULL);
}

void apu_tick_internal(apu_t* __restrict pp)
{
    synchronize(pp);
    advance_cycles(pp, APU_N_PER_TICK, 1, NULL);
}

void apu_render(apu_t* __restrict pp, s16* outbuf, word ncounts)
{
    synchronize(pp);
    advance_cycles(pp, ncounts * APU_N_PER_TICK, 1, outbuf);
}

void apu_render_faster(apu_t* __restrict pp, s16* outbuf, word ncounts)
{
    apu_render(pp, outbuf, ncounts);
}
