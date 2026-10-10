#include "platform.h"

#include "ppu.h"

#include <cpu/cache.h>
#include <cpu/dual.h>
#include <cpu/intc.h>
#include <yaul.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* PicoGB renders eight guard pixels on either side of the visible Game Boy
 * image. Keep that native layout in both fixtures so the probe exercises the
 * actual PPU write geometry rather than a reduced 160-pixel approximation. */
#define PROBE_WIDTH         160U
#define PROBE_HEIGHT        144U
#define PROBE_STRIDE        176U
#define PROBE_VIEW_X          8U
#define PROBE_FRAME_GUARD     2U
#define PROBE_FRAME_PIXELS \
    (PROBE_FRAME_GUARD + (PROBE_HEIGHT * PROBE_STRIDE) + PROBE_FRAME_GUARD)
#define PROBE_GUARD_VALUE 0x5AA5U

#define RGB1555_RED     0x001FU
#define RGB1555_BLUE    0x7C00U
#define RGB1555_WHITE   0x7FFFU

enum probe_status {
    PROBE_STATUS_IDLE = 0U,
    PROBE_STATUS_READY,
    PROBE_STATUS_RUNNING,
    PROBE_STATUS_DONE,
    PROBE_STATUS_ERROR
};

struct ppu_fixture {
    ppu_t ppu;
    uint8_t vram[0x2000] __attribute__((aligned(4)));
    uint8_t oam[0x0100] __attribute__((aligned(4)));
    pixel_t pixels[PROBE_FRAME_PIXELS] __attribute__((aligned(4)));
    pixel_t *lines[PROBE_HEIGHT];
};

/* This descriptor and the Slave-owned fixture deliberately use P2/cache-
 * through addresses. The two SH-2 data caches are not coherent, so it is
 * safer for this first hardware probe to trade a little speed for an
 * unambiguous ownership model. A production pipeline can introduce cached,
 * double-buffered snapshots only after this proves the handoff itself. */
struct slave_job {
    volatile uint32_t status;
    volatile uint32_t checksum;
    volatile uint32_t frames;
    volatile uint32_t ready;
};

static struct ppu_fixture master_fixture;
static struct ppu_fixture slave_fixture __uncached;
static struct slave_job slave_job __uncached;

static void __attribute__((noreturn))
probe_failure(uint32_t stage);

static void
compiler_barrier(void)
{
    __asm__ volatile("" ::: "memory");
}

static uint32_t
fixture_checksum(const struct ppu_fixture *fixture)
{
    uint32_t hash = 2166136261UL;

    for (uint32_t y = 0U; y < PROBE_HEIGHT; y++) {
        const pixel_t *const row = fixture->lines[y] + PROBE_VIEW_X;

        for (uint32_t x = 0U; x < PROBE_WIDTH; x++) {
            hash ^= row[x];
            hash *= 16777619UL;
        }
    }

    return hash;
}

static bool
fixture_guards_valid(const struct ppu_fixture *fixture)
{
    return (fixture->pixels[0] == PROBE_GUARD_VALUE) &&
        (fixture->pixels[1] == PROBE_GUARD_VALUE) &&
        (fixture->pixels[PROBE_FRAME_GUARD +
            (PROBE_HEIGHT * PROBE_STRIDE)] == PROBE_GUARD_VALUE) &&
        (fixture->pixels[PROBE_FRAME_GUARD +
            (PROBE_HEIGHT * PROBE_STRIDE) + 1U] == PROBE_GUARD_VALUE);
}

static bool
fixtures_match(const struct ppu_fixture *left,
    const struct ppu_fixture *right)
{
    if (!fixture_guards_valid(left) || !fixture_guards_valid(right)) {
        return false;
    }

    for (uint32_t y = 0U; y < PROBE_HEIGHT; y++) {
        const pixel_t *const left_row = left->lines[y] + PROBE_VIEW_X;
        const pixel_t *const right_row = right->lines[y] + PROBE_VIEW_X;

        for (uint32_t x = 0U; x < PROBE_WIDTH; x++) {
            if (left_row[x] != right_row[x]) {
                return false;
            }
        }
    }

    return true;
}

