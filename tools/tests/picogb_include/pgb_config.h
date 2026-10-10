#pragma once

/* Minimal DMG configuration used by the isolated host smoke test. */
#define CONFIG_NOBOOTMEME
#define CONFIG_USE_FLAT_ROM

/* One RGB565 pixel per framebuffer entry; no interlacing or audio. */
#define PPU_MODE 6
#define PPU_INTERLACE 0

