/*
 * Master-only VDP2 bitmap-path probe.
 *
 * This intentionally uses the emulator's real VDP2 setup and 160x144
 * presentation routine, but omits ROM loading, controls, CD access, audio,
 * and SameBoy. A stable test card proves the framebuffer format, centering,
 * destination stride, and VDP2 VBlank synchronisation independently.
 */

#include "platform.h"

#include <stdint.h>

static uint32_t core_frame_buffer[KNOVGB_WIDTH * KNOVGB_HEIGHT];
static uint16_t saturn_frame_buffer[KNOVGB_WIDTH * KNOVGB_HEIGHT];

#define RGB1555_RED     0x001FU
#define RGB1555_GREEN   0x03E0U
#define RGB1555_BLUE    0x7C00U
#define RGB1555_YELLOW  0x03FFU
#define RGB1555_MAGENTA 0x7C1FU
#define RGB1555_WHITE   0x7FFFU
#define RGB1555_DARK    0x1084U
#define RGB1555_LIGHT   0x4210U

static uint16_t
test_pixel(uint32_t x, uint32_t y)
{
    /* A coloured two-pixel border makes orientation immediately visible. */
    if (y < 2U) {
        return RGB1555_RED;
    }
    if (x >= KNOVGB_WIDTH - 2U) {
        return RGB1555_GREEN;
    }
    if (y >= KNOVGB_HEIGHT - 2U) {
        return RGB1555_BLUE;
    }
    if (x < 2U) {
        return RGB1555_MAGENTA;
    }

    /* The four corners confirm RGB1555 colour order. */
    if ((x < 24U) && (y < 24U)) {
        return RGB1555_RED;
    }
    if ((x >= KNOVGB_WIDTH - 24U) && (y < 24U)) {
        return RGB1555_GREEN;
    }
    if ((x < 24U) && (y >= KNOVGB_HEIGHT - 24U)) {
        return RGB1555_BLUE;
    }
    if ((x >= KNOVGB_WIDTH - 24U) && (y >= KNOVGB_HEIGHT - 24U)) {
        return RGB1555_WHITE;
    }

    /* Checkerboard, centre cross, and diagonals expose a bad row stride or
     * source-to-destination copy immediately. */
    if ((x == (KNOVGB_WIDTH / 2U)) || (y == (KNOVGB_HEIGHT / 2U)) ||
        (x == y) || (x + y == KNOVGB_WIDTH - 1U)) {
        return RGB1555_YELLOW;
    }

    return (((x >> 3U) + (y >> 3U)) & 1U) ? RGB1555_LIGHT : RGB1555_DARK;
}

int
main(void)
{
    for (uint32_t y = 0; y < KNOVGB_HEIGHT; y++) {
        for (uint32_t x = 0; x < KNOVGB_WIDTH; x++) {
            const uint32_t index = (y * KNOVGB_WIDTH) + x;

            /* Mirror the normal emulator's 32-bit SameBoy-to-RGB1555
             * conversion before calling its ordinary presentation API. */
            core_frame_buffer[index] = test_pixel(x, y);
            saturn_frame_buffer[index] = (uint16_t)core_frame_buffer[index];
        }
    }

    saturn_platform_video_init();
    saturn_platform_present(saturn_frame_buffer);

    for (;;) {
        __asm__ volatile("nop");
    }
}