static void
fixture_install_scene(struct ppu_fixture *fixture)
{
    /* The pattern covers every physical tile slot, both tile maps, varied
     * scroll/window state, and eleven OAM candidates. It is deterministic,
     * visually recognisable, and exercises the same scanline renderer used by
     * the MVP without involving the SM83 interpreter. */
    for (uint32_t tile = 0U; tile < 384U; tile++) {
        for (uint32_t row = 0U; row < 8U; row++) {
            const uint32_t offset = (tile << 4U) | (row << 1U);

            fixture->vram[offset] = (uint8_t)((tile * 17U) ^
                (row * 29U) ^ 0x5AU);
            fixture->vram[offset + 1U] = (uint8_t)((tile * 11U) ^
                (row * 43U) ^ 0xA5U);
        }
    }

    for (uint32_t offset = 0U; offset < 0x0400U; offset++) {
        fixture->vram[0x1800U + offset] =
            (uint8_t)((offset * 7U) & 0xFFU);
        fixture->vram[0x1C00U + offset] =
            (uint8_t)((offset * 13U) & 0xFFU);
    }

    for (uint32_t sprite = 0U; sprite < 11U; sprite++) {
        const uint32_t offset = sprite << 2U;
        const uint32_t x = (sprite == 0U) ? 0U :
            (sprite == 1U) ? 1U :
            (sprite == 10U) ? 168U : (8U + (sprite * 14U));

        fixture->oam[offset + 0U] = (uint8_t)(16U + (sprite & 7U));
        fixture->oam[offset + 1U] = (uint8_t)x;
        fixture->oam[offset + 2U] = (uint8_t)(4U + sprite);
        fixture->oam[offset + 3U] = (uint8_t)(((sprite & 1U) ? 0x20U : 0U) |
            ((sprite & 2U) ? 0x40U : 0U) |
            ((sprite & 4U) ? 0x10U : 0U));
    }
}

static bool
fixture_init(struct ppu_fixture *fixture)
{
    memset(fixture, 0, sizeof(*fixture));

    fixture->pixels[0] = PROBE_GUARD_VALUE;
    fixture->pixels[1] = PROBE_GUARD_VALUE;
    fixture->pixels[PROBE_FRAME_GUARD +
        (PROBE_HEIGHT * PROBE_STRIDE)] = PROBE_GUARD_VALUE;
    fixture->pixels[PROBE_FRAME_GUARD +
        (PROBE_HEIGHT * PROBE_STRIDE) + 1U] = PROBE_GUARD_VALUE;

    for (uint32_t y = 0U; y < PROBE_HEIGHT; y++) {
        fixture->lines[y] = &fixture->pixels[PROBE_FRAME_GUARD +
            (y * PROBE_STRIDE)];
    }

    ppu_initialize(&fixture->ppu);
    fixture->ppu.VRAM = fixture->vram;
    fixture->ppu.OAM = fixture->oam;
    fixture->ppu.state.framebuffer = fixture->lines;
    fixture->ppu.state.scanY = 0U;
    fixture->ppu.next_update_n = 0U;
    fixture->ppu.next_update_ticks = 0U;
    fixture->ppu.state.interlace = ~0U;

    /* LCD on, BG/window/sprites enabled, 8x16 sprites, unsigned tiles,
     * BG map 9800 and window map 9C00. */
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

    fixture_install_scene(fixture);
    return fixture_guards_valid(fixture);
}

static bool
fixture_run_three_frames(struct ppu_fixture *fixture)
{
    for (uint32_t frame = 0U; frame < 3U; frame++) {
        fixture->ppu._redrawed = 0U;

        for (uint32_t event = 0U; event < 200U; event++) {
            (void)ppu_tick(&fixture->ppu, 456U);

            if (fixture->ppu._redrawed != 0U) {
                break;
            }

            if (event == 199U) {
                return false;
            }
        }
    }

    return fixture_guards_valid(fixture);
}

static void
slave_ppu_worker(void)
{
    if (cpu_dual_executor_get() != CPU_SLAVE) {
        slave_job.status = PROBE_STATUS_ERROR;
        return;
    }

    /* Yaul's Slave startup purges the cache, but the loader may have left the
     * cache in 2-way/RAM mode. Match the deterministic four-way setup used by
     * the Master before executing the PPU's code and data workload. */
    if (slave_job.ready == 0U) {
        cpu_cache_way_mode_set(CPU_CACHE_MODE_4_WAY);
        cpu_cache_purge();
        compiler_barrier();
        slave_job.ready = 1U;
        return;
    }

    if (slave_job.status != PROBE_STATUS_READY) {
        return;
    }

    slave_job.status = PROBE_STATUS_RUNNING;
    compiler_barrier();

    if (!fixture_run_three_frames(&slave_fixture)) {
        slave_job.status = PROBE_STATUS_ERROR;
        return;
    }

    slave_job.checksum = fixture_checksum(&slave_fixture);
    slave_job.frames = 3U;
    compiler_barrier();
    slave_job.status = PROBE_STATUS_DONE;
}

