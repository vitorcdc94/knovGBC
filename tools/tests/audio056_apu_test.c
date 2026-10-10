#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "apu.h"

static s16 samples_a[32768], samples_b[32768];

static void init(apu_t* a, s16* samples, word size)
{
    memset(a, 0, sizeof(*a));
    a->outbuf = samples;
    a->outbuf_size = size;
    apu_initialize(a);
}

static void power(apu_t* a)
{
    apu_write(a, 0x26, 0x80);
    apu_write(a, 0x24, 0x77);
    apu_write(a, 0x25, 0xFF);
}

static void tick(apu_t* a, word cycles)
{
    apu_tick(a, cycles, 1);
    apu_flush(a, 1);
}

static void pulse(apu_t* a, word channel, word frequency, word envelope, word duty)
{
    word base = channel ? 0x15 : 0x10;
    apu_write(a, base + 1, duty << 6);
    apu_write(a, base + 2, envelope);
    apu_write(a, base + 3, frequency & 255);
    apu_write(a, base + 4, 0x80 | (frequency >> 8));
}

static void test_clock_ring_and_off(void)
{
    apu_t a;
    s16 guarded[19];
    word i;
    guarded[0] = 1234;
    guarded[18] = 2345;
    init(&a, guarded + 1, 17);
    tick(&a, 1048576);
    assert(a.outbuf_pos == 32768 && a.CTR_INT == 1048576);
    assert(guarded[0] == 1234 && guarded[18] == 2345);
    for(i = 1; i <= 17; ++i)
        assert(!guarded[i]);
    power(&a);
    pulse(&a, 0, 1792, 0xF0, 2);
    tick(&a, 32 * 40);
    apu_write(&a, 0x26, 0);
    i = a.outbuf_pos;
    tick(&a, 32 * 17);
    assert(a.outbuf_pos == i + 17);
    for(i = 1; i <= 17; ++i)
        assert(!guarded[i]);
    assert(apu_read(&a, 0x26) == 0x70);
    a.outbuf_pos = UINT_MAX - 1u;
    tick(&a, 96);
    assert(a.outbuf_pos == 1);
    assert(guarded[0] == 1234 && guarded[18] == 2345);
}

static void test_registers_and_lengths(void)
{
    static const r8 masks[23] = {
        0x80, 0x3F, 0, 0xFF, 0xBF, 0xFF, 0x3F, 0, 0xFF, 0xBF,
        0x7F, 0xFF, 0x9F, 0xFF, 0xBF, 0xFF, 0xFF, 0, 0, 0xBF, 0, 0, 0x70
    };
    apu_t a;
    word i;
    init(&a, samples_a, 32768);
    for(i = 0; i < 23; ++i)
        assert(apu_read(&a, 0x10 + i) == masks[i]);
    for(i = 0x27; i < 0x30; ++i)
    {
        apu_write(&a, i, 0xFF);
        assert(apu_read(&a, i) == 0xFF);
    }
    assert(apu_read(&a, 0x0F) == 0xFF);
    apu_write(&a, 0x11, 0xFF); /* DMG powered-off length write, duty stays zero. */
    apu_write(&a, 0x1B, 0);
    assert(a.ch[0].length_ctr == 1 && a.ch[2].length_ctr == 256);
    assert(apu_read(&a, 0x11) == 0x3F);
    power(&a);
    apu_write(&a, 0x12, 0xF0);
    apu_write(&a, 0x14, 0xC0);
    assert(apu_read(&a, 0x26) == 0xF1);
    tick(&a, 2047);
    assert(a.ch[0].length_ctr == 1 && a.ch[0].is_on);
    tick(&a, 1);
    assert(!a.ch[0].length_ctr && !a.ch[0].is_on);
    apu_write(&a, 0x14, 0xC0); /* Next step1 does not clock length. */
    assert(a.ch[0].length_ctr == 63);
    apu_write(&a, 0x26, 0);
    assert(a.ch[0].length_ctr == 63);
    assert(apu_read(&a, 0x24) == 0 && apu_read(&a, 0x25) == 0);
    power(&a);
    apu_write(&a, 0x16, 63);
    apu_write(&a, 0x19, 0x40); /* Length clocks even an inactive channel. */
    tick(&a, 2048);
    assert(!a.ch[1].length_ctr);
    for(i = 0; i < 22; ++i)
    {
        apu_write(&a, 0x10 + i, 0x55);
        assert(apu_read(&a, 0x10 + i) == (0x55u | masks[i]));
    }
    apu_write(&a, 0x26, 0x8F);
    assert(!(apu_read(&a, 0x26) & 15)); /* NR52 low bits are read-only. */
}

