#include "platform_video_vdp12.h"

#include <yaul.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* The Saturn display remains 320x224. The Game Boy picture is not scaled. */
#define GAME_X 80
#define GAME_Y 40
#define GAME_RIGHT (GAME_X + 159)
#define GAME_BOTTOM (GAME_Y + 143)

/* One VDP2 page is 64x64 8x8 characters in one-word PND mode (8 KiB).
 * NBG0's visible 20x18 characters start at page cell (10,5). The background
 * PND, transparent HUD PND and character data occupy separate VRAM regions. */
#define MAP_CELLS 64U
#define MAP_WORDS (MAP_CELLS * MAP_CELLS)
#define BG_MAP_ADDR VDP2_VRAM_ADDR(VDP2_VRAM_BANK_A0, 0x0000U)
#define HUD_MAP_ADDR VDP2_VRAM_ADDR(VDP2_VRAM_BANK_A0, 0x2000U)
#define CHAR_ADDR VDP2_VRAM_ADDR(VDP2_VRAM_BANK_B0, 0x0000U)
#define HUD_CHAR_ADDR VDP2_VRAM_ADDR(VDP2_VRAM_BANK_B0, 0x4000U)
#define HUD_ATLAS_CHARS 64U /* ASCII 32..95 */
#define HUD_MAP_X 1U
#define HUD_MAP_WIDTH 28U

#define CMDT_SPRITE_FIRST 3U
#define CMDT_COUNT (PICOGB_VDP12_MAX_SPRITE_STRIPS + 4U)
#define STRIP_TEXTURE_BYTES 8U

_Static_assert((PICOGB_VDP12_TILE_COUNT + 1U) *
    PICOGB_VDP12_TILE_BYTES <= 0x4000U,
    "Background characters overlap HUD characters in VDP2 VRAM");
_Static_assert((0x4000U + HUD_ATLAS_CHARS * 32U) <= VDP2_VRAM_BSIZE_4,
    "HUD characters exceed VDP2 VRAM bank B0");
_Static_assert(CMDT_COUNT <= VDP1_VRAM_DEFAULT_CMDT_COUNT,
    "VDP1 command list exceeds the default command partition");
_Static_assert(PICOGB_VDP12_MAX_SPRITE_STRIPS * STRIP_TEXTURE_BYTES <=
    VDP1_VRAM_DEFAULT_TEXTURE_SIZE,
    "VDP1 sprite strips exceed the default texture partition");

static vdp1_cmdt_t sprite_cmdts[CMDT_COUNT] __attribute__((aligned(32)));
static volatile uint16_t *sprite_textures;
static bool initialized;
static bool sprite_frame_pending;

static const uint8_t *
glyph_rows(char character)
{
    static const uint8_t blank[5] = {0, 0, 0, 0, 0};
    static const uint8_t digits[10][5] = {
        {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7},
        {5,5,7,1,1}, {7,4,7,1,7}, {7,4,7,5,7}, {7,1,2,2,2},
        {7,5,7,5,7}, {7,5,7,1,7}
    };
    static const uint8_t letters[26][5] = {
        {2,5,7,5,5}, {6,5,6,5,6}, {7,4,4,4,7}, {6,5,5,5,6},
        {7,4,6,4,7}, {7,4,6,4,4}, {7,4,5,5,7}, {5,5,7,5,5},
        {7,2,2,2,7}, {1,1,1,5,2}, {5,5,6,5,5}, {4,4,4,4,7},
        {5,7,7,5,5}, {5,7,7,7,5}, {7,5,5,5,7}, {6,5,6,4,4},
        {7,5,5,7,3}, {6,5,6,5,5}, {7,4,7,1,7}, {7,2,2,2,2},
        {5,5,5,5,7}, {5,5,5,5,2}, {5,5,5,7,5}, {5,5,2,5,5},
        {5,5,2,2,2}, {7,1,2,4,7}
    };
    static const uint8_t dot[5] = {0,0,0,0,2};
    static const uint8_t colon[5] = {0,2,0,2,0};
    static const uint8_t dash[5] = {0,0,7,0,0};

    if ((character >= '0') && (character <= '9'))
        return digits[(uint32_t)(character - '0')];
    if ((character >= 'A') && (character <= 'Z'))
        return letters[(uint32_t)(character - 'A')];
    switch (character) {
        case '.': return dot;
        case ':': return colon;
        case '-': return dash;
        default: return blank;
    }
}

