#include "mvp_profiler.h"
#include "platform.h"
#include "saturn_heap.h"

#include "gb.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* SameBoy emits 32-bit host pixels. Only presented frames are converted to
 * the Saturn's native RGB1555 buffer; skipped frames pay neither cost. */
static uint32_t core_frame_buffer[KNOVGB_WIDTH * KNOVGB_HEIGHT];
static uint16_t saturn_frame_buffer[KNOVGB_WIDTH * KNOVGB_HEIGHT];

extern uint8_t knovgbc_mvp_rom[];
extern uint8_t knovgbc_mvp_rom_end[];
extern const uint8_t knovgbc_mvp_dmg_boot_rom[];
extern const uint8_t knovgbc_mvp_dmg_boot_rom_end[];

#define RGB1555_RED     0x001FU
#define RGB1555_BLUE    0x7C00U
#define RGB1555_YELLOW  0x03FFU
#define RGB1555_MAGENTA 0x7C1FU
#define RGB1555_CYAN    0x7FE0U
#define RGB1555_WHITE   0x7FFFU

static uint32_t
rgb1555(GB_gameboy_t *gb, uint8_t red, uint8_t green, uint8_t blue)
{
    (void)gb;
    return ((uint32_t)(blue >> 3) << 10) |
        ((uint32_t)(green >> 3) << 5) | (red >> 3);
}

static void
load_dmg_boot_rom(GB_gameboy_t *gb, GB_boot_rom_t type)
{
    (void)type;
    GB_load_boot_rom_from_buffer(gb, knovgbc_mvp_dmg_boot_rom,
        (size_t)(knovgbc_mvp_dmg_boot_rom_end -
        knovgbc_mvp_dmg_boot_rom));
}

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

static void __attribute__((noreturn))
startup_failure(uint32_t stage)
{
    show_startup_marker(RGB1555_RED, stage);
    for (;;) {
        __asm__ volatile("nop");
    }
}

static bool
embedded_rom_is_supported(void)
{
    const size_t size = (size_t)(knovgbc_mvp_rom_end - knovgbc_mvp_rom);

    /* This first MVP deliberately accepts one exact class: a 32 KiB DMG
     * ROM-only cartridge. Tetris has CGB flag 0 and cartridge type 0. */
    return (size == 0x8000U) &&
        ((knovgbc_mvp_rom[0x0143U] & 0x80U) == 0U) &&
        (knovgbc_mvp_rom[0x0147U] == 0U);
}

int
main(void)
{
    saturn_platform_video_init();
    show_startup_marker(RGB1555_BLUE, 1U);

    if (!saturn_heap_self_test()) {
        startup_failure(1U);
    }

    if (!saturn_platform_services_init()) {
        startup_failure(2U);
    }

    show_startup_marker(RGB1555_YELLOW, 2U);
    if (!embedded_rom_is_supported()) {
        startup_failure(3U);
    }

    show_startup_marker(RGB1555_MAGENTA, 3U);
    GB_gameboy_t *const gb = GB_init(GB_alloc(), GB_MODEL_DMG_B);
    if (gb == NULL) {
        startup_failure(4U);
    }

    GB_set_boot_rom_load_callback(gb, load_dmg_boot_rom);
    GB_set_rgb_encode_callback(gb, rgb1555);
    GB_set_pixels_output(gb, core_frame_buffer);

    /* Tetris is exactly one 32 KiB bank pair, so SameBoy's borrowed-buffer
     * padding is zero bytes. The writable embedded section remains valid for
     * the emulator's entire lifetime. */
    GB_load_rom_from_buffer_borrowed(gb, knovgbc_mvp_rom,
        (size_t)(knovgbc_mvp_rom_end - knovgbc_mvp_rom));
    show_startup_marker(RGB1555_CYAN, 4U);

    /* Align the independent FRT schedule with a real display boundary once.
     * Later frames never blindly wait for another VBlank after missing one. */
    saturn_platform_wait_vblank();
    mvp_profiler_init();

    uint32_t logical_frame = 0;
    for (;;) {
        const uint32_t frame_start = mvp_profiler_ticks_get();
        const bool presented = (logical_frame & 1U) == 0U;

        saturn_platform_update_input(gb);

        /* SameBoy keeps CPU, PPU timing, interrupts and input running while
         * this flag only suppresses pixel generation. This is the useful
         * half of frameskip 1, not merely dropping an already-rendered image. */
        GB_set_rendering_disabled(gb, !presented);

        const uint32_t core_start = mvp_profiler_ticks_get();
        GB_run_frame(gb);
        const uint32_t core_end = mvp_profiler_ticks_get();

        /* Frameskip 1: emulate every frame but convert and present only even
         * logical frames. Input and Game Boy timing therefore remain 59.73 Hz. */
        uint32_t conversion_ticks = 0;
        uint32_t upload_ticks = 0;

        if (presented) {
            const uint32_t conversion_start = mvp_profiler_ticks_get();
            for (uint32_t i = 0;
                 i < KNOVGB_WIDTH * KNOVGB_HEIGHT; i++) {
                saturn_frame_buffer[i] = (uint16_t)core_frame_buffer[i];
            }
            conversion_ticks =
                mvp_profiler_ticks_get() - conversion_start;
        }

        const uint32_t wait_ticks = mvp_profiler_frame_pace();

        if (presented) {
            const uint32_t upload_start = mvp_profiler_ticks_get();
            saturn_platform_present(saturn_frame_buffer);
            upload_ticks = mvp_profiler_ticks_get() - upload_start;
        }

        const uint32_t frame_end = mvp_profiler_ticks_get();
        mvp_profiler_frame_record(frame_end - frame_start,
            core_end - core_start, conversion_ticks, wait_ticks,
            upload_ticks, presented);

        logical_frame++;
    }
}
