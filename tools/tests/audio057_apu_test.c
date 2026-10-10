#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apu.h"

/* Keep the frozen 056 state and entry points independent of the live APU. */
#define apu_t audio056_apu_t
#define apu_ch_t audio056_apu_ch_t
#define apu_initialize audio056_apu_initialize
#define apu_reset audio056_apu_reset
#define apu_write audio056_apu_write
#define apu_read audio056_apu_read
#define apu_write_wave audio056_apu_write_wave
#define apu_read_wave audio056_apu_read_wave
#define apu_div_reset audio056_apu_div_reset
#define apu_flush audio056_apu_flush
#define apu_render audio056_apu_render
#define apu_render_faster audio056_apu_render_faster
#define apu_tick_internal_internals audio056_apu_tick_internal_internals
#define apu_tick_internal audio056_apu_tick_internal
#define apu_tick audio056_apu_tick
#include "fixtures/audio056/apu.h"
#undef apu_t
#undef apu_ch_t
#undef apu_initialize
#undef apu_reset
#undef apu_write
#undef apu_read
#undef apu_write_wave
#undef apu_read_wave
#undef apu_div_reset
#undef apu_flush
#undef apu_render
#undef apu_render_faster
#undef apu_tick_internal_internals
#undef apu_tick_internal
#undef apu_tick

#define MAX_RING 4096u
static const char* phase;
static word event_number;
static uint64_t pcm_hash = UINT64_C(14695981039346656037);
static uint64_t samples_compared;
static word checks;

#define REQUIRE(expression) do { \
    if(!(expression)) { \
        fprintf(stderr, "audio057 APU: %s event=%u line=%d: %s\n", \
                phase, event_number, __LINE__, #expression); \
        exit(1); \
    } \
} while(0)

struct pair_t
{
    apu_t current;
    audio056_apu_t reference;
    s16 current_buffer[MAX_RING + 2];
    s16 reference_buffer[MAX_RING + 2];
    word capacity;
};

static void compare_state(struct pair_t* pair)
{
    apu_t* a = &pair->current;
    audio056_apu_t* b = &pair->reference;
    REQUIRE(sizeof(a->ch) == sizeof(b->ch));
    REQUIRE(!memcmp(a->ch, b->ch, sizeof(a->ch)));
    REQUIRE(!memcmp(a->WVRAM, b->WVRAM, sizeof(a->WVRAM)));
    REQUIRE(a->MASTER_CFG == b->MASTER_CFG);
    REQUIRE(a->CTR_INT == b->CTR_INT);
    REQUIRE(a->CTR_INT_FRAC == b->CTR_INT_FRAC);
    REQUIRE(a->CTR_DIV == b->CTR_DIV);
    REQUIRE(a->outbuf_pos == b->outbuf_pos);
    REQUIRE(a->outbuf_size == b->outbuf_size);
    REQUIRE(a->sample_cycles == b->sample_cycles);
#if CONFIG_APU_MONO
    /* 057 merges stereo terminal areas before averaging. The observable
     * mono area is identical; its two internal accumulators need not be. */
    REQUIRE(a->sample_sum_l + a->sample_sum_r == b->sample_sum_l + b->sample_sum_r);
#if APU_MONO_FAST
    REQUIRE(a->sample_sum_r == 0);
#endif
#else
    REQUIRE(a->sample_sum_l == b->sample_sum_l);
    REQUIRE(a->sample_sum_r == b->sample_sum_r);
#endif
    REQUIRE(a->capacitor_l == b->capacitor_l);
    REQUIRE(a->capacitor_r == b->capacitor_r);
    REQUIRE(a->frame_step == b->frame_step);
    REQUIRE(a->pending_render == b->pending_render);
    REQUIRE(a->sample_dacs == b->sample_dacs);
    REQUIRE(a->wave_access == b->wave_access);
    REQUIRE(pair->current_buffer[0] == 1234 && pair->reference_buffer[0] == 1234);
    REQUIRE(pair->current_buffer[pair->capacity + 1] == -2345);
    REQUIRE(pair->reference_buffer[pair->capacity + 1] == -2345);
    REQUIRE(!memcmp(pair->current_buffer, pair->reference_buffer,
                    (pair->capacity + 2u) * sizeof(s16)));
    ++checks;
}

static void compare_output(struct pair_t* pair, word old_position)
{
    word i, count = pair->current.outbuf_pos - old_position;
    compare_state(pair);
    REQUIRE(count <= pair->capacity);
    for(i = 0; i < count; ++i)
    {
        word index = (old_position + i) % pair->capacity;
        s16 sample = pair->current.outbuf[index];
        REQUIRE(sample == pair->reference.outbuf[index]);
        pcm_hash ^= (uint16_t)sample;
        pcm_hash *= UINT64_C(1099511628211);
        ++samples_compared;
    }
}

