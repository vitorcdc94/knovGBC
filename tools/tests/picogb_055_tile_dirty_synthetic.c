/* Full-rate indexed-PPU dependency cases for the 055 tile-pattern change.
 * Reuse the 050 fixture so VRAM, OAM and IO writes take the real memory path.
 */
#define main picogb_050_fixture_main
#include "picogb_ppu_regression.c"
#undef main

static uint64_t last_image;

static int report_frame(struct ppu_fixture *fixture, const char *name,
    int must_change)
{
    uint64_t image;

    if (!run_to_vblank(fixture) || !guards_valid(fixture)) {
        fprintf(stderr, "%s: frame failed or corrupted guard\n", name);
        return 0;
    }
    image = framebuffer_hash(fixture);
    if ((must_change > 0 && image == last_image) ||
        (must_change < 0 && image != last_image)) {
        fprintf(stderr, "%s: unexpected visual mutation result\n", name);
        return 0;
    }
    printf("ppu-055 %-20s image=%016llx\n", name,
        (unsigned long long)image);
    last_image = image;
    return 1;
}

static void fill_tile(struct ppu_fixture *fixture, word tile, word low,
    word high)
{
    word row;

    for (row = 0U; row != 8U; row++) {
        const word address = 0x8000U + (tile << 4) + (row << 1);
        write_byte(fixture, address, low);
        write_byte(fixture, address + 1U, high);
    }
}

static int dirty_line_count_matches(const struct ppu_fixture *fixture,
    word candidate_count, const char *name)
{
    word count = 0U;
    word line;

    for (line = 0U; line != VISIBLE_HEIGHT; line++) {
        count += (fixture->ppu.dmg_indexed_dirty_lines[line >> 3] >>
            (line & 7U)) & 1U;
    }
#if defined(KNOVGBC_PPU_TILE_DIRTY_LINES)
    const word expected = candidate_count;
#else
    const word expected = VISIBLE_HEIGHT;
    (void)candidate_count;
#endif
    if (count != expected) {
        fprintf(stderr, "%s: dirty lines %u, expected %u\n", name,
            (unsigned)count, (unsigned)expected);
        return 0;
    }
    return 1;
}

static int test_sprite_dependencies(void)
{
    struct ppu_fixture fixture;

    if (!fixture_init(&fixture)) return 0;
    write_byte(&fixture, 0xFF40U, 0x97U); /* BG plus 8x16 objects, no window. */
    write_byte(&fixture, 0xFF43U, 0U);
    write_byte(&fixture, 0xFF42U, 0U);
    write_byte(&fixture, 0xFF48U, 0xE4U);
    fill_tile(&fixture, 2U, 0xFFU, 0U);
    fill_tile(&fixture, 3U, 0U, 0xFFU);
    write_byte(&fixture, 0xFE00U, 48U);
    write_byte(&fixture, 0xFE01U, 48U);
    write_byte(&fixture, 0xFE02U, 3U); /* Odd tile number must resolve to 2/3. */
    if (!report_frame(&fixture, "obj16-setup", 0)) return 0;

    fill_tile(&fixture, 2U, 0U, 0xFFU);
    if (!dirty_line_count_matches(&fixture, 8U, "obj16-upper-half")) return 0;
    if (!report_frame(&fixture, "obj16-upper-half", 1)) return 0;
    fill_tile(&fixture, 3U, 0xFFU, 0xFFU);
    if (!dirty_line_count_matches(&fixture, 8U, "obj16-lower-half")) return 0;
    if (!report_frame(&fixture, "obj16-lower-half", 1)) return 0;

    write_byte(&fixture, 0xFE03U, 0x60U); /* Horizontal and vertical flip. */
    if (!report_frame(&fixture, "obj16-flips", 1)) return 0;
    fill_tile(&fixture, 2U, 0xFFU, 0U);
    if (!dirty_line_count_matches(&fixture, 8U, "obj16-flipped-half")) return 0;
    if (!report_frame(&fixture, "obj16-flipped-half", 1)) return 0;
    fill_tile(&fixture, 3U, 0U, 0xFFU);
    if (!dirty_line_count_matches(&fixture, 8U, "obj16-flipped-other")) return 0;
    if (!report_frame(&fixture, "obj16-flipped-other", 1)) return 0;

    /* Move the object and change its tile pair, then mutate its new half.
     * This proves that dependency sets are rebuilt after OAM changes. */
    fill_tile(&fixture, 4U, 0xFFU, 0U);
    fill_tile(&fixture, 5U, 0xFFU, 0xFFU);
    write_byte(&fixture, 0xFE00U, 80U);
    write_byte(&fixture, 0xFE01U, 72U);
    write_byte(&fixture, 0xFE02U, 5U);
    write_byte(&fixture, 0xFE03U, 0U);
    if (!report_frame(&fixture, "obj16-new-pair", 1)) return 0;
    fill_tile(&fixture, 5U, 0U, 0xFFU);
    if (!dirty_line_count_matches(&fixture, 8U, "obj16-new-half")) return 0;
    if (!report_frame(&fixture, "obj16-new-half", 1)) return 0;

    /* Physical tile 383 is the final dependency bit. It is unused here. */
    fill_tile(&fixture, 383U, 0xFFU, 0xFFU);
    if (!dirty_line_count_matches(&fixture, 0U, "unused-last-tile")) return 0;
    if (!report_frame(&fixture, "unused-last-tile", -1)) return 0;

    /* A signed BG map byte 127 selects physical tile 383. Only its eight
     * visible source rows should be invalidated; test the final bitmap bit. */
    write_byte(&fixture, 0xFF40U, 0x81U);
    write_byte(&fixture, 0x9843U, 127U);
    if (!report_frame(&fixture, "signed-last-setup", 1)) return 0;
    fill_tile(&fixture, 383U, 0xFFU, 0U);
    if (!dirty_line_count_matches(&fixture, 8U, "signed-last-pattern")) return 0;
    if (!report_frame(&fixture, "signed-last-pattern", 1)) return 0;
    write_byte(&fixture, 0x9843U, 126U);
    if (!report_frame(&fixture, "signed-new-map", 1)) return 0;
    fill_tile(&fixture, 382U, 0U, 0xFFU);
    if (!dirty_line_count_matches(&fixture, 8U, "signed-new-pattern")) return 0;
    if (!report_frame(&fixture, "signed-new-pattern", 1)) return 0;
    return 1;
}

