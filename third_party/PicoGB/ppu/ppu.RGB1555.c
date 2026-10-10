/* knovGBC PicoGB library extension/snapshot; notice updated 2026-10-10.
 * This source extends/adapts the PicoGB library based on upstream commit
 * 325a12a25e2d1f4799424cc18c0e69a7810695b0.
 * The preserved local implementation predates the 2026-10-07 inventory.
 * Exact historical code-edit dates are not established by surviving records.
 * Only this source-handoff notice was changed on 2026-10-10.
 * See third_party/PicoGB/KNOVGBC_CHANGES.md for dated validation evidence;
 * report dates are not claims that every code change occurred on those dates.
 * Distributed with the modified library under GNU LGPL v2.1.
 * See third_party/PicoGB/LICENSE.txt for the library license.
 * Original upstream license and copyright notices remain applicable.
 */
/*
 * Native Sega Saturn VDP2 RGB1555 palette.
 *
 * PicoGB's DMG renderer indexes this table after applying BGP/OBP. The
 * knovGBC configuration enables CONFIG_PPU_INVERT, so keep the traditional
 * dark-to-light order used by PicoGB's RGB565 backend. Bit 15 is added by
 * the VDP2 uploader.
 */
static const pixel_t palette[] PGB_DATA = {
    0x0000U,
    0x294AU,
    0x56B5U,
    0x7FFFU
};