static void pair_init(struct pair_t* pair, word capacity)
{
    REQUIRE(capacity >= APU_N_CHANNELS && capacity <= MAX_RING);
    REQUIRE(capacity % APU_N_CHANNELS == 0);
    memset(pair, 0, sizeof(*pair));
    pair->capacity = capacity;
    pair->current_buffer[0] = pair->reference_buffer[0] = 1234;
    pair->current_buffer[capacity + 1] = pair->reference_buffer[capacity + 1] = -2345;
    pair->current.outbuf = pair->current_buffer + 1;
    pair->reference.outbuf = pair->reference_buffer + 1;
    pair->current.outbuf_size = pair->reference.outbuf_size = capacity;
    apu_initialize(&pair->current);
    audio056_apu_initialize(&pair->reference);
    compare_state(pair);
}

static void pair_tick(struct pair_t* pair, word cycles, wbool render)
{
    /* Drain often enough to compare every emitted sample even for tiny
     * rings. Partial samples and pending cycles may coexist after sound I/O. */
    word maximum = (pair->capacity / APU_N_CHANNELS) * APU_N_PER_TICK - 31u;
    if(maximum > 1024)
        maximum = 1024;
    while(cycles)
    {
        word count = cycles < maximum ? cycles : maximum;
        word old_position = pair->current.outbuf_pos;
        apu_tick(&pair->current, count, render);
        audio056_apu_tick(&pair->reference, count, render);
        cycles -= count;
        compare_output(pair, old_position);
    }
}

static void pair_flush(struct pair_t* pair, wbool render)
{
    word old_position = pair->current.outbuf_pos;
    apu_flush(&pair->current, render);
    audio056_apu_flush(&pair->reference, render);
    compare_output(pair, old_position);
}

static void pair_write(struct pair_t* pair, word address, word data)
{
    word old_position = pair->current.outbuf_pos;
    apu_write(&pair->current, address, data);
    audio056_apu_write(&pair->reference, address, data);
    compare_output(pair, old_position);
}

static void pair_wave_write(struct pair_t* pair, word address, word data)
{
    word old_position = pair->current.outbuf_pos;
    apu_write_wave(&pair->current, address, data);
    audio056_apu_write_wave(&pair->reference, address, data);
    compare_output(pair, old_position);
}

static void pair_read(struct pair_t* pair, word address, wbool wave)
{
    word old_position = pair->current.outbuf_pos;
    word a = wave ? apu_read_wave(&pair->current, address) : apu_read(&pair->current, address);
    word b = wave ? audio056_apu_read_wave(&pair->reference, address)
                  : audio056_apu_read(&pair->reference, address);
    REQUIRE(a == b);
    compare_output(pair, old_position);
}

static void pair_div_reset(struct pair_t* pair)
{
    word old_position = pair->current.outbuf_pos;
    apu_div_reset(&pair->current);
    audio056_apu_div_reset(&pair->reference);
    compare_output(pair, old_position);
}

static void pair_reset(struct pair_t* pair)
{
    word old_position = pair->current.outbuf_pos;
    apu_reset(&pair->current);
    audio056_apu_reset(&pair->reference);
    compare_output(pair, old_position);
}

static void all_channels(struct pair_t* pair)
{
    word i;
    pair_write(pair, 0x26, 0x80);
    pair_write(pair, 0x24, 0x77);
    pair_write(pair, 0x25, 0xFF);
    pair_write(pair, 0x11, 0xC0);
    pair_write(pair, 0x12, 0xF0);
    pair_write(pair, 0x13, 0xD3);
    pair_write(pair, 0x14, 0x86);
    pair_write(pair, 0x16, 0x40);
    pair_write(pair, 0x17, 0xB8);
    pair_write(pair, 0x18, 0xFF);
    pair_write(pair, 0x19, 0x87);
    for(i = 0; i < 16; ++i)
        pair_wave_write(pair, 0x30 + i, (i * 73u) ^ 0x5Au);
    pair_write(pair, 0x1A, 0x80);
    pair_write(pair, 0x1C, 0x20);
    pair_write(pair, 0x1D, 0xF3);
    pair_write(pair, 0x1E, 0x87);
    pair_write(pair, 0x21, 0xD0);
    pair_write(pair, 0x22, 0);
    pair_write(pair, 0x23, 0x80);
}