static void
hud_atlas_init(void)
{
    volatile uint16_t *const characters =
        (volatile uint16_t *)(uintptr_t)HUD_CHAR_ADDR;

    /* A character is 8x8 at 4 bpp: two 16-bit words per row. Keep pixel 0
     * transparent so NBG1 adds text without obscuring the Game Boy image. */
    for (uint32_t character = 0; character < HUD_ATLAS_CHARS; character++) {
        const uint8_t *const rows = glyph_rows((char)(character + 32U));
        for (uint32_t y = 0; y < 8U; y++) {
            uint16_t left = 0U;
            uint16_t right = 0U;
            if ((y >= 1U) && (y <= 5U)) {
                const uint8_t bits = rows[y - 1U];
                /* Pixels x=2,3,4 occupy word 0's two low nibbles and
                 * word 1's high nibble; shade 5 is HUD white. */
                if (bits & 4U) left |= 0x0050U;
                if (bits & 2U) left |= 0x0005U;
                if (bits & 1U) right |= 0x5000U;
            }
            characters[(character * 16U) + (y * 2U)] = left;
            characters[(character * 16U) + (y * 2U) + 1U] = right;
        }
    }
}

static void
maps_init(void)
{
    volatile uint16_t *const bg_map =
        (volatile uint16_t *)(uintptr_t)BG_MAP_ADDR;
    volatile uint16_t *const hud_map =
        (volatile uint16_t *)(uintptr_t)HUD_MAP_ADDR;
    volatile uint16_t *const blank_character =
        (volatile uint16_t *)(uintptr_t)CHAR_ADDR;

    for (uint32_t i = 0; i < MAP_WORDS; i++) {
        bg_map[i] = 0U;
        hud_map[i] = 0U;
    }
    for (uint32_t i = 0; i < 16U; i++) blank_character[i] = 0U;

    /* A unique VDP2 character per screen tile retains the exact pixels
     * captured at each Game Boy scanline, including mid-frame palette and
     * tile-map changes. The page's remaining cells select blank character 0. */
    for (uint32_t y = 0; y < PICOGB_VDP12_TILE_ROWS; y++) {
        for (uint32_t x = 0; x < PICOGB_VDP12_TILE_COLS; x++) {
            const uint32_t tile = y * PICOGB_VDP12_TILE_COLS + x;
            bg_map[(y + (GAME_Y / 8)) * MAP_CELLS + x + (GAME_X / 8)] =
                VDP2_SCRN_PND_CONFIG_0(0,
                    CHAR_ADDR + ((tile + 1U) * PICOGB_VDP12_TILE_BYTES),
                    VDP2_CRAM_ADDR(0), 0, 0);
        }
    }
}

