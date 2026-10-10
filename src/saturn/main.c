#include "platform.h"
#include "saturn_heap.h"

#include "gb.h"

#include <stddef.h>
#include <stdint.h>

/* SameBoy always writes 32-bit host pixels. Convert once per completed frame. */
static uint32_t core_frame_buffer[KNOVGB_WIDTH * KNOVGB_HEIGHT];
static uint16_t saturn_frame_buffer[KNOVGB_WIDTH * KNOVGB_HEIGHT];

/*
 * Audio output is not wired to the Saturn sound hardware yet. Keep a harmless
 * sink installed for the future backend; the basic build deliberately leaves
 * SameBoy's sample rate at zero, so no samples are currently generated.
 */
static void
discard_audio(GB_gameboy_t *gb, GB_sample_t *sample)
{
    (void)gb;
    (void)sample;
}

static uint32_t
rgb1555(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{
    (void)gb;
    /* SameBoy's output is 0RGB1555, which is directly suitable for VDP2. */
    /* Saturn RGB1555 stores red in bits 0..4 and blue in bits 10..14. */
    return ((uint32_t)(b >> 3) << 10) | ((uint32_t)(g >> 3) << 5) | (r >> 3);
}

extern const uint8_t knovgbc_dmg_boot_rom[];
extern const uint8_t knovgbc_dmg_boot_rom_end[];
extern const uint8_t knovgbc_cgb_boot_rom[];
extern const uint8_t knovgbc_cgb_boot_rom_end[];

static void
load_boot_rom(GB_gameboy_t *gb, GB_boot_rom_t type)
{
    const uint8_t *start = knovgbc_dmg_boot_rom;
    const uint8_t *end = knovgbc_dmg_boot_rom_end;

    switch (type) {
        case GB_BOOT_ROM_CGB_0:
        case GB_BOOT_ROM_CGB:
        case GB_BOOT_ROM_CGB_E:
        case GB_BOOT_ROM_AGB_0:
        case GB_BOOT_ROM_AGB:
            start = knovgbc_cgb_boot_rom;
            end = knovgbc_cgb_boot_rom_end;
            break;
        default:
            break;
    }

    GB_load_boot_rom_from_buffer(gb, start, (size_t)(end - start));
}

static GB_model_t
model_for_rom(const uint8_t *rom, uint32_t rom_size)
{
    /* Header bit 7 selects a Color-capable cartridge. Flag 0x80 is backward
     * compatible and 0xC0 is Color-only; both should boot as CGB-E. */
    if ((rom_size > 0x0143U) && ((rom[0x0143U] & 0x80U) != 0U)) {
        return GB_MODEL_CGB_E;
    }
    return GB_MODEL_DMG_B;
}

#ifdef KNOVGBC_BOOT_DIAGNOSTICS
#define RGB1555_RED     0x001FU
#define RGB1555_BLUE    0x7C00U
#define RGB1555_YELLOW  0x03FFU
#define RGB1555_MAGENTA 0x7C1FU
#define RGB1555_CYAN    0x7FE0U
#define RGB1555_WHITE   0x7FFFU

static void
show_startup_marker(uint16_t colour, uint32_t bars)
{
    for (uint32_t y = 0; y < KNOVGB_HEIGHT; y++) {
        for (uint32_t x = 0; x < KNOVGB_WIDTH; x++) {
            uint16_t pixel = colour;
            if ((x < 2U) || (x >= KNOVGB_WIDTH - 2U) ||
                (y < 2U) || (y >= KNOVGB_HEIGHT - 2U)) {
                pixel = RGB1555_WHITE;
            }
            for (uint32_t bar = 0; bar < bars; bar++) {
                const uint32_t left = 8U + (bar * 12U);
                if ((x >= left) && (x < left + 6U) &&
                    (y >= 8U) && (y < 28U)) {
                    pixel = RGB1555_WHITE;
                }
            }
            saturn_frame_buffer[(y * KNOVGB_WIDTH) + x] = pixel;
        }
    }
    saturn_platform_present(saturn_frame_buffer);
}

static void
startup_failure(uint32_t stage)
{
    show_startup_marker(RGB1555_RED, stage);
    for (;;) {
        __asm__ volatile("nop");
    }
}
#else
#define show_startup_marker(colour, bars) ((void)0)
#endif

int
main(void)
{
    GB_gameboy_t *gb;
    uint8_t *rom = NULL;
    uint32_t rom_size = 0;
    char title[32] = {0};

    saturn_platform_video_init();
    show_startup_marker(RGB1555_BLUE, 1U);

    if (!saturn_heap_self_test()) {
#ifdef KNOVGBC_BOOT_DIAGNOSTICS
        startup_failure(1U);
#else
        return 1;
#endif
    }

    if (!saturn_platform_services_init()) {
#ifdef KNOVGBC_BOOT_DIAGNOSTICS
        startup_failure(2U);
#else
        return 1;
#endif
    }

    show_startup_marker(RGB1555_YELLOW, 2U);
    if (!saturn_platform_load_selected_rom(&rom, &rom_size, title, sizeof(title))) {
#ifdef KNOVGBC_BOOT_DIAGNOSTICS
        startup_failure(3U);
#else
        return 1;
#endif
    }

    show_startup_marker(RGB1555_MAGENTA, 3U);
    gb = GB_init(GB_alloc(), model_for_rom(rom, rom_size));
    if (gb == NULL) {
#ifdef KNOVGBC_BOOT_DIAGNOSTICS
        startup_failure(4U);
#else
        return 1;
#endif
    }

    GB_set_boot_rom_load_callback(gb, load_boot_rom);
    GB_set_rgb_encode_callback(gb, rgb1555);
    GB_set_pixels_output(gb, core_frame_buffer);
    GB_apu_set_sample_callback(gb, discard_audio);
    /* The platform owns this L-WRAM buffer for the program's entire lifetime. */
    GB_load_rom_from_buffer_borrowed(gb, rom, rom_size);
    show_startup_marker(RGB1555_CYAN, 4U);

    for (;;) {
        saturn_platform_update_input(gb);
        GB_run_frame(gb);
        for (uint32_t i = 0; i < KNOVGB_WIDTH * KNOVGB_HEIGHT; i++) {
            saturn_frame_buffer[i] = (uint16_t)core_frame_buffer[i];
        }
        if (!saturn_platform_fast_forward()) {
            saturn_platform_wait_vblank();
        }
        saturn_platform_present(saturn_frame_buffer);
    }
}