static void exhaust_routes(void)
{
    struct pair_t pair;
    word volume, pan, i;
    phase = "all 65536 NR50/NR51 pairs";
    event_number = 0;
    pair_init(&pair, MAX_RING);
    all_channels(&pair);
    for(volume = 0; volume < 256; ++volume)
        for(pan = 0; pan < 256; ++pan)
        {
            event_number = volume * 256u + pan;
            pair_tick(&pair, (volume & 31u) + 1u, 1);
            pair_write(&pair, 0x24, volume);
            pair_tick(&pair, (pan & 15u) + 1u, 1);
            pair_write(&pair, 0x25, pan);
            pair_tick(&pair, 35u + ((volume ^ pan) & 15u), 1);
            pair_read(&pair, 0x24, 0);
            pair_read(&pair, 0x25, 0);
            if(!(event_number & 255u))
                for(i = 0x10; i < 0x30; ++i)
                    pair_read(&pair, i, 0);
        }
    pair_flush(&pair, 1);
    for(i = 0; i < 4; ++i)
        REQUIRE(pair.current.ch[i].is_on);
}

static word random_value(word* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static void scripted_events(void)
{
    struct pair_t pair;
    word i, offset;
    phase = "power, sweep, envelopes, DIV and zero routes";
    event_number = 0;
    pair_init(&pair, 514);
    all_channels(&pair);
    for(i = 0; i < 4; ++i)
    {
        event_number = i;
        pair_tick(&pair, 129u + i, 1);
        pair_write(&pair, 0x25, 0); /* Active DACs must keep the HPF connected. */
        pair_tick(&pair, 2051u + i, 1);
        pair_write(&pair, 0x25, 0xFF);
    }
    pair_write(&pair, 0x10, 0x11);
    pair_write(&pair, 0x13, 1000 & 255);
    pair_write(&pair, 0x14, 0x80 | (1000 >> 8));
    pair_write(&pair, 0x12, 0x11);
    pair_write(&pair, 0x14, 0x80 | (1000 >> 8));
    pair_write(&pair, 0x17, 0xE9);
    pair_write(&pair, 0x19, 0x87);
    pair_write(&pair, 0x21, 0xF2);
    pair_write(&pair, 0x23, 0xC0);
    pair_tick(&pair, 65536, 1);
    for(offset = 0; offset < 32; ++offset)
    {
        event_number = offset + 4;
        pair_tick(&pair, offset + 1u, 1);
        pair_write(&pair, 0x24, offset * 7u);
        pair_write(&pair, 0x25, offset * 13u);
        pair_write(&pair, 0x26, 0);
        pair_write(&pair, 0x24, 0x77); /* Ignored while off. */
        pair_write(&pair, 0x25, 0xFF);
        pair_write(&pair, 0x11, 63);
        pair_write(&pair, 0x1B, 0);
        pair_tick(&pair, 67u - offset, 1);
        pair_read(&pair, 0x26, 0);
        pair_write(&pair, 0x26, 0x80);
        all_channels(&pair);
        pair_tick(&pair, 1023, 1);
        pair_div_reset(&pair);
        pair_tick(&pair, 1024, 1);
        pair_div_reset(&pair);
        pair_tick(&pair, 31u - offset, 0);
        pair_write(&pair, 0x24, offset * 17u);
        pair_tick(&pair, 33u + offset, 1);
    }
    pair_reset(&pair);
    pair_tick(&pair, 129, 1);
    all_channels(&pair);
    pair_flush(&pair, 1);
}

static void random_stream(word capacity, word seed)
{
    struct pair_t pair;
    word i;
    phase = "deterministic random stream and ring wraps";
    pair_init(&pair, capacity);
    all_channels(&pair);
    pair.current.CTR_DIV = pair.reference.CTR_DIV = 0x26E168;
    pair.current.outbuf_pos = pair.reference.outbuf_pos = UINT_MAX - 3u * APU_N_CHANNELS;
    pair_tick(&pair, 256, 1);
    for(i = 0; i < 20000; ++i)
    {
        word value = random_value(&seed);
        word address = 0x10u + ((value >> 16) & 31u);
        event_number = i;
        switch(value & 15u)
        {
            case 0:
            case 1:
            case 2:
            case 3:
                pair_write(&pair, address, value >> 8);
                break;
            case 4:
                pair_wave_write(&pair, 0x30u + ((value >> 8) & 15u), value >> 16);
                break;
            case 5:
                pair_read(&pair, address, 0);
                break;
            case 6:
                pair_read(&pair, value >> 16, 1);
                break;
            case 7:
                pair_div_reset(&pair);
                break;
            case 8:
                pair_tick(&pair, (value >> 8) & 511u, 0);
                break;
            case 9:
                pair_flush(&pair, (value >> 8) & 1u);
                break;
            case 10:
                pair_write(&pair, 0x24, value >> 8);
                pair_write(&pair, 0x25, value >> 16);
                break;
            case 11:
                pair_write(&pair, 0x26, (value >> 8) & 0x80u);
                break;
            case 12:
                pair_write(&pair, 0x26, 0x80);
                pair_write(&pair, 0x12, value >> 8);
                pair_write(&pair, 0x14, 0x80u | ((value >> 16) & 0x47u));
                pair_write(&pair, 0x1A, value >> 8);
                pair_write(&pair, 0x1E, 0x80u | ((value >> 16) & 0x47u));
                break;
            case 13:
                if(!(value & 0x100u))
                    pair_reset(&pair);
                else
                    all_channels(&pair);
                break;
            default:
                pair_tick(&pair, (value >> 8) & 4095u, 1);
                break;
        }
        pair_tick(&pair, (value >> 24) & 63u, 1);
    }
    pair_flush(&pair, 1);
}

static void partition_equivalence(void)
{
    struct pair_t whole, split;
    word i, seed = 0x137Bu;
    phase = "chunk partition equivalence";
    event_number = 0;
    pair_init(&whole, MAX_RING);
    pair_init(&split, MAX_RING);
    all_channels(&whole);
    all_channels(&split);
    for(i = 0; i < 1000; ++i)
    {
        word remaining = (random_value(&seed) & 255u) + 1u;
        word route = random_value(&seed);
        wbool render = (route >> 20) & 1u;
        event_number = i;
        pair_tick(&whole, remaining, render);
        while(remaining)
        {
            word count = (random_value(&seed) & 15u) + 1u;
            if(count > remaining)
                count = remaining;
            pair_tick(&split, count, render);
            remaining -= count;
        }
        /* Both pending accumulators synchronize at the same I/O boundary. */
        pair_write(&whole, 0x24, route);
        pair_write(&split, 0x24, route);
        pair_write(&whole, 0x25, route >> 8);
        pair_write(&split, 0x25, route >> 8);
        REQUIRE(!memcmp(whole.current.ch, split.current.ch, sizeof(whole.current.ch)));
        REQUIRE(whole.current.outbuf_pos == split.current.outbuf_pos);
        REQUIRE(!memcmp(whole.current_buffer, split.current_buffer, sizeof(whole.current_buffer)));
        REQUIRE(whole.current.sample_sum_l == split.current.sample_sum_l);
        REQUIRE(whole.current.sample_sum_r == split.current.sample_sum_r);
        REQUIRE(whole.current.capacitor_l == split.current.capacitor_l);
        REQUIRE(whole.current.capacitor_r == split.current.capacitor_r);
    }
}

static void legacy_render_entry_points(void)
{
    struct pair_t pair;
    s16 actual[16 * APU_N_CHANNELS + 2], expected[16 * APU_N_CHANNELS + 2];
    word i;
    phase = "legacy render wrappers";
    event_number = 0;
    pair_init(&pair, MAX_RING);
    all_channels(&pair);
    pair_tick(&pair, 7, 1);
    memset(actual, 0, sizeof(actual));
    memset(expected, 0, sizeof(expected));
    actual[0] = expected[0] = 1234;
    actual[16 * APU_N_CHANNELS + 1] = expected[16 * APU_N_CHANNELS + 1] = -2345;
    apu_render(&pair.current, actual + 1, 16);
    audio056_apu_render(&pair.reference, expected + 1, 16);
    REQUIRE(!memcmp(actual, expected, sizeof(actual)));
    compare_state(&pair);
    for(i = 0; i < 4; ++i)
    {
        word old_position = pair.current.outbuf_pos;
        event_number = i;
        apu_tick_internal(&pair.current);
        audio056_apu_tick_internal(&pair.reference);
        compare_output(&pair, old_position);
        apu_tick_internal_internals(&pair.current);
        audio056_apu_tick_internal_internals(&pair.reference);
        compare_state(&pair);
    }
    apu_render_faster(&pair.current, actual + 1, 16);
    audio056_apu_render_faster(&pair.reference, expected + 1, 16);
    REQUIRE(!memcmp(actual, expected, sizeof(actual)));
    compare_state(&pair);
    /* Initialization must also invalidate any previously populated cache. */
    apu_initialize(&pair.current);
    audio056_apu_initialize(&pair.reference);
    compare_state(&pair);
}

int main(void)
{
    static const word mono_capacities[] = { 1, 2, 17, 64, 257, 4096 };
    static const word stereo_capacities[] = { 2, 4, 18, 64, 514, 4096 };
    const word* capacities = CONFIG_APU_MONO ? mono_capacities : stereo_capacities;
    word i;
    exhaust_routes();
    scripted_events();
    for(i = 0; i < 6; ++i)
        random_stream(capacities[i], 0x12345678u + i);
    partition_equivalence();
    legacy_render_entry_points();
    printf("audio057 differential: mono=%u fast=%u, 65536 routes, %u state checks, "
           "%llu exact PCM entries, hash=%016llx passed\n", CONFIG_APU_MONO,
           APU_MONO_FAST, checks, (unsigned long long)samples_compared,
           (unsigned long long)pcm_hash);
    return 0;
}