bool
saturn_vdp12_init(void)
{
    if (initialized) return true;

    vdp1_vram_partitions_t partitions;
    vdp1_vram_partitions_get(&partitions);
    if ((partitions.cmdt_size < CMDT_COUNT * sizeof(vdp1_cmdt_t)) ||
        (partitions.texture_size <
         PICOGB_VDP12_MAX_SPRITE_STRIPS * STRIP_TEXTURE_BYTES)) {
        return false;
    }
    sprite_textures = (volatile uint16_t *)partitions.texture_base;

    /* One PND and one character fetch for each normal background leave the
     * other VDP2 VRAM cycles available to the CPU during active display. */
    vdp2_vram_cycp_t cycles;
    for (uint32_t bank = 0; bank < 4U; bank++) cycles.pt[bank].raw = 0xFFFFFFFFU;
    cycles.pt[VDP2_VRAM_BANK_A0].t0 = VDP2_VRAM_CYCP_PNDR_NBG0;
    cycles.pt[VDP2_VRAM_BANK_A0].t1 = VDP2_VRAM_CYCP_PNDR_NBG1;
    cycles.pt[VDP2_VRAM_BANK_B0].t0 = VDP2_VRAM_CYCP_CHPNDR_NBG0;
    cycles.pt[VDP2_VRAM_BANK_B0].t1 = VDP2_VRAM_CYCP_CHPNDR_NBG1;
    for (uint32_t bank = 0; bank < 4U; bank++) {
        if ((bank != VDP2_VRAM_BANK_A0) && (bank != VDP2_VRAM_BANK_B0))
            continue;
        cycles.pt[bank].t2 = VDP2_VRAM_CYCP_CPU_RW;
        cycles.pt[bank].t3 = VDP2_VRAM_CYCP_CPU_RW;
        cycles.pt[bank].t4 = VDP2_VRAM_CYCP_CPU_RW;
        cycles.pt[bank].t5 = VDP2_VRAM_CYCP_CPU_RW;
        cycles.pt[bank].t6 = VDP2_VRAM_CYCP_CPU_RW;
        cycles.pt[bank].t7 = VDP2_VRAM_CYCP_CPU_RW;
    }
    const vdp2_vram_usage_t usage = {
        .a0 = VDP2_VRAM_USAGE_TYPE_PND,
        .a1 = VDP2_VRAM_USAGE_TYPE_NONE,
        .b0 = VDP2_VRAM_USAGE_TYPE_CPD_BPD,
        .b1 = VDP2_VRAM_USAGE_TYPE_NONE
    };
    const vdp2_scrn_normal_map_t bg_map = {
        .base_addr = {BG_MAP_ADDR, BG_MAP_ADDR, BG_MAP_ADDR, BG_MAP_ADDR}
    };
    const vdp2_scrn_normal_map_t hud_map = {
        .base_addr = {HUD_MAP_ADDR, HUD_MAP_ADDR, HUD_MAP_ADDR, HUD_MAP_ADDR}
    };
    const vdp2_scrn_cell_format_t bg_format = {
        .scroll_screen = VDP2_SCRN_NBG0,
        .ccc = VDP2_SCRN_CCC_PALETTE_16,
        .char_size = VDP2_SCRN_CHAR_SIZE_1X1,
        .pnd_size = 1,
        .aux_mode = VDP2_SCRN_AUX_MODE_0,
        .plane_size = VDP2_SCRN_PLANE_SIZE_1X1,
        .cpd_base = CHAR_ADDR,
        .palette_base = VDP2_CRAM_ADDR(0)
    };
    const vdp2_scrn_cell_format_t hud_format = {
        .scroll_screen = VDP2_SCRN_NBG1,
        .ccc = VDP2_SCRN_CCC_PALETTE_16,
        .char_size = VDP2_SCRN_CHAR_SIZE_1X1,
        .pnd_size = 1,
        .aux_mode = VDP2_SCRN_AUX_MODE_0,
        .plane_size = VDP2_SCRN_PLANE_SIZE_1X1,
        .cpd_base = HUD_CHAR_ADDR,
        .palette_base = VDP2_CRAM_ADDR(0)
    };

    vdp2_tvmd_display_clear();
    vdp2_cram_mode_set(0);
    vdp2_vram_usage_set(&usage);
    vdp2_vram_cycp_set(&cycles);
    vdp2_scrn_cell_format_set(&bg_format, &bg_map);
    vdp2_scrn_cell_format_set(&hud_format, &hud_map);
    vdp2_scrn_priority_set(VDP2_SCRN_NBG0, 4);
    vdp2_scrn_priority_set(VDP2_SCRN_NBG1, 7);
    vdp2_sprite_priority_set(0, 6);
    vdp2_scrn_scroll_x_set(VDP2_SCRN_NBG0, FIX16(0.0));
    vdp2_scrn_scroll_y_set(VDP2_SCRN_NBG0, FIX16(0.0));
    vdp2_scrn_scroll_x_set(VDP2_SCRN_NBG1, FIX16(0.0));
    vdp2_scrn_scroll_y_set(VDP2_SCRN_NBG1, FIX16(0.0));
    vdp2_sync();
    vdp2_sync_wait();

    volatile uint16_t *const cram =
        (volatile uint16_t *)(uintptr_t)VDP2_CRAM_ADDR(0);
    cram[0] = 0x8000U;
    cram[1] = 0x8000U;
    cram[2] = 0xA94AU;
    cram[3] = 0xD6B5U;
    cram[4] = 0xFFFFU;
    cram[5] = 0xFFFFU;
    maps_init();
    hud_atlas_init();

    vdp1_env_t sprite_env;
    vdp1_env_default_init(&sprite_env);
    sprite_env.erase_color.raw = 0U; /* Transparent VDP1 framebuffer. */
    sprite_env.bpp = VDP1_ENV_BPP_16;
    sprite_env.sprite_type = 0;
    vdp1_env_set(&sprite_env);
    vdp1_sync_mode_set(VDP1_SYNC_MODE_ERASE_CHANGE);

    /* Leave HUD index 0 transparent; NBG0's index 0 stays opaque so the
     * border outside 160x144 has the background shade. */
    vdp2_scrn_display_set((vdp2_scrn_disp_t)
        (VDP2_SCRN_DISP_NBG0 | VDP2_SCRN_DISPTP_NBG1));
    vdp2_tvmd_display_set();
    vdp2_sync();
    vdp2_sync_wait();
    sprite_frame_pending = false;
    initialized = true;
    return true;
}

