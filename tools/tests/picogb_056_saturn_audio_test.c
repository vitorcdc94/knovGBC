#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint16_t test_sound_ram[0x80000U / 2U];
static uint16_t test_scsp[0x1000U / 2U];
static uint32_t test_ticks;
static uint8_t test_mask = 6U;
static unsigned int test_sound_commands;
static int test_standard;

#define PICOGB_AUDIO_SOUND_RAM_BASE ((uintptr_t)test_sound_ram)
#define PICOGB_AUDIO_SCSP_REG_BASE ((uintptr_t)test_scsp)
#include "../../src/saturn/picogb_saturn_audio.c"

uint8_t cpu_intc_mask_get(void) { return test_mask; }
void cpu_intc_mask_set(uint8_t mask) { test_mask = mask; }
int vdp2_tvmd_tv_standard_get(void) { return test_standard; }
uint32_t mvp_profiler_ticks_get(void) { return test_ticks; }

uint8_t
smpc_smc_sndoff_call(void)
{
    assert(test_mask == 15U);
    assert((test_sound_commands & 1U) == 0U);
    test_sound_commands++;
    return 7U;
}

uint8_t
smpc_smc_sndon_call(void)
{
    assert(test_mask == 15U);
    assert((test_sound_commands & 1U) != 0U);
    assert(test_sound_ram[0] == 0x0007U);
    assert(test_sound_ram[1] == 0xFF00U);
    assert(test_sound_ram[2] == 0x0000U);
    assert(test_sound_ram[3] == 0x0008U);
    assert(test_sound_ram[4] == 0x46FCU);
    assert(test_sound_ram[5] == 0x2700U);
    assert(test_sound_ram[6] == 0x60FEU);
    assert(test_scsp[0x400U / 2U] == 0x0200U);
    test_sound_commands++;
    return 6U;
}

static void
change_page(uint16_t page)
{
    test_ticks += 3000U;
    test_scsp[0x408U / 2U] = page << AUDIO_MONITOR_SHIFT;
    picogb_saturn_audio_pump();
}

static void
assert_segment(uint32_t segment, uint16_t value)
{
    for (uint32_t i = 0U; i < PICOGB_SATURN_AUDIO_SEGMENT_SAMPLES; i++) {
        assert(test_sound_ram[AUDIO_PCM_RAM_OFFSET / 2U +
            segment * PICOGB_SATURN_AUDIO_SEGMENT_SAMPLES + i] == value);
    }
}