static void test_div_and_rates(void)
{
    apu_t a;
    init(&a, samples_a, 32768);
    power(&a);
    pulse(&a, 0, 0, 0xF0, 2);
    apu_write(&a, 0x11, 63);
    apu_write(&a, 0x14, 0x40);
    apu_tick(&a, 1023, 1);
    apu_div_reset(&a);
    assert(a.ch[0].is_on && !a.frame_step && !a.CTR_DIV);
    apu_tick(&a, 1024, 1);
    apu_div_reset(&a);
    assert(!a.ch[0].is_on && a.frame_step == 1);
    assert(a.CTR_INT == 2047 && a.outbuf_pos == 63);
    init(&a, samples_a, 32768);
    power(&a);
    pulse(&a, 0, 0, 0xF0, 2);
    apu_write(&a, 0x14, 0x40);
    tick(&a, 2048);
    assert(a.ch[0].length_ctr == 63);
    tick(&a, 4095);
    assert(a.ch[0].length_ctr == 63);
    tick(&a, 1);
    assert(a.ch[0].length_ctr == 62);
    init(&a, samples_a, 32768);
    a.CTR_DIV = 0x26E168;
    power(&a);
    apu_write(&a, 0x16, 63);
    apu_write(&a, 0x19, 0x40);
    tick(&a, 2047 - (0x26E168u & 2047));
    assert(a.ch[1].length_ctr == 1);
    tick(&a, 1);
    assert(!a.ch[1].length_ctr);
}

static void test_envelope_and_sweep(void)
{
    apu_t a;
    init(&a, samples_a, 32768);
    power(&a);
    pulse(&a, 0, 0, 0x11, 2);
    tick(&a, 16383);
    assert(a.ch[0].vol == 1);
    tick(&a, 1);
    assert(!a.ch[0].vol && (apu_read(&a, 0x26) & 1));
    apu_write(&a, 0x12, 0);
    assert(!(apu_read(&a, 0x26) & 1));
    pulse(&a, 1, 0, 0xE9, 2);
    tick(&a, 16384);
    assert(a.ch[1].vol == 15);
    tick(&a, 16384 * 3);
    assert(a.ch[1].vol == 15);
    pulse(&a, 1, 0, 0xF8, 2);
    tick(&a, 16384 * 10);
    assert(a.ch[1].vol == 15);
    init(&a, samples_a, 32768);
    power(&a);
    apu_write(&a, 0x10, 0x11);
    pulse(&a, 0, 1400, 0xF0, 2);
    assert(!a.ch[0].is_on); /* Trigger overflow. */
    pulse(&a, 0, 1000, 0xF0, 2);
    assert(a.ch[0].is_on);
    tick(&a, 6143);
    assert(a.ch[0].raw == 1000);
    tick(&a, 1);
    assert(a.ch[0].raw == 1500 && !a.ch[0].is_on); /* Second overflow check. */
    apu_write(&a, 0x10, 0x19);
    pulse(&a, 0, 1000, 0xF0, 2);
    assert(a.ch[0].is_on && a.ch[0].sweep_negated);
    apu_write(&a, 0x10, 0x11);
    assert(!a.ch[0].is_on);
}

static word noise_reference(word state, word short_mode, word clocks)
{
    while(clocks--)
    {
        word bit = (~(state ^ (state >> 1))) & 1;
        state = (state >> 1) | (bit << 14);
        if(short_mode)
            state = (state & ~(1u << 6)) | (bit << 6);
    }
    return state;
}

static void test_noise_and_wave(void)
{
    apu_t a;
    word i, mode;
    for(mode = 0; mode <= 8; mode += 8)
    {
        init(&a, samples_a, 32768);
        power(&a);
        apu_write(&a, 0x21, 0xF0);
        apu_write(&a, 0x22, mode);
        apu_write(&a, 0x23, 0x80);
        tick(&a, 32);
        assert(a.ch[3].raw == noise_reference(0, mode, 16));
        assert(a.ch[3].raw_out == (a.ch[3].raw & 1));
        apu_write(&a, 0x22, 0xE0);
        apu_write(&a, 0x23, 0x80);
        tick(&a, 1048576);
        assert(!a.ch[3].raw);
    }
    init(&a, samples_a, 32768);
    power(&a);
    for(i = 0; i < 16; ++i)
        apu_write_wave(&a, 0x30 + i, i * 17);
    apu_write(&a, 0x1A, 0x80);
    apu_write(&a, 0x1C, 0x20);
    apu_write(&a, 0x1D, 0xFE);
    apu_write(&a, 0x1E, 0x87);
    assert(apu_read_wave(&a, 0x30) == 0xFF);
    tick(&a, 1);
    assert(a.ch[2].sample_no == 1 && a.ch[2].raw_out == 0);
    assert(apu_read_wave(&a, 0x3F) == 0);
    apu_write_wave(&a, 0x3F, 0x45);
    assert(a.WVRAM[0] == 0x45 && a.WVRAM[15] == 0xFF);
    tick(&a, 31);
    assert(a.ch[2].sample_no == 0 && a.ch[2].raw_out == 4);
    apu_write(&a, 0x1A, 0);
    assert(!(apu_read(&a, 0x26) & 4));
    apu_write_wave(&a, 0x3F, 0x67);
    assert(apu_read_wave(&a, 0x3F) == 0x67);
}