int main(void)
{
    struct ppu_fixture fixture;
    word offset;
    word guard;

    if (!fixture_init(&fixture)) {
        fprintf(stderr, "fixture setup failed\n");
        return 1;
    }
    install_visual_fixture(&fixture);

    /* Pin a used tile in both maps. Its data also belongs to the OAM set. */
    write_byte(&fixture, 0x9800U, 5U);
    write_byte(&fixture, 0x9C00U, 5U);
    if (!report_frame(&fixture, "initial", 0)) return 2;

    fill_tile(&fixture, 5U, 0xFFU, 0x00U);
    if (!report_frame(&fixture, "tile-bg-window", 1)) return 3;
    if (!report_frame(&fixture, "idle", -1)) return 4;

    write_byte(&fixture, 0x9800U, 6U);
    if (!report_frame(&fixture, "bg-map", 1)) return 5;
    write_byte(&fixture, 0x9C00U, 6U);
    if (!report_frame(&fixture, "window-map", 1)) return 6;

    write_byte(&fixture, 0xFF43U, 4U);
    if (!report_frame(&fixture, "scroll-x", 1)) return 7;
    write_byte(&fixture, 0xFF42U, 6U);
    if (!report_frame(&fixture, "scroll-y", 1)) return 8;
    write_byte(&fixture, 0xFF4BU, 71U);
    if (!report_frame(&fixture, "window-x", 1)) return 9;
    write_byte(&fixture, 0xFF4AU, 41U);
    if (!report_frame(&fixture, "window-y", 1)) return 10;

    /* A uniform map isolates the object tile from BG/window uses. */
    for (offset = 0U; offset != 0x400U; offset++) {
        write_byte(&fixture, 0x9800U + offset, 0U);
        write_byte(&fixture, 0x9C00U + offset, 0U);
    }
    write_byte(&fixture, 0xFF40U, 0x93U);
    if (!report_frame(&fixture, "sprite-setup", 1)) return 11;
    fill_tile(&fixture, 6U, 0xFFU, 0xFFU);
    if (!report_frame(&fixture, "sprite-tile", 1)) return 12;

    /* Signed addressing maps tile number zero to physical tile 256. */
    write_byte(&fixture, 0xFF40U, 0x83U);
    /* Switching the addressing mode need not change the fixture's pixels;
     * the following pattern mutation is the actual visibility assertion. */
    if (!report_frame(&fixture, "signed-setup", 0)) return 13;
    fill_tile(&fixture, 256U, 0x00U, 0xFFU);
    if (!report_frame(&fixture, "signed-tile", 1)) return 14;

    /* A tile write after line 72 needs to repaint earlier lines next frame. */
    for (guard = 0U; guard != 400U && fixture.ppu.state.scanY != 72U;
         guard++) {
        (void)ppu_tick(&fixture.ppu, 456U);
    }
    if (guard == 400U) {
        fprintf(stderr, "mid-frame scanline was not reached\n");
        return 15;
    }
    fill_tile(&fixture, 256U, 0xFFU, 0xFFU);
    if (!report_frame(&fixture, "mid-frame-now", 0)) return 16;
    if (!report_frame(&fixture, "mid-frame-next", 1)) return 17;
    if (!report_frame(&fixture, "final-idle", -1)) return 18;
    if (!test_sprite_dependencies()) return 19;

    return 0;
}
