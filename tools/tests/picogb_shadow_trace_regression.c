/*
 * Host regression for the live Slave PPU shadow protocol.
 *
 * Build PicoGB with KNOVGBC_SLAVE_PPU_SHADOW so the same two hooks used on
 * Saturn record a complete Master scanout. Mutate VRAM, OAM/DMA and PPU I/O
 * while that scanout is active, replay only the visual trace into a private
 * PPU, and require the visible RGB1555 pixels to match exactly.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fabric.h"
#include "microcode.h"
#include "microcode_dispatch.h"
#include "mi.h"
#include "ppu.h"

enum {
    TRACE_WIDTH = 160,
    TRACE_HEIGHT = 144,
    TRACE_STRIDE = 176,
    TRACE_VIEW_X = 8,
    TRACE_MAX_DELTAS = 8192
};

enum trace_area {
    TRACE_VRAM = 0,
    TRACE_OAM = 1
};

struct trace_delta {
    uint16_t offset;
    uint8_t value;
    uint8_t area;
};

struct trace_line {
    uint16_t delta_first;
    uint16_t delta_count;
    uint8_t scan_y;
    uint8_t internal_wy;
    uint8_t lcdc;
    uint8_t scx;
    uint8_t scy;
    uint8_t wx;
    uint8_t wy;
    uint8_t bgp;
    uint8_t obp0;
    uint8_t obp1;
    hilow16_t latches[PPU_N_OBJS];
};

struct trace_capture {
    int active;
    int invalid;
    uint32_t line_count;
    uint32_t delta_count;
    r8 base_vram[0x2000];
    r8 base_oam[0x100];
    r8 vram_dirty[0x2000 / 8];
    r8 oam_dirty[0x100 / 8];
    struct trace_line lines[TRACE_HEIGHT];
    struct trace_delta deltas[TRACE_MAX_DELTAS];
};

struct fixture {
    struct mb_state mb;
    struct mi_dispatch memory;
    struct ppu_t ppu;
    struct pgf_userdata_t fabric;
    r8 rom[0x8000];
    r8 wram[0x2000];
    r8 vram[0x2000];
    r8 sram[0x2000];
    r8 hram[0x100];
    r8 oam[0x100];
    pixel_t master_frame[TRACE_HEIGHT][TRACE_STRIDE];
    pixel_t *master_lines[TRACE_HEIGHT];
};

static struct trace_capture capture;

static void bit_set(r8 *bits, uint32_t index)
{
    bits[index >> 3] |= (r8)(1U << (index & 7U));
}

static int bit_test(const r8 *bits, uint32_t index)
{
    return (bits[index >> 3] & (r8)(1U << (index & 7U))) != 0U;
}

static void bit_clear(r8 *bits, uint32_t index)
{
    bits[index >> 3] &= (r8)~(1U << (index & 7U));
}

static void trace_append(uint8_t area, uint16_t offset, uint8_t value)
{
    if (capture.delta_count >= TRACE_MAX_DELTAS) {
        capture.invalid = 1;
        return;
    }

    capture.deltas[capture.delta_count].area = area;
    capture.deltas[capture.delta_count].offset = offset;
    capture.deltas[capture.delta_count].value = value;
    capture.delta_count++;
}

static void trace_flush(const struct ppu_t *ppu)
{
    uint32_t offset;

    for (offset = 0U; offset < 0x2000U; offset++) {
        if (bit_test(capture.vram_dirty, offset)) {
            trace_append(TRACE_VRAM, (uint16_t)offset, ppu->VRAM[offset]);
            bit_clear(capture.vram_dirty, offset);
        }
    }
    for (offset = 0U; offset < 0x100U; offset++) {
        if (bit_test(capture.oam_dirty, offset)) {
            trace_append(TRACE_OAM, (uint16_t)offset, ppu->OAM[offset]);
            bit_clear(capture.oam_dirty, offset);
        }
    }
}

void knovgbc_slave_ppu_shadow_frame_begin(const struct ppu_t *ppu)
{
    if (capture.active) {
        return;
    }

    memset(&capture, 0, sizeof(capture));
    memcpy(capture.base_vram, ppu->VRAM, sizeof(capture.base_vram));
    memcpy(capture.base_oam, ppu->OAM, sizeof(capture.base_oam));
    capture.active = 1;
}

void knovgbc_slave_ppu_shadow_line(const struct ppu_t *ppu)
{
    struct trace_line *line;
    uint32_t index;

    if (!capture.active || capture.invalid) {
        return;
    }

    index = capture.line_count;
    if ((index >= TRACE_HEIGHT) || (ppu->state.scanY != index)) {
        capture.invalid = 1;
        return;
    }

    line = &capture.lines[index];
    line->delta_first = (uint16_t)capture.delta_count;
    trace_flush(ppu);
    line->delta_count = (uint16_t)(capture.delta_count - line->delta_first);
    line->scan_y = (uint8_t)ppu->state.scanY;
    line->internal_wy = (uint8_t)ppu->_internal_WY;
    line->lcdc = (uint8_t)ppu->rLCDC;
    line->scx = (uint8_t)ppu->rSCX;
    line->scy = (uint8_t)ppu->rSCY;
    line->wx = (uint8_t)ppu->rWX;
    line->wy = (uint8_t)ppu->rWY;
    line->bgp = (uint8_t)ppu->rBGP;
    line->obp0 = (uint8_t)ppu->rOBP0;
    line->obp1 = (uint8_t)ppu->rOBP1;
    memcpy(line->latches, ppu->state.latches, sizeof(line->latches));
    capture.line_count++;
}

static void trace_vram_write(void *userdata, word offset)
{
    (void)userdata;
    if (capture.active && (offset < 0x2000U)) {
        bit_set(capture.vram_dirty, offset);
    }
}

static void trace_oam_write(void *userdata, word offset)
{
    uint32_t index;
    (void)userdata;

    if (!capture.active) {
        return;
    }
    if (offset == 0U) {
        for (index = 0U; index < 0x100U; index++) {
            bit_set(capture.oam_dirty, index);
        }
    }
    else if (offset < 0x100U) {
        bit_set(capture.oam_dirty, offset);
    }
}

static word rom_write(void *userdata, word address, word data, word type)
{
    (void)userdata;
    (void)address;
    (void)data;
    (void)type;
    return MB_DATA_DEFAULT;
}

static void write_byte(struct fixture *fixture, word address, word value)
{
    mch_memory_dispatch_write(&fixture->mb, address, value);
}

static int fixture_init(struct fixture *fixture)
{
    uint32_t y;

    memset(fixture, 0, sizeof(*fixture));
    for (y = 0U; y < TRACE_HEIGHT; y++) {
        fixture->master_lines[y] = fixture->master_frame[y];
    }

    fixture->memory.ROM = fixture->rom;
    fixture->memory.WRAM = fixture->wram;
    fixture->memory.VRAM = fixture->vram;
    fixture->memory.SRAM = fixture->sram;
    fixture->memory.HRAM = fixture->hram;
    fixture->memory.OAM = fixture->oam;
    fixture->memory.BANK_ROM = 1U;
    fixture->memory.BANK_WRAM = 1U;
    fixture->memory.N_ROM = 2U;
    fixture->memory.N_SRAM = 1U;
    fixture->memory.userdata = &fixture->fabric;
    fixture->memory.dispatch_ROM = rom_write;
    fixture->memory.dispatch_IO = pgf_cb_IO_;
    fixture->memory.dispatch_VRAM_write = trace_vram_write;
    fixture->memory.dispatch_OAM_write = trace_oam_write;

    fixture->mb.mi = &fixture->memory;
    micache_invalidate(&fixture->mb.micache);
    fixture->fabric.mb = &fixture->mb;
    fixture->fabric.ppu = &fixture->ppu;

    ppu_initialize(&fixture->ppu);
    fixture->ppu.VRAM = fixture->vram;
    fixture->ppu.OAM = fixture->oam;
    fixture->ppu.state.framebuffer = fixture->master_lines;
    fixture->ppu.state.scanY = 0U;
    fixture->ppu.next_update_n = 0U;
    fixture->ppu.next_update_ticks = 0U;
    fixture->ppu.state.interlace = ~0U;
    fixture->ppu.rLCDC = 0xF7U;
    fixture->ppu.rSTAT = 0x85U;
    fixture->ppu.rSCX = 3U;
    fixture->ppu.rSCY = 5U;
    fixture->ppu.rWX = 70U;
    fixture->ppu.rWY = 40U;
    fixture->ppu.rBGP = 0xE4U;
    fixture->ppu.rOBP0 = 0xD2U;
    fixture->ppu.rOBP1 = 0x39U;
#if defined(KNOVGBC_PPU_PALETTE_CACHE)
    ppu_dmg_palette_cache_refresh(&fixture->ppu);
#endif
    return 1;
}

static void fixture_install_scene(struct fixture *fixture)
{
    uint32_t tile;
    uint32_t row;
    uint32_t offset;

    for (tile = 0U; tile < 384U; tile++) {
        for (row = 0U; row < 8U; row++) {
            const word address = 0x8000U + (tile << 4U) + (row << 1U);
            write_byte(fixture, address,
                (word)((tile * 17U) ^ (row * 29U) ^ 0x5AU));
            write_byte(fixture, address + 1U,
                (word)((tile * 11U) ^ (row * 43U) ^ 0xA5U));
        }
    }
    for (offset = 0U; offset < 0x400U; offset++) {
        write_byte(fixture, 0x9800U + offset, (offset * 7U) & 0xFFU);
        write_byte(fixture, 0x9C00U + offset, (offset * 13U) & 0xFFU);
    }
    for (tile = 0U; tile < 11U; tile++) {
        const word base = 0xFE00U + (tile << 2U);
        write_byte(fixture, base, 16U + (tile & 7U));
        write_byte(fixture, base + 1U, 8U + (tile * 13U));
        write_byte(fixture, base + 2U, 4U + tile);
        write_byte(fixture, base + 3U, (tile & 1U) ? 0x60U : 0x10U);
    }
}

static int trace_replay_matches(const struct fixture *fixture)
{
    struct ppu_t slave;
    r8 vram[0x2000];
    r8 oam[0x100];
    pixel_t slave_frame[TRACE_HEIGHT][TRACE_STRIDE];
    pixel_t *slave_lines[TRACE_HEIGHT];
    uint32_t line_index;

    if (!capture.active || capture.invalid ||
        (capture.line_count != TRACE_HEIGHT)) {
        return 0;
    }

    memcpy(vram, capture.base_vram, sizeof(vram));
    memcpy(oam, capture.base_oam, sizeof(oam));
    memset(&slave, 0, sizeof(slave));
    memset(slave_frame, 0, sizeof(slave_frame));
    for (line_index = 0U; line_index < TRACE_HEIGHT; line_index++) {
        slave_lines[line_index] = slave_frame[line_index];
    }
    ppu_initialize(&slave);
    slave.VRAM = vram;
    slave.OAM = oam;
    slave.state.framebuffer = slave_lines;

    for (line_index = 0U; line_index < TRACE_HEIGHT; line_index++) {
        const struct trace_line *const line = &capture.lines[line_index];
        const uint32_t delta_end = (uint32_t)line->delta_first +
            (uint32_t)line->delta_count;
        uint32_t delta_index;

        if ((line->scan_y != line_index) || (delta_end > capture.delta_count)) {
            return 0;
        }
        for (delta_index = line->delta_first;
             delta_index < delta_end; delta_index++) {
            const struct trace_delta *const delta =
                &capture.deltas[delta_index];
            if ((delta->area == TRACE_VRAM) && (delta->offset < 0x2000U)) {
                vram[delta->offset] = delta->value;
            }
            else if ((delta->area == TRACE_OAM) &&
                (delta->offset < 0x100U)) {
                oam[delta->offset] = delta->value;
            }
            else {
                return 0;
            }
        }

        slave.state.scanY = line->scan_y;
        slave._internal_WY = line->internal_wy;
        slave.rLCDC = line->lcdc;
        slave.rSCX = line->scx;
        slave.rSCY = line->scy;
        slave.rWX = line->wx;
        slave.rWY = line->wy;
        slave.rBGP = line->bgp;
        slave.rOBP0 = line->obp0;
        slave.rOBP1 = line->obp1;
        memcpy(slave.state.latches, line->latches, sizeof(line->latches));
#if defined(KNOVGBC_PPU_PALETTE_CACHE)
        ppu_dmg_palette_cache_refresh(&slave);
#endif
        ppu_slave_shadow_render_scanline(&slave);
    }

    for (line_index = 0U; line_index < TRACE_HEIGHT; line_index++) {
        uint32_t x;
        for (x = 0U; x < TRACE_WIDTH; x++) {
            if (fixture->master_frame[line_index][TRACE_VIEW_X + x] !=
                slave_frame[line_index][TRACE_VIEW_X + x]) {
                return 0;
            }
        }
    }
    return 1;
}

int main(void)
{
    struct fixture fixture;
    uint32_t guard;
    int mutated = 0;

    if (!fixture_init(&fixture)) {
        return 1;
    }
    fixture_install_scene(&fixture);
    for (guard = 0U; guard < 200U; guard++) {
        (void)ppu_tick(&fixture.ppu, 456U);

        /* The writes land between scanlines. They deliberately exercise the
         * normal dispatch hooks, PPU register snapshots and OAM DMA path. */
        if (capture.active && !mutated && (fixture.ppu.state.scanY >= 48U)) {
            uint32_t index;
            write_byte(&fixture, 0x8052U, 0xFFU);
            write_byte(&fixture, 0x8053U, 0x00U);
            write_byte(&fixture, 0x9821U, 0x05U);
            write_byte(&fixture, 0xFF47U, 0x1BU);
            write_byte(&fixture, 0xFF43U, 0x07U);
            write_byte(&fixture, 0xFF4AU, 0x10U);
            write_byte(&fixture, 0xFF4BU, 0x07U);
            for (index = 0U; index < 0xA0U; index++) {
                fixture.wram[index] = (r8)((index * 37U) + 19U);
            }
            write_byte(&fixture, 0xFF46U, 0xC0U);
            mutated = 1;
        }

        if (fixture.ppu._redrawed != 0U) {
            fixture.ppu._redrawed = 0U;
            break;
        }
    }

    if (!mutated || !trace_replay_matches(&fixture)) {
        fprintf(stderr, "Slave shadow trace replay mismatch\n");
        return 2;
    }

    printf("shadow-trace lines=%u deltas=%u match=1\n",
        (unsigned int)capture.line_count,
        (unsigned int)capture.delta_count);
    return 0;
}