void
saturn_vdp12_present(const struct picogb_vdp12_frame *frame)
{
    if (!initialized || (frame == NULL) || (frame->valid == 0U)) return;

    /* Do not replace textures or command data until the previous VDP1 frame
     * has finished drawing and changed framebuffers. */
    if (sprite_frame_pending) {
        vdp1_sync_wait();
        sprite_frame_pending = false;
    }

    volatile uint32_t *const destination =
        (volatile uint32_t *)(uintptr_t)(CHAR_ADDR + PICOGB_VDP12_TILE_BYTES);
    const uint8_t *const source = &frame->bg_tiles[0][0];
    for (uint32_t word = 0;
         word < (PICOGB_VDP12_TILE_COUNT * PICOGB_VDP12_TILE_BYTES / 4U);
         word++) {
        const uint32_t i = word * 4U;
        destination[word] = ((uint32_t)source[i] << 24) |
            ((uint32_t)source[i + 1U] << 16) |
            ((uint32_t)source[i + 2U] << 8) | source[i + 3U];
    }

    const uint16_t count = frame->sprite_count >
        PICOGB_VDP12_MAX_SPRITE_STRIPS ?
        PICOGB_VDP12_MAX_SPRITE_STRIPS : frame->sprite_count;
    const vdp1_env_coords_t clip = {
        .system_clip = {319, 223},
        .user_clip_ul = {GAME_X, GAME_Y},
        .user_clip_lr = {GAME_RIGHT, GAME_BOTTOM},
        .local = {0, 0}
    };
    memset(sprite_cmdts, 0, (count + 4U) * sizeof(sprite_cmdts[0]));
    vdp1_env_preamble_populate(sprite_cmdts, &clip);
    const uintptr_t texture_base = (uintptr_t)sprite_textures;

    for (uint16_t index = 0; index < count; index++) {
        const picogb_vdp12_sprite_strip_t *const strip =
            &frame->sprite_strips[index];
        const uintptr_t texture = texture_base +
            ((uintptr_t)index * STRIP_TEXTURE_BYTES);
        volatile uint16_t *const tex = (volatile uint16_t *)texture;
        for (uint32_t pair = 0; pair < 4U; pair++) {
            tex[pair] = ((uint16_t)strip->pixels[pair * 2U] << 8) |
                strip->pixels[pair * 2U + 1U];
        }

        vdp1_cmdt_t *const command = &sprite_cmdts[CMDT_SPRITE_FIRST + index];
        vdp1_cmdt_normal_sprite_set(command);
        /* Inner user clipping, end-code disabled, pixel zero transparent,
         * 16-colour-bank source, replace colour. */
        command->cmd_pmod = 0x0480U;
        command->cmd_colr = 0U;
        vdp1_cmdt_char_base_set(command, texture);
        vdp1_cmdt_char_size_set(command, 8U, 1U);
        command->cmd_xa = (int16_t)(GAME_X + strip->x);
        command->cmd_ya = (int16_t)(GAME_Y + strip->y);
    }
    vdp1_cmdt_end_set(&sprite_cmdts[CMDT_SPRITE_FIRST + count]);
    vdp1_sync_cmdt_put(sprite_cmdts, count + 4U, 0U);
    vdp1_sync();
    sprite_frame_pending = true;
}

static void
hud_line_draw(uint32_t row, const char *line)
{
    volatile uint16_t *const map =
        (volatile uint16_t *)(uintptr_t)HUD_MAP_ADDR;
    volatile uint16_t *const destination =
        map + row * MAP_CELLS + HUD_MAP_X;
    for (uint32_t x = 0; x < HUD_MAP_WIDTH; x++) {
        const char character = line == NULL ? '\0' : line[x];
        if ((character < ' ') || (character > '_')) {
            destination[x] = 0U;
            if (character == '\0') line = NULL;
            continue;
        }
        destination[x] = VDP2_SCRN_PND_CONFIG_0(0,
            HUD_CHAR_ADDR + ((uint32_t)(character - ' ') * 32U),
            VDP2_CRAM_ADDR(0), 0, 0);
    }
}

void
saturn_vdp12_hud_draw(const char *line_1, const char *line_2,
    const char *line_3)
{
    if (!initialized) return;
    hud_line_draw(0U, line_1);
    hud_line_draw(1U, line_2);
    hud_line_draw(2U, line_3);
}
