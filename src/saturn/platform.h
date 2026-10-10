#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The platform public interface needs only a pointer to SameBoy's opaque
 * instance. Keeping the full core header out of this header lets standalone
 * video probes stay independent of the emulator implementation. */
typedef struct GB_gameboy_s GB_gameboy_t;

#define KNOVGB_WIDTH 160
#define KNOVGB_HEIGHT 144

/* Configure and enable the VDP2 bitmap output only. This does not access the
 * pad, CD block, or SameBoy, so hardware probes can validate video in
 * isolation. */
void saturn_platform_video_init(void);

/* Initialise VDP2, SMPC and the CD block. */
void saturn_platform_init(void);

/* Initialise controller polling and the CD filesystem after video is already
 * visible. Keeping this separate lets the hardware build leave a useful
 * marker on screen if the Saroo/CD path does not return. */
bool saturn_platform_services_init(void);

/* Upload a 160x144 RGB1555 image to the current VDP2 bitmap buffer. */
void saturn_platform_present(const uint16_t *pixels);

/* Upload the same image from a framebuffer whose rows may contain padding.
 * This lets lightweight emulator cores render directly into their preferred
 * scanline layout without an intermediate 160x144 conversion buffer. */
void saturn_platform_present_strided(const uint16_t *pixels,
    uint32_t stride_pixels);

/* Experimental 8-bit indexed VDP2 bitmap presenter (kmvpe047 only). */
#if defined(KNOVGBC_VDP2_INDEXED8)
void saturn_platform_present_indexed8_strided(const uint8_t *pixels,
    uint32_t stride_pixels);
#endif

/* Draw the MVP performance readout outside the Game Boy image. The baseline
 * uses the area above it; the zoom build stacks fields in the left bar. */
void saturn_platform_hud_draw(const char *line_1, const char *line_2,
    const char *line_3);

/* Map port 1's digital pad to SameBoy's eight buttons. */
void saturn_platform_update_input(GB_gameboy_t *gb);

/* Wait for the host display's vblank. */
void saturn_platform_wait_vblank(void);

/* Requests a sustained fast-forward state through the Saturn R button. */
bool saturn_platform_fast_forward(void);

/*
 * Read the first ISO9660 ROM into the dedicated 1 MiB Low Work RAM buffer.
 * The implementation rejects ROMs that exceed that buffer before calling
 * SameBoy.
 */
bool saturn_platform_load_selected_rom(uint8_t **rom, uint32_t *size,
    char *title, uint32_t title_size);
