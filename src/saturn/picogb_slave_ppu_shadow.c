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
#include "picogb_slave_ppu_shadow.h"

#include "mvp_profiler.h"

#include <cpu/cache.h>
#include <cpu/dual.h>
#include <yaul.h>

#include "fabric.h"
#include "ppu.h"

#include <stddef.h>
#include <string.h>

/* Keep the native PicoGB row geometry. The renderer may touch the eight
 * guard pixels on each side while presenting only [8, 168). */
#define SHADOW_WIDTH             160U
#define SHADOW_HEIGHT            144U
#define SHADOW_STRIDE            176U
#define SHADOW_VIEW_X              8U
#define SHADOW_VRAM_BYTES       0x2000U
#define SHADOW_OAM_BYTES        0x0100U
#define SHADOW_SLOT_COUNT           2U
#define SHADOW_MAX_DELTAS       8192U
#define SHADOW_TIMEOUT_FRAMES     20U

enum shadow_slot_state {
    SHADOW_SLOT_FREE = 0U,
    SHADOW_SLOT_FILLING,
    SHADOW_SLOT_READY,
    SHADOW_SLOT_RUNNING,
    SHADOW_SLOT_DONE,
    SHADOW_SLOT_ERROR
};

enum shadow_status_code {
    /* Values shown as S000x in the test target's existing HUD. */
    SHADOW_STATUS_WAITING = 0U,
    SHADOW_STATUS_MATCH = 1U,
    SHADOW_STATUS_MISMATCH = 2U,
    SHADOW_STATUS_DROPPED = 3U,
    SHADOW_STATUS_TIMEOUT = 4U,
    SHADOW_STATUS_ERROR = 5U
};

enum shadow_delta_area {
    SHADOW_DELTA_VRAM = 0U,
    SHADOW_DELTA_OAM = 1U
};

struct shadow_delta {
    uint16_t offset;
    uint8_t value;
    uint8_t area;
};

/* This is exactly the information the generic DMG scanline compositor reads
 * at the instant it draws a line. It deliberately excludes timing/IRQ state:
 * timing remains entirely Master-owned. */
struct shadow_line {
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

struct shadow_slot {
    volatile uint32_t state;
    volatile uint32_t sequence;
    volatile uint32_t deadline_frame;
    volatile uint32_t error;
    volatile uint32_t line_count;
    volatile uint32_t delta_count;
    volatile uint32_t reference_checksum;
    volatile uint32_t slave_checksum;

    uint8_t vram[SHADOW_VRAM_BYTES];
    uint8_t oam[SHADOW_OAM_BYTES];
    uint8_t vram_dirty[SHADOW_VRAM_BYTES / 8U];
    uint8_t oam_dirty[SHADOW_OAM_BYTES / 8U];
    struct shadow_line lines[SHADOW_HEIGHT];
    struct shadow_delta deltas[SHADOW_MAX_DELTAS];