int
main(void)
{
    static int16_t samples[PICOGB_SATURN_AUDIO_BUFFER_SAMPLES + 512U];
    for (uint32_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        samples[i] = -12345;
    }
    picogb_saturn_audio_status_t status;
    assert(picogb_saturn_audio_submit(samples, 512U) == 0U);
    picogb_saturn_audio_pump();
    picogb_saturn_audio_status(NULL);

    memset(test_sound_ram, 0x55, sizeof(test_sound_ram));
    memset(test_scsp, 0xAA, sizeof(test_scsp));
    assert(picogb_saturn_audio_init());
    assert(test_mask == 6U);
    assert(test_sound_commands == 2U);
    assert(test_scsp[0x400U / 2U] == 0x020FU);
    assert(test_scsp[0x41EU / 2U] == 0U);
    assert(test_scsp[0x42AU / 2U] == 0U);
    assert(test_scsp[0x06U / 2U] == 8191U);
    assert(test_scsp[0x10U / 2U] == 0x79F2U);
    assert(test_scsp[0x16U / 2U] == 0xE000U);
    assert(test_sound_ram[(AUDIO_PCM_RAM_OFFSET - 2U) / 2U] == 0x5555U);
    assert(test_sound_ram[(AUDIO_PCM_RAM_OFFSET + 16384U) / 2U] == 0x5555U);
    assert_segment(0U, 0U);
    assert_segment(1U, 0U);
    for (uint32_t slot = 1U; slot < 32U; slot++) {
        for (uint32_t word = 0U; word < AUDIO_SLOT_WORDS; word++) {
            assert(test_scsp[slot * AUDIO_SLOT_WORDS + word] == 0U);
        }
    }

    assert(picogb_saturn_audio_submit(NULL, 512U) == 0U);
    assert(picogb_saturn_audio_submit(samples, 0U) == 0U);
    assert(picogb_saturn_audio_submit(samples, 2048U) == 2048U);
    picogb_saturn_audio_status(&status);
    assert(!status.running && status.queued_samples == 2048U);
    assert(picogb_saturn_audio_submit(samples, 2048U) == 2048U);
    picogb_saturn_audio_status(&status);
    assert(status.running && status.queued_samples == 4096U);
    assert_segment(0U, (uint16_t)-12345);
    assert_segment(1U, 0U);
    assert((test_scsp[0] & (AUDIO_KYONB | AUDIO_KYONEX)) ==
        (AUDIO_KYONB | AUDIO_KYONEX));

    assert(picogb_saturn_audio_submit(samples, 4608U) == 4096U);
    assert_segment(0U, (uint16_t)-12345);
    assert_segment(1U, (uint16_t)-12345);
    assert(picogb_saturn_audio_submit(samples, 512U) == 0U);
    picogb_saturn_audio_status(&status);
    assert(status.queued_samples == 8192U && status.underrun_count == 0U);

    change_page(1U);
    assert_segment(0U, 0U);
    assert_segment(1U, (uint16_t)-12345);
    assert(picogb_saturn_audio_submit(samples, 4096U) == 4096U);
    assert_segment(0U, (uint16_t)-12345);
    assert_segment(1U, (uint16_t)-12345);
    change_page(0U);
    assert_segment(0U, (uint16_t)-12345);
    assert_segment(1U, 0U);
    picogb_saturn_audio_status(&status);
    assert(status.underrun_count == 0U && status.queued_samples == 4096U);

    /* Partial next segment: keep its valid prefix, leave starvation silent,
     * and do not overwrite any sample in the now-playing segment.
     */
    assert(picogb_saturn_audio_submit(samples, 512U) == 512U);
    change_page(1U);
    for (uint32_t i = 0U; i < 4096U; i++) {
        assert(test_sound_ram[AUDIO_PCM_RAM_OFFSET / 2U + 4096U + i] ==
            ((i < 512U) ? (uint16_t)-12345 : 0U));
    }
    assert_segment(0U, 0U);
    picogb_saturn_audio_status(&status);
    assert(status.underrun_count == 1U && status.queued_samples == 4096U);
    assert(picogb_saturn_audio_submit(samples, 4096U) == 4096U);
    assert_segment(0U, (uint16_t)-12345);

    /* A long service gap makes the coarse monitor ambiguous. Recovery
     * stops only our PCM slot and never issues SNDOFF or leaves 68K stopped.
     */
    test_ticks += 210U * 100U;
    picogb_saturn_audio_pump();
    picogb_saturn_audio_status(&status);
    assert(!status.running && status.queued_samples == 0U);
    assert(status.underrun_count == 2U && status.recovery_count == 1U);
    assert(test_sound_commands == 2U);
    assert_segment(0U, 0U);
    assert_segment(1U, 0U);
    test_scsp[0x408U / 2U] = 0U;
    assert(picogb_saturn_audio_submit(samples, 4096U) == 4096U);
    picogb_saturn_audio_status(&status);
    assert(status.running);

    /* Unsigned cursor rollover after extended operation. */
    audio.read_floor = 0xFFFFF000U;
    audio.write_cursor = 0U;
    audio.last_page = 1U;
    change_page(0U);
    picogb_saturn_audio_status(&status);
    assert(status.queued_samples == 4096U);
    assert(picogb_saturn_audio_submit(samples, 4096U) == 4096U);
    assert_segment(1U, (uint16_t)-12345);

    /* Reinitialization also covers PAL timeout scaling and old sound state. */
    test_standard = VDP2_TVMD_TV_STANDARD_PAL;
    assert(picogb_saturn_audio_init());
    assert(audio.timeout_ticks == 208U * 100U);
    assert(test_sound_commands == 4U);
    picogb_saturn_audio_status(&status);
    assert(status.accepted_samples == 0U && status.underrun_count == 0U);
    puts("056 SCSP PCM mock: initialization, backpressure, safe segments, silence, recovery, rollover pass");
    return 0;
}
