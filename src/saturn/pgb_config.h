#pragma once

/*
 * PicoGB configuration for the first Saturn MVP.
 *
 * Keep this deliberately narrower than a general Game Boy emulator: one
 * embedded DMG ROM and no boot ROM. Audio is opt-in only for 056. The established build
 * renders every other frame; the isolated 60-visual-FPS build renders all.
 * config.h turns every option not defined here
 * into zero, which lets the linker discard the CGB, APU and mapper paths.
 */
#define NDEBUG 1
#define NO_ASSERT 1

#define CONFIG_NOBOOTMEME 1
#define CONFIG_PPU_INVERT 1
#define CONFIG_USE_FLAT_ROM 1

/* 056 only: synthesize the four DMG channels into a fixed-rate mono PCM
 * stream. The preserved 055 and earlier targets do not compile the APU. */
#if defined(KNOVGBC_AUDIO)
#define CONFIG_APU_ENABLE 1
#define CONFIG_APU_MONO 1
#define CONFIG_APU_N_PER_TICK 32
#endif

/* The indexed experiment retains the same four resolved DMG shades, but
 * stores one VDP2 palette index per pixel instead of a 16-bit colour. */
#if defined(KNOVGBC_VDP2_INDEXED8)
#define PPU_MODE 8
#else
/* Render the four DMG shades directly in the VDP2 RGB1555 layout. This
 * removes both the 32-bit SameBoy framebuffer and a per-pixel conversion. */
#define PPU_MODE 7
#endif

/* Mode 1 renders a complete frame, then suppresses all scanlines of the next
 * frame. Mode 0 draws each frame; CPU and device timing stay at 59.73 Hz. */
#if defined(KNOVGBC_NO_FRAMESKIP)
#define PPU_INTERLACE 0
#else
#define PPU_INTERLACE 1
#endif

/* types.h detects the compiler target byte order. The runtime also verifies
 * the register aliases before executing the first Game Boy instruction. */