static void setup_all(apu_t* a, s16* samples)
{
    word i;
    init(a, samples, 32768);
    power(a);
    apu_write(a, 0x10, 0x25);
    pulse(a, 0, 1300, 0xCA, 3);
    pulse(a, 1, 2047, 0xB2, 1);
    for(i = 0; i < 16; ++i)
        apu_write_wave(a, i, i * 13);
    apu_write(a, 0x1A, 0x80);
    apu_write(a, 0x1C, 0x60);
    apu_write(a, 0x1D, 0xFF);
    apu_write(a, 0x1E, 0x87);
    apu_write(a, 0x21, 0xA3);
    apu_write(a, 0x22, 0x08);
    apu_write(a, 0x23, 0x80);
}

static void test_partition_and_muting(void)
{
    apu_t a, b;
    word i, cycles, state = 1;
    setup_all(&a, samples_a);
    setup_all(&b, samples_b);
    for(i = 0; i < 100; ++i)
    {
        word remaining = 1301;
        apu_tick(&a, remaining, 1);
        while(remaining)
        {
            state = state * 1664525u + 1013904223u;
            cycles = (state >> 24) % 53u + 1;
            if(cycles > remaining)
                cycles = remaining;
            apu_tick(&b, cycles, 1);
            remaining -= cycles;
        }
        apu_write(&a, 0x24, i);
        apu_write(&b, 0x24, i);
        apu_write(&a, 0x25, i * 7);
        apu_write(&b, 0x25, i * 7);
    }
    apu_flush(&a, 1);
    apu_flush(&b, 1);
    assert(a.outbuf_pos == 130100 / 32);
    assert(a.outbuf_pos == b.outbuf_pos);
    assert(!memcmp(samples_a, samples_b, a.outbuf_pos * sizeof(s16)));
    assert(!memcmp(a.ch, b.ch, sizeof(a.ch)));
    assert(a.sample_sum_l == b.sample_sum_l && a.sample_sum_r == b.sample_sum_r);
    assert(a.capacitor_l == b.capacitor_l);
    setup_all(&a, samples_a);
    setup_all(&b, samples_b);
    apu_tick(&a, 65537, 1);
    apu_tick(&b, 65537, 0);
    apu_flush(&a, 1);
    apu_flush(&b, 0);
    assert(!b.outbuf_pos);
    assert(!memcmp(a.ch, b.ch, sizeof(a.ch)));
    assert(a.frame_step == b.frame_step && a.sample_cycles == b.sample_cycles);
    assert(a.capacitor_l == b.capacitor_l);
}

static void test_mixing_and_frequency(void)
{
    apu_t a;
    word i, transitions = 0, positive = 0, negative = 0;
    init(&a, samples_a, 32768);
    power(&a);
    apu_write(&a, 0x24, 0x70);
    apu_write(&a, 0x25, 0x10);
    pulse(&a, 0, 0, 0xF0, 2);
    tick(&a, 32);
    assert(samples_a[0] == 3840); /* Mono averages left/right; left gain8. */
    init(&a, samples_a, 32768);
    power(&a);
    apu_write(&a, 0x24, 0x70);
    apu_write(&a, 0x25, 1);
    pulse(&a, 0, 0, 0xF0, 2);
    tick(&a, 32);
    assert(samples_a[0] == 480); /* Mono averages left/right; right gain1. */
    init(&a, samples_a, 32768);
    power(&a);
    apu_write(&a, 0x24, 0);
    apu_write(&a, 0x25, 1);
    pulse(&a, 0, 0, 0xF0, 2);
    apu_tick(&a, 16, 1);
    apu_write(&a, 0x25, 0); /* Must flush exactly half of this PCM box. */
    tick(&a, 16);
    assert(samples_a[0] == 240);
    init(&a, samples_a, 32768);
    power(&a);
    apu_write(&a, 0x25, 0x11);
    pulse(&a, 0, 1792, 0xF0, 2); /* 131072/(2048-1792) = 512 Hz. */
    tick(&a, 1048576);
    assert(a.outbuf_pos == 32768);
    for(i = 1; i < 32768; ++i)
    {
        positive |= samples_a[i] > 0;
        negative |= samples_a[i] < 0;
        if(samples_a[i - 1] < 0 && samples_a[i] >= 0)
            ++transitions;
    }
    assert(positive && negative && transitions >= 511 && transitions <= 513);
    assert(a.ch[0].sample_no == 0 && a.ch[0].ctr == 1024);
}

int main(void)
{
    test_clock_ring_and_off();
    test_registers_and_lengths();
    test_div_and_rates();
    test_envelope_and_sweep();
    test_noise_and_wave();
    test_partition_and_muting();
    test_mixing_and_frequency();
    puts("audio056 APU: sample counts, power-off silence, ring bounds, register masks, DIV, length, envelope, sweep, pulse, wave, noise, routing, and chunk partitions passed");
    return 0;
}