static bool
wait_for_status(uint32_t expected)
{
    /* A bounded polling loop turns a broken Slave bring-up into a red error
     * card instead of a black screen. This is intentionally much longer than
     * the PPU workload on real hardware. */
    for (uint32_t spin = 0U; spin < 0x02000000UL; spin++) {
        const uint32_t status = slave_job.status;

        if (status == expected) {
            return true;
        }
        if (status == PROBE_STATUS_ERROR) {
            return false;
        }
        __asm__ volatile("nop");
    }

    return false;
}

static bool
wait_for_slave_ready(void)
{
    for (uint32_t spin = 0U; spin < 0x02000000UL; spin++) {
        if (slave_job.ready != 0U) {
            return true;
        }
        __asm__ volatile("nop");
    }

    return false;
}

static void
show_card(uint16_t colour, uint32_t bars)
{
    for (uint32_t y = 0U; y < PROBE_HEIGHT; y++) {
        pixel_t *const row = master_fixture.lines[y] + PROBE_VIEW_X;

        for (uint32_t x = 0U; x < PROBE_WIDTH; x++) {
            pixel_t pixel = colour;

            if ((x < 2U) || (x >= PROBE_WIDTH - 2U) ||
                (y < 2U) || (y >= PROBE_HEIGHT - 2U)) {
                pixel = RGB1555_WHITE;
            }

            for (uint32_t bar = 0U; bar < bars; bar++) {
                const uint32_t left = 8U + (bar * 12U);
                if ((x >= left) && (x < left + 6U) &&
                    (y >= 8U) && (y < 28U)) {
                    pixel = RGB1555_WHITE;
                }
            }

            row[x] = pixel;
        }
    }

    saturn_platform_present_strided(master_fixture.lines[0] + PROBE_VIEW_X,
        PROBE_STRIDE);
}

static void __attribute__((noreturn))
probe_failure(uint32_t stage)
{
    show_card(RGB1555_RED, stage);
    for (;;) {
        __asm__ volatile("nop");
    }
}

static void
cache_configure_master(void)
{
    const uint8_t old_mask = cpu_intc_mask_get();

    cpu_intc_mask_set(15);
    cpu_cache_way_mode_set(CPU_CACHE_MODE_4_WAY);
    cpu_cache_purge();
    cpu_intc_mask_set(old_mask);
}

int
main(void)
{
    cache_configure_master();
    saturn_platform_video_init();

    if (!fixture_init(&master_fixture) || !fixture_init(&slave_fixture)) {
        probe_failure(1U);
    }

    if (!fixture_run_three_frames(&master_fixture)) {
        probe_failure(2U);
    }

    /* Keep the proven Master-only CPU startup intact until Yaul and the VDP
     * have completed their normal initialization. This public Yaul call then
     * starts the Slave in polling mode and installs the documented 0x94 entry
     * trampoline without changing the emulation CPU path. */
    slave_job.status = PROBE_STATUS_IDLE;
    slave_job.ready = 0U;
    compiler_barrier();
    cpu_dual_comm_mode_set(CPU_DUAL_ENTRY_POLLING);
    cpu_dual_slave_set(slave_ppu_worker);

    /* First notification is an explicit ready handshake. It prevents the
     * actual PPU job from racing the Slave's private Yaul initialization. */
    cpu_dual_slave_notify();
    if (!wait_for_slave_ready()) {
        probe_failure(3U);
    }

    slave_job.status = PROBE_STATUS_READY;
    compiler_barrier();
    cpu_dual_slave_notify();
    if (!wait_for_status(PROBE_STATUS_DONE)) {
        probe_failure(4U);
    }
    compiler_barrier();

    if ((slave_job.frames != 3U) ||
        (slave_job.checksum != fixture_checksum(&master_fixture)) ||
        !fixtures_match(&master_fixture, &slave_fixture)) {
        probe_failure(5U);
    }

    /* The displayed image was generated in the Slave fixture. Success means
     * it matched the Master reference pixel-for-pixel, including guard bounds.
     * No SM83, timer, VDP2 or controller work is ever run by the Slave. */
    saturn_platform_present_strided(slave_fixture.lines[0] + PROBE_VIEW_X,
        PROBE_STRIDE);

    for (;;) {
        __asm__ volatile("nop");
    }
}