    /* A reference makes a mismatch inspectable without looking at the live
     * Master frame after it has advanced. The Slave writes only slave_frame. */
    pixel_t master_frame[SHADOW_HEIGHT][SHADOW_STRIDE]
        __attribute__((aligned(4)));
    pixel_t slave_frame[SHADOW_HEIGHT][SHADOW_STRIDE]
        __attribute__((aligned(4)));
};

struct shadow_control {
    volatile uint32_t slave_ready;
    volatile uint32_t slave_error;
};

/* P2 is cache-through on both SH-2s. It is intentionally initialized by the
 * Master: Yaul's normal BSS clear does not cover .uncached. */
static struct shadow_slot shadow_slots[SHADOW_SLOT_COUNT] __uncached
    __attribute__((aligned(16)));
static struct shadow_control shadow_control __uncached
    __attribute__((aligned(16)));

static int32_t capture_slot_index = -1;
static uint32_t next_sequence;
static uint32_t master_frame_number;
static bool capture_invalid;
static bool shadow_enabled;

static void
compiler_barrier(void)
{
    __asm__ volatile("" ::: "memory");
}

static void
bit_set(uint8_t *bits, uint32_t index)
{
    bits[index >> 3U] |= (uint8_t)(1U << (index & 7U));
}

static bool
bit_test(const uint8_t *bits, uint32_t index)
{
    return (bits[index >> 3U] & (uint8_t)(1U << (index & 7U))) != 0U;
}

static void
bit_clear(uint8_t *bits, uint32_t index)
{
    bits[index >> 3U] &= (uint8_t)~(1U << (index & 7U));
}

static uint32_t
frame_checksum(const pixel_t *frame, uint32_t stride_pixels)
{
    uint32_t hash = 2166136261UL;

    for (uint32_t y = 0U; y < SHADOW_HEIGHT; y++) {
        const pixel_t *const row = &frame[y * stride_pixels + SHADOW_VIEW_X];

        for (uint32_t x = 0U; x < SHADOW_WIDTH; x++) {
            hash ^= row[x];
            hash *= 16777619UL;
        }
    }

    return hash;
}

static bool
frames_match(const struct shadow_slot *slot)
{
    for (uint32_t y = 0U; y < SHADOW_HEIGHT; y++) {
        const pixel_t *const master =
            &slot->master_frame[y][SHADOW_VIEW_X];
        const pixel_t *const slave =
            &slot->slave_frame[y][SHADOW_VIEW_X];

        for (uint32_t x = 0U; x < SHADOW_WIDTH; x++) {
            if (master[x] != slave[x]) {
                return false;
            }
        }
    }

    return true;
}

static int32_t
free_slot_find(void)
{
    for (uint32_t index = 0U; index < SHADOW_SLOT_COUNT; index++) {
        if (shadow_slots[index].state == SHADOW_SLOT_FREE) {
            return (int32_t)index;
        }
    }

    return -1;
}

static void
capture_drop(void)
{
    if (capture_slot_index >= 0) {
        shadow_slots[capture_slot_index].state = SHADOW_SLOT_FREE;
    }

    capture_slot_index = -1;
    capture_invalid = false;
    mvp_profiler_shadow_status_set(SHADOW_STATUS_DROPPED);
}

static void
capture_delta_append(struct shadow_slot *slot, uint8_t area, uint16_t offset,
    uint8_t value)
{
    const uint32_t count = slot->delta_count;

    if (count >= SHADOW_MAX_DELTAS) {
        capture_invalid = true;
        slot->error = 1U;
        return;
    }

    slot->deltas[count].area = area;
    slot->deltas[count].offset = offset;
    slot->deltas[count].value = value;
    slot->delta_count = count + 1U;
}

static void
capture_dirty_flush(struct shadow_slot *slot, const ppu_t *ppu)
{
    for (uint32_t offset = 0U; offset < SHADOW_VRAM_BYTES; offset++) {
        if (bit_test(slot->vram_dirty, offset)) {
            capture_delta_append(slot, SHADOW_DELTA_VRAM, (uint16_t)offset,
                ppu->VRAM[offset]);
            bit_clear(slot->vram_dirty, offset);
        }
    }

    for (uint32_t offset = 0U; offset < SHADOW_OAM_BYTES; offset++) {
        if (bit_test(slot->oam_dirty, offset)) {
            capture_delta_append(slot, SHADOW_DELTA_OAM, (uint16_t)offset,
                ppu->OAM[offset]);
            bit_clear(slot->oam_dirty, offset);
        }
    }
}

void
knovgbc_slave_ppu_shadow_frame_begin(const ppu_t *ppu)
{
    int32_t index;
    struct shadow_slot *slot;

    if (!shadow_enabled || (capture_slot_index >= 0)) {
        return;
    }

    /* Only record the pass that the normal interlaced frontend actually
     * presents. A skipped pass has no authoritative full framebuffer. */
    if ((ppu->state.interlace & 3U) != 3U) {
        return;
    }

    index = free_slot_find();
    if (index < 0) {
        mvp_profiler_shadow_status_set(SHADOW_STATUS_DROPPED);
        return;
    }

    slot = &shadow_slots[index];
    slot->state = SHADOW_SLOT_FILLING;
    slot->sequence = ++next_sequence;
    slot->error = 0U;
    slot->line_count = 0U;
    slot->delta_count = 0U;
    slot->reference_checksum = 0U;
    slot->slave_checksum = 0U;
    memset(slot->vram_dirty, 0, sizeof(slot->vram_dirty));
    memset(slot->oam_dirty, 0, sizeof(slot->oam_dirty));
    memcpy(slot->vram, ppu->VRAM, sizeof(slot->vram));
    memcpy(slot->oam, ppu->OAM, sizeof(slot->oam));

    /* The compositor intentionally leaves prior pixels in place in a few
     * valid LCD configurations (for example when the DMG background is
     * disabled). Start the private Slave image from precisely the same frame
     * that the Master has at the beginning of this visual pass, so such
     * preserved pixels compare correctly too. */
    for (uint32_t y = 0U; y < SHADOW_HEIGHT; y++) {
        memcpy(slot->slave_frame[y], ppu->state.framebuffer[y],
            sizeof(slot->slave_frame[y]));
    }

    capture_slot_index = index;
    capture_invalid = false;
}

void
knovgbc_slave_ppu_shadow_line(const ppu_t *ppu)
{
    struct shadow_slot *slot;
    uint32_t line_index;
    struct shadow_line *line;
    const uint32_t scan_y = ppu->state.scanY;

    if (!shadow_enabled || (capture_slot_index < 0) || capture_invalid) {
        return;
    }

    slot = &shadow_slots[capture_slot_index];
    line_index = slot->line_count;
    if ((line_index >= SHADOW_HEIGHT) || (scan_y != line_index)) {
        capture_invalid = true;
        slot->error = 2U;
        return;
    }

    line = &slot->lines[line_index];
    line->delta_first = (uint16_t)slot->delta_count;
    capture_dirty_flush(slot, ppu);
    if (capture_invalid || (slot->delta_count > SHADOW_MAX_DELTAS)) {
        return;
    }

    line->delta_count = (uint16_t)(slot->delta_count - line->delta_first);
    line->scan_y = (uint8_t)scan_y;
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
    slot->line_count = line_index + 1U;
}

void
picogb_slave_ppu_shadow_vram_write(void *userdata, word offset)
{
    (void)userdata;

    if (shadow_enabled && (capture_slot_index >= 0) &&
        (offset < SHADOW_VRAM_BYTES) && !capture_invalid) {
        bit_set(shadow_slots[capture_slot_index].vram_dirty, offset);
    }
}

void
picogb_slave_ppu_shadow_oam_write(void *userdata, word offset)
{
    (void)userdata;

    if (!shadow_enabled || (capture_slot_index < 0) || capture_invalid) {
        return;
    }

    struct shadow_slot *const slot = &shadow_slots[capture_slot_index];
    if (offset == 0U) {
        /* The fabric reports a completed OAM DMA as offset zero. Marking all
         * OAM bytes is also harmless for the rare direct write at FE00. */
        memset(slot->oam_dirty, 0xFF, sizeof(slot->oam_dirty));
    }
    else if (offset < SHADOW_OAM_BYTES) {
        bit_set(slot->oam_dirty, offset);
    }
}

static bool
slot_render(struct shadow_slot *slot)
{
    ppu_t ppu;
    pixel_t *lines[SHADOW_HEIGHT];

    if (slot->line_count != SHADOW_HEIGHT) {
        return false;
    }

    memset(&ppu, 0, sizeof(ppu));
    ppu_initialize(&ppu);
    ppu.VRAM = slot->vram;
    ppu.OAM = slot->oam;
    ppu.state.framebuffer = lines;

    for (uint32_t y = 0U; y < SHADOW_HEIGHT; y++) {
        lines[y] = slot->slave_frame[y];
    }

    for (uint32_t index = 0U; index < SHADOW_HEIGHT; index++) {
        const struct shadow_line *const line = &slot->lines[index];
        const uint32_t delta_end = (uint32_t)line->delta_first +
            (uint32_t)line->delta_count;

        if ((line->scan_y != index) || (delta_end > slot->delta_count)) {
            return false;
        }

        for (uint32_t delta_index = line->delta_first;
             delta_index < delta_end; delta_index++) {
            const struct shadow_delta *const delta =
                &slot->deltas[delta_index];

            if ((delta->area == SHADOW_DELTA_VRAM) &&
                (delta->offset < SHADOW_VRAM_BYTES)) {
                slot->vram[delta->offset] = delta->value;
            }
            else if ((delta->area == SHADOW_DELTA_OAM) &&
                (delta->offset < SHADOW_OAM_BYTES)) {
                slot->oam[delta->offset] = delta->value;
            }
            else {
                return false;
            }
        }

        ppu.state.scanY = line->scan_y;
        ppu._internal_WY = line->internal_wy;
        ppu.rLCDC = line->lcdc;
        ppu.rSCX = line->scx;
        ppu.rSCY = line->scy;
        ppu.rWX = line->wx;
        ppu.rWY = line->wy;
        ppu.rBGP = line->bgp;
        ppu.rOBP0 = line->obp0;
        ppu.rOBP1 = line->obp1;
        memcpy(ppu.state.latches, line->latches, sizeof(line->latches));
#if defined(KNOVGBC_PPU_PALETTE_CACHE)
        ppu_dmg_palette_cache_refresh(&ppu);
#endif
        ppu_slave_shadow_render_scanline(&ppu);
    }

    slot->slave_checksum = frame_checksum(&slot->slave_frame[0][0],
        SHADOW_STRIDE);
    return true;
}

static void
slave_worker(void)
{
    if (cpu_dual_executor_get() != CPU_SLAVE) {
        shadow_control.slave_error = 1U;
        return;
    }

    if (shadow_control.slave_ready == 0U) {
        /* Yaul's Slave startup purges cache contents but preserves its mode.
         * Match the Master’s deterministic 4-way setting before measuring a
         * real rendering job. */
        cpu_cache_way_mode_set(CPU_CACHE_MODE_4_WAY);
        cpu_cache_purge();
        compiler_barrier();
        shadow_control.slave_ready = 1U;
        return;
    }

    /* A MINIT notification is a wake-up, not a queue. Process every READY
     * slot so a notification coalesced while this callback ran loses no job. */
    for (uint32_t pass = 0U; pass < SHADOW_SLOT_COUNT; pass++) {
        for (uint32_t index = 0U; index < SHADOW_SLOT_COUNT; index++) {
            struct shadow_slot *const slot = &shadow_slots[index];

            if (slot->state != SHADOW_SLOT_READY) {
                continue;
            }

            compiler_barrier();
            slot->state = SHADOW_SLOT_RUNNING;
            compiler_barrier();

            if (!slot_render(slot)) {
                slot->error = 3U;
                compiler_barrier();
                slot->state = SHADOW_SLOT_ERROR;
            }
            else {
                compiler_barrier();
                slot->state = SHADOW_SLOT_DONE;
            }
            break;
        }
    }
}

static bool
slave_ready_wait(void)
{
    for (uint32_t spin = 0U; spin < 0x02000000UL; spin++) {
        if (shadow_control.slave_ready != 0U) {
            return true;
        }
        if ((spin & 0xFFFFU) == 0U) {
            cpu_dual_slave_notify();
        }
        __asm__ volatile("nop");
    }

    return false;
}

bool
picogb_slave_ppu_shadow_init(void)
{
    memset(shadow_slots, 0, sizeof(shadow_slots));
    memset(&shadow_control, 0, sizeof(shadow_control));
    capture_slot_index = -1;
    next_sequence = 0U;
    master_frame_number = 0U;
    capture_invalid = false;
    shadow_enabled = false;

    for (uint32_t index = 0U; index < SHADOW_SLOT_COUNT; index++) {
        shadow_slots[index].state = SHADOW_SLOT_FREE;
    }

    compiler_barrier();
    cpu_dual_comm_mode_set(CPU_DUAL_ENTRY_POLLING);
    cpu_dual_slave_set(slave_worker);
    cpu_dual_slave_notify();
    if (!slave_ready_wait() || (shadow_control.slave_error != 0U)) {
        mvp_profiler_shadow_status_set(SHADOW_STATUS_ERROR);
        return false;
    }

    shadow_enabled = true;
    mvp_profiler_shadow_status_set(SHADOW_STATUS_WAITING);
    return true;
}

void
picogb_slave_ppu_shadow_frame_complete(const pixel_t *frame,
    uint32_t stride_pixels, bool rendered)
{
    struct shadow_slot *slot;

    master_frame_number++;
    if (!shadow_enabled || (capture_slot_index < 0)) {
        return;
    }

    slot = &shadow_slots[capture_slot_index];
    if (!rendered || capture_invalid ||
        (slot->line_count != SHADOW_HEIGHT) ||
        (slot->delta_count > SHADOW_MAX_DELTAS)) {
        capture_drop();
        return;
    }

    for (uint32_t y = 0U; y < SHADOW_HEIGHT; y++) {
        memcpy(slot->master_frame[y], &frame[y * stride_pixels],
            sizeof(slot->master_frame[y]));
    }
    slot->reference_checksum = frame_checksum(frame, stride_pixels);
    slot->deadline_frame = master_frame_number + SHADOW_TIMEOUT_FRAMES;
    compiler_barrier();
    slot->state = SHADOW_SLOT_READY;
    capture_slot_index = -1;
    capture_invalid = false;
    cpu_dual_slave_notify();
}

void
picogb_slave_ppu_shadow_poll(void)
{
    if (!shadow_enabled) {
        return;
    }

    if (shadow_control.slave_error != 0U) {
        shadow_enabled = false;
        mvp_profiler_shadow_status_set(SHADOW_STATUS_ERROR);
        return;
    }

    for (uint32_t index = 0U; index < SHADOW_SLOT_COUNT; index++) {
        struct shadow_slot *const slot = &shadow_slots[index];
        const uint32_t state = slot->state;

        if (state == SHADOW_SLOT_DONE) {
            compiler_barrier();
            if ((slot->reference_checksum == slot->slave_checksum) &&
                frames_match(slot)) {
                mvp_profiler_shadow_status_set(SHADOW_STATUS_MATCH);
            }
            else {
                mvp_profiler_shadow_status_set(SHADOW_STATUS_MISMATCH);
            }
            slot->state = SHADOW_SLOT_FREE;
        }
        else if (state == SHADOW_SLOT_ERROR) {
            shadow_enabled = false;
            mvp_profiler_shadow_status_set(SHADOW_STATUS_ERROR);
        }
        else if ((state == SHADOW_SLOT_READY) ||
                 (state == SHADOW_SLOT_RUNNING)) {
            if ((int32_t)(master_frame_number - slot->deadline_frame) > 0) {
                shadow_enabled = false;
                mvp_profiler_shadow_status_set(SHADOW_STATUS_TIMEOUT);
            }
        }
    }
}
