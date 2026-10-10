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
#include "mvp_profiler.h"
#if defined(KNOVGBC_OPCODE_PROFILE)
#include "picogb_opcode_profile.h"
#endif
#include "picogb_mvp_input.h"
#if defined(KNOVGBC_AUDIO)
#include "picogb_saturn_audio.h"
#endif
#if defined(KNOVGBC_SCHEDULER_BATCH)
#include "picogb_event_scheduler.h"
#endif
#if defined(KNOVGBC_SH2_SM83_ASM)
#include "picogb_sm83_sh2.h"
#endif
#if defined(KNOVGBC_SM83_COMPACT)
#include "picogb_sm83_compact.h"
#endif
#if defined(KNOVGBC_BLOCK_RECOMPILER)
#include "picogb_block_cache.h"
#endif
#if defined(KNOVGBC_BLOCK_STATIC)
#include "picogb_block_executor.h"
#endif
#if defined(KNOVGBC_IDLE_SPIN)
#include "picogb_idle_spin.h"
#endif
#if defined(KNOVGBC_HRAM_WAIT)
#include "picogb_hram_wait.h"
#endif
#if defined(KNOVGBC_SLAVE_PPU_SHADOW)
#include "picogb_slave_ppu_shadow.h"
#endif
#include "platform.h"
#if defined(KNOVGBC_VDP12_NATIVE)
#include "platform_video_vdp12.h"
#include "picogb_vdp12_frame.h"
#endif

#include "fabric.h"
#include "microcode.h"
#include "ppu.h"

#include <yaul.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* PicoGB draws eight guard pixels on either side of the 160-pixel viewport.
 * A row must therefore contain 176 pixels even though only [8, 168) is sent
 * to the Saturn. */
#define PICOGB_FRAME_STRIDE 176U
#define PICOGB_VIEW_X         8U
#if defined(KNOVGBC_AUDIO)
_Static_assert(CONFIG_APU_ENABLE && CONFIG_APU_MONO,
    "056 requires enabled mono DMG audio");
_Static_assert(APU_N_PER_TICK * PICOGB_SATURN_AUDIO_RATE == 1048576U,
    "APU and SCSP producer rates must use the same DMG clock");
#endif

#if defined(KNOVGBC_VDP2_INDEXED8)
#define MVP_PIXEL_RED     4U
#define MVP_PIXEL_BLUE    5U
#define MVP_PIXEL_YELLOW  6U
#define MVP_PIXEL_MAGENTA 7U
#define MVP_PIXEL_CYAN    8U
#define MVP_PIXEL_WHITE   3U
_Static_assert(PPU_MODE == 8 && sizeof(pixel_t) == sizeof(uint8_t),
    "The indexed VDP2 experiment requires one byte per PicoGB pixel");
#else
#define MVP_PIXEL_RED     0x001FU
#define MVP_PIXEL_BLUE    0x7C00U
#define MVP_PIXEL_YELLOW  0x03FFU
#define MVP_PIXEL_MAGENTA 0x7C1FU
#define MVP_PIXEL_CYAN    0x7FE0U
#define MVP_PIXEL_WHITE   0x7FFFU
_Static_assert(PPU_MODE == 7,
    "The Saturn MVP requires native PicoGB RGB1555 output");
_Static_assert(sizeof(pixel_t) == sizeof(uint16_t),
    "Unexpected PicoGB pixel type");
#endif
#if defined(KNOVGBC_NO_FRAMESKIP)
_Static_assert(PPU_INTERLACE == 0,
    "The full-rate Saturn test must render every logical frame");
#else
_Static_assert(PPU_INTERLACE == 1,
    "The Saturn MVP requires full-frame frameskip 1");
#endif

extern uint8_t knovgbc_mvp_rom[];
extern uint8_t knovgbc_mvp_rom_end[];

/* Every byte owned by the emulator is static.  The 8 KiB SRAM area is a
 * harmless guard for the address decoder; this ROM-only build never saves or
 * banks it. */
struct picogb_mvp_state {
    mb_state cpu;
    ppu_t ppu;
    struct mi_dispatch memory;
    struct pgf_userdata_t fabric;
#if defined(KNOVGBC_AUDIO)
    apu_t apu;
    int16_t audio_samples[4096] __attribute__((aligned(4)));
    uint32_t audio_consumed;
    uint32_t audio_dropped;
#endif

    uint8_t wram[0x2000] __attribute__((aligned(4)));
    uint8_t vram[0x2000] __attribute__((aligned(4)));
    uint8_t unused_sram[0x2000] __attribute__((aligned(4)));
    uint8_t hram[0x0100] __attribute__((aligned(4)));
    uint8_t oam[0x0100] __attribute__((aligned(4)));

    pixel_t core_frame[KNOVGB_HEIGHT][PICOGB_FRAME_STRIDE]
        __attribute__((aligned(4)));
    pixel_t *core_lines[KNOVGB_HEIGHT];
    word pending_ppu_irq;
#if defined(KNOVGBC_BLOCK_RECOMPILER)
    struct picogb_block_cache block_cache;
#endif
#if defined(KNOVGBC_BLOCK_STATIC)
    struct picogb_block_executor_cache block_executor_cache;
#endif
#if defined(KNOVGBC_SCHEDULER_BATCH)
    struct picogb_event_scheduler scheduler;
#endif
};

static struct picogb_mvp_state core;

#if defined(KNOVGBC_AUDIO)
/* Transfer modest contiguous PCM batches, rather than doing a sound-RAM
 * write or querying SCSP after every guest instruction. Unsigned cursors
 * retain their meaning across wraparound. If real playback cannot keep up,
 * discard the oldest pending samples, never overwrite an active SCSP page. */
static inline void
picogb_audio_service(bool force)
{
    uint32_t available = core.apu.outbuf_pos - core.audio_consumed;
    if (!force && available < 512U) return;
    picogb_saturn_audio_pump();
    if (available > 4096U) {
        core.audio_dropped += available - 4096U;
        core.audio_consumed = core.apu.outbuf_pos - 4096U;
        available = 4096U;
    }
    while (available != 0U) {
        const uint32_t start = core.audio_consumed & 4095U;
        uint32_t count = 4096U - start;
        if (count > available) count = available;
        const uint32_t accepted = picogb_saturn_audio_submit(
            &core.audio_samples[start], count);
        core.audio_consumed += accepted;
        available -= accepted;
        if (accepted < count) break;
    }
}
#endif

#if PPU_DMG_FAST_RENDERER || PPU_DMG_DIRTY_SCANLINES || defined(KNOVGBC_SLAVE_PPU_SHADOW)
/* The PicoGB memory dispatcher remains independent from the Saturn frontend.
 * These tiny adapters connect completed VRAM/OAM writes to optional derived
 * PPU data. The regular Master renderer and the Slave shadow recorder may
 * coexist; neither changes the emulated memory write itself. */
static void
picogb_mvp_vram_write(void *userdata, word offset)
{
#if PPU_DMG_FAST_RENDERER || PPU_DMG_DIRTY_SCANLINES
    struct pgf_userdata_t *const fabric = userdata;
    ppu_dmg_on_vram_write(fabric->ppu, offset);
#endif
#if defined(KNOVGBC_SLAVE_PPU_SHADOW)
    picogb_slave_ppu_shadow_vram_write(userdata, offset);
#endif
}

static void
picogb_mvp_oam_write(void *userdata, word offset)
{
#if PPU_DMG_FAST_RENDERER || PPU_DMG_DIRTY_SCANLINES
    struct pgf_userdata_t *const fabric = userdata;
    ppu_dmg_on_oam_write(fabric->ppu, offset);
#endif
#if defined(KNOVGBC_SLAVE_PPU_SHADOW)
    picogb_slave_ppu_shadow_oam_write(userdata, offset);
#endif
}
#endif

static void
show_startup_marker(pixel_t colour, uint32_t bars)
{
    for (uint32_t y = 0; y < KNOVGB_HEIGHT; y++) {
        for (uint32_t x = 0; x < KNOVGB_WIDTH; x++) {
            pixel_t pixel = colour;
            if ((x < 2U) || (x >= KNOVGB_WIDTH - 2U) ||
                (y < 2U) || (y >= KNOVGB_HEIGHT - 2U)) {
                pixel = MVP_PIXEL_WHITE;
            }
            for (uint32_t bar = 0; bar < bars; bar++) {
                const uint32_t left = 8U + (bar * 12U);
                if ((x >= left) && (x < left + 6U) &&
                    (y >= 8U) && (y < 28U)) {
                    pixel = MVP_PIXEL_WHITE;
                }
            }
            core.core_frame[y][PICOGB_VIEW_X + x] = pixel;
        }
    }
#if defined(KNOVGBC_VDP2_INDEXED8)
    saturn_platform_present_indexed8_strided(
#else
    saturn_platform_present_strided(
#endif
        &core.core_frame[0][PICOGB_VIEW_X], PICOGB_FRAME_STRIDE);
}

static void __attribute__((noreturn))
startup_failure(uint32_t stage)
{
    show_startup_marker(MVP_PIXEL_RED, stage);
    for (;;) {
        __asm__ volatile("nop");
    }
}

static bool
embedded_rom_is_supported(void)
{
    const size_t size = (size_t)(knovgbc_mvp_rom_end - knovgbc_mvp_rom);

#if defined(KNOVGBC_SML_MBC1)
    /* Separate DMG/MBC1 cartridge build. Four 16 KiB ROM banks and no
     * cartridge RAM match this image's embedded Super Mario Land ROM. */
    return (size == 0x10000U) &&
        ((knovgbc_mvp_rom[0x0143U] & 0x80U) == 0U) &&
        (knovgbc_mvp_rom[0x0147U] == 0x01U) &&
        (knovgbc_mvp_rom[0x0148U] == 0x01U) &&
        (knovgbc_mvp_rom[0x0149U] == 0U);
#else
    /* The first product image accepts exactly Tetris' cartridge class:
     * 32 KiB, original DMG mode and cartridge type 0 (no MBC). */
    return (size == 0x8000U) &&
        ((knovgbc_mvp_rom[0x0143U] & 0x80U) == 0U) &&
        (knovgbc_mvp_rom[0x0147U] == 0U) &&
        (knovgbc_mvp_rom[0x0148U] == 0U) &&
        (knovgbc_mvp_rom[0x0149U] == 0U);
#endif
}

#if !defined(KNOVGBC_SML_MBC1)
static word
rom_only_write(void *userdata, word address, word data, word type)
{
    (void)userdata;
    (void)address;
    (void)data;
    (void)type;
    return MB_DATA_DEFAULT;
}
#endif

#if defined(KNOVGBC_SCHEDULER_BATCH)
/* FF00-FF7F is the only CPU-visible boundary for DIV/TIMA and the PPU's
 * registers. Commit preceding deferred time before PicoGB reads or changes
 * one of those registers; no opcode classification is necessary. */
static word
picogb_mvp_io(void *userdata, word address, word data, word type)
{
    word result;

    picogb_event_scheduler_flush(&core.scheduler);
    result = pgf_cb_IO_(userdata, address, data, type);
    picogb_event_scheduler_reschedule(&core.scheduler);
    return result;
}
#endif

static bool
picogb_core_init(void)
{
    memset(&core, 0, sizeof(core));

    /* PicoGB upstream lays these unions out for little-endian hosts.  Refuse
     * to run unless the SH-2 endian port has corrected both representations;
     * executing with swapped B/C or instruction bytes only produces a
     * misleading crash later in the ROM. */
    core.cpu.reg.BC = 0x1234U;
    core.cpu.IR.raw = 0x5678U;
    if ((core.cpu.reg.B != 0x12U) || (core.cpu.reg.C != 0x34U) ||
        (core.cpu.IR.high != 0x56U) || (core.cpu.IR.low != 0x78U)) {
        return false;
    }
    memset(&core.cpu, 0, sizeof(core.cpu));

    for (uint32_t y = 0; y < KNOVGB_HEIGHT; y++) {
        core.core_lines[y] = core.core_frame[y];
    }

    core.memory.ROM = knovgbc_mvp_rom;
    core.memory.WRAM = core.wram;
    core.memory.VRAM = core.vram;
    core.memory.SRAM = core.unused_sram;
    core.memory.HRAM = core.hram;
    core.memory.OAM = core.oam;
    core.memory.BANK_ROM = 1U;
    core.memory.BANK_WRAM = 1U;
    core.memory.BANK_VRAM = 0U;
    core.memory.BANK_SRAM = 0U;
#if defined(KNOVGBC_SML_MBC1)
    core.memory.N_ROM = 4U;
#else
    core.memory.N_ROM = 2U;
#endif
    core.memory.N_SRAM = 1U;
#if defined(KNOVGBC_SML_MBC1)
    core.memory.ROM_MAPPER = 0x01U;
#else
    core.memory.ROM_MAPPER = 0U;
#endif
    core.memory.userdata = &core.fabric;
#if defined(KNOVGBC_SML_MBC1)
    core.memory.dispatch_ROM = pgf_cb_ROM_MBC1;
#else
    core.memory.dispatch_ROM = rom_only_write;
#endif
#if defined(KNOVGBC_SCHEDULER_BATCH)
    core.memory.dispatch_IO = picogb_mvp_io;
#else
    core.memory.dispatch_IO = pgf_cb_IO_;
#endif
#if PPU_DMG_FAST_RENDERER || PPU_DMG_DIRTY_SCANLINES || defined(KNOVGBC_SLAVE_PPU_SHADOW)
    core.memory.dispatch_VRAM_write = picogb_mvp_vram_write;
    core.memory.dispatch_OAM_write = picogb_mvp_oam_write;
#endif

    core.cpu.mi = &core.memory;
    micache_invalidate(&core.cpu.micache);
#if defined(KNOVGBC_IDLE_SPIN_CACHE)
    picogb_idle_spin_cache_reset();
#endif
#if defined(KNOVGBC_BLOCK_RECOMPILER)
    picogb_block_cache_init(&core.block_cache);
#endif
#if defined(KNOVGBC_BLOCK_STATIC)
    picogb_block_executor_cache_init(&core.block_executor_cache);
#endif

    ppu_initialize(&core.ppu);
    core.ppu.VRAM = core.vram;
    core.ppu.OAM = core.oam;
    core.ppu.state.framebuffer = core.core_lines;

    core.fabric.mb = &core.cpu;
    core.fabric.ppu = &core.ppu;
#if defined(KNOVGBC_AUDIO)
    apu_initialize(&core.apu);
    core.apu.outbuf = core.audio_samples;
    core.apu.outbuf_size = 4096U;
    core.fabric.apu = &core.apu;
#else
    core.fabric.apu = NULL;
#endif
    core.fabric.JOYP_RAW = 0U;
    core.fabric.JOYP = 0U;
    core.fabric.TIMER_CNT = 0U;

    /* DMG state immediately after its internal boot ROM has finished.  No
     * boot-ROM image is linked or executed in this MVP. */
    core.cpu.PC = 0x0100U;
    core.cpu.SP = 0xFFFEU;
    core.cpu.reg.A = 0x01U;
    core.cpu.reg.F = 0xB0U;
    core.cpu.reg.BC = 0x0013U;
    core.cpu.reg.DE = 0x00D8U;
    core.cpu.reg.HL = 0x014DU;
    core.cpu.IE = 0U;
    core.cpu.IF = 0U;
    core.cpu.IME = 0U;
    core.cpu.IME_ASK = 0U;
    core.cpu.IR.raw = 0U;
    core.cpu.DIV = 0x26E168U;
#if defined(KNOVGBC_AUDIO)
    core.apu.CTR_DIV = core.cpu.DIV;
    apu_write(&core.apu, 0x26U, 0x80U);
    apu_write(&core.apu, 0x24U, 0x77U);
    apu_write(&core.apu, 0x25U, 0xF3U);
#endif

    core.ppu.rLCDC = 0x91U;
    core.ppu.rSTAT = 0x85U;
    core.ppu.rBGP = 0xFCU;
    core.ppu.rOBP0 = 0xFFU;
    core.ppu.rOBP1 = 0xFFU;
#if defined(KNOVGBC_PPU_PALETTE_CACHE)
    ppu_dmg_palette_cache_refresh(&core.ppu);
#endif
    core.ppu.state.scanY = 0x90U;

    /* ppu_initialize starts mode 1 with a renderable phase.  Because the
     * post-BIOS state begins in VBlank, start at zero so the wrap to line 0
     * selects rendering for the first complete active frame. */
    core.ppu.state.interlace = 0U;
    core.pending_ppu_irq = 0U;
#if defined(KNOVGBC_SCHEDULER_BATCH)
    picogb_event_scheduler_init(&core.scheduler, &core.cpu, &core.ppu,
        &core.fabric);
#endif

    return true;
}

#if defined(KNOVGBC_BLOCK_RECOMPILER)
/* The block cache calls this after each translated guest instruction.  It is
 * the exact ordinary (non-batched) device sequence from the main CPU loop;
 * a block is admitted only when it ends strictly before the next PPU event
 * and the timer is disabled, so no hardware event can be hidden inside it. */
static void
picogb_block_commit(void *userdata, word cycles)
{
    (void)userdata;

    core.cpu.DIV += cycles;
    pgf_timer_update(&core.fabric, cycles);

    if (core.ppu.rLCDC & 0x80U) {
        core.pending_ppu_irq |= ppu_tick(&core.ppu, cycles << 2);
    }
}

static bool
picogb_try_block(void)
{
    struct picogb_block_runtime runtime;
    uint32_t safe_ppu_ticks = UINT32_MAX;

    /* The first native block family is deliberately linear and never changes
     * IME.  Entering with delayed IME semantics or an active Game Boy timer
     * would require additional per-opcode event deadlines, so those states
     * retain the reference interpreter for now. */
    if ((core.cpu.HALTING != 0U) || (core.cpu.IME != 0U) ||
        (core.cpu.IME_ASK != 0U) || (core.fabric.TIMER_CNT & 4U)) {
        return false;
    }

    if (core.ppu.rLCDC & 0x80U) {
        safe_ppu_ticks = core.ppu.next_update_ticks;
        if (safe_ppu_ticks <= 4U) {
            return false;
        }
    }

    runtime.userdata = NULL;
    runtime.commit = picogb_block_commit;
    runtime.instructions = 0U;
    runtime.cycles = 0U;

    return picogb_block_cache_try_execute(&core.block_cache, &core.cpu,
        &runtime, safe_ppu_ticks) != 0;
}
#endif

#if defined(KNOVGBC_BLOCK_STATIC)
/* The portable block executor commits its total device time once after a
 * block. Its admission guard below ensures that no PPU/timer event can occur
 * within that small straight-line sequence. Unlike kmvpr023, it neither
 * writes nor executes SH-2 instructions dynamically: it is just a small C
 * switch over a predecoded ROM plan. */
static void
picogb_block_static_commit(void *userdata, word cycles)
{
    (void)userdata;

    core.cpu.DIV += cycles;
    pgf_timer_update(&core.fabric, cycles);

    if (core.ppu.rLCDC & 0x80U) {
        core.pending_ppu_irq |= ppu_tick(&core.ppu, cycles << 2);
    }
}

static bool
picogb_try_block_static(void)
{
    struct picogb_block_executor_runtime runtime;
    uint32_t safe_ppu_ticks = UINT32_MAX;

    /* Keep the first C executor at the same conservative event boundary as
     * the reference loop. Timer-enabled, interrupt-visible and HALT states
     * remain with mb_exec(), so no event can occur between operations here. */
    if ((core.cpu.HALTING != 0U) || (core.cpu.IR.high != 0U) ||
#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
        ((core.cpu.IME != 0U) &&
            (mbh_irq_get_pending(&core.cpu) != 0U)) ||
#else
        (core.cpu.IME != 0U) ||
#endif
#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
        ((core.cpu.IME == 0U) && (core.cpu.IME_ASK != 0U))
#else
        (core.cpu.IME_ASK != 0U)
#endif
#if !defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
        || (core.fabric.TIMER_CNT & 4U)
#endif
        ) {
        return false;
    }

    if (core.ppu.rLCDC & 0x80U) {
        safe_ppu_ticks = core.ppu.next_update_ticks;
        if (safe_ppu_ticks <= 4U) {
            return false;
        }
    }

#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
    /* Register/HRAM-only blocks cannot observe TIMA. Still stop strictly
     * before its next overflow, which could request an interrupt. */
    if (core.fabric.TIMER_CNT & 4U) {
        const uint32_t step =
            pgf_timer_step_periods[core.fabric.TIMER_CNT & 3U];
        const uint32_t accum = core.fabric.TIMER_ACCUM;
        const uint32_t sub = core.fabric.TIMER_SUB;
        uint32_t timer_ticks;

        if ((accum > 0xFFU) || (sub >= step)) {
            return false;
        }
        timer_ticks = (((0x100U - accum) * step) - sub) * 4U;
        if (timer_ticks < safe_ppu_ticks) {
            safe_ppu_ticks = timer_ticks;
        }
    }
#endif

    runtime.userdata = NULL;
    runtime.commit = picogb_block_static_commit;
    runtime.instructions = 0U;
    runtime.cycles = 0U;

    return picogb_block_executor_try_execute(&core.block_executor_cache,
        &core.cpu, &runtime, safe_ppu_ticks) != 0;
}
#endif

static bool
picogb_frame_ready(bool *rendered)
{
    if (core.ppu._redrawed == 0U) {
        return false;
    }

    core.ppu._redrawed = 0U;
#if PPU_DMG_FAST_RENDERER
    *rendered =
#if !defined(KNOVGBC_NO_FRAMESKIP)
        ((core.ppu.state.interlace & 3U) == 3U) &&
#endif
        (core.ppu.dmg_frame_changed != 0U);
#else
#if defined(KNOVGBC_NO_FRAMESKIP)
    *rendered = true;
#else
    *rendered = (core.ppu.state.interlace & 3U) == 3U;
#endif
#endif
    return true;
}

static bool
picogb_run_logical_frame(bool *rendered)
{
    for (;;) {
        word cycles;

        /* Make the PPU event visible to the CPU before deciding whether a
         * halted Game Boy may sleep until another hardware event.  Keeping
         * it behind the CPU step can postpone VBlank/STAT by a whole line,
         * or retain it indefinitely if that step turns the LCD off. */
#if defined(KNOVGBC_SCHEDULER_BATCH)
        const word pending_ppu_irq =
            picogb_event_scheduler_take_ppu_irq(&core.scheduler);
        if (pending_ppu_irq != 0U) {
            core.cpu.IF |= pending_ppu_irq;
        }

        /* A HALTed CPU chooses its next step from live device state. Flush
         * an incomplete batch before calculating that boundary. */
        if (core.cpu.HALTING && (core.scheduler.pending_cycles != 0U)) {
            picogb_event_scheduler_flush(&core.scheduler);
            if (picogb_frame_ready(rendered)) {
                return true;
            }
            if (core.scheduler.pending_ppu_irq != 0U) {
                continue;
            }
        }
#else
        if (core.pending_ppu_irq != 0U) {
            core.cpu.IF |= core.pending_ppu_irq;
            core.pending_ppu_irq = 0U;
        }
#endif

#if defined(KNOVGBC_IDLE_SPIN)
        /* Skip a proven, read-only SM83 polling loop. 029 stops before the
         * event; 031 may finish its first event-crossing instruction. Both
         * commit devices and deliver their IRQ before any further opcode.
         * 032 may also resume the same verified loop at its A7/JR suffix. */
        if ((core.cpu.IR.raw == 0x00F0U)
#if defined(KNOVGBC_IDLE_RESUME)
            || (((core.cpu.IR.raw == 0x00A7U) ||
                    (core.cpu.IR.raw == 0x0028U)) && (core.cpu.reg.A == 0U))
#endif
            ) {
#if defined(KNOVGBC_HRAM_WAIT)
            if (picogb_hram_wait_try(&core.cpu, &core.ppu, &core.fabric,
                    &core.pending_ppu_irq, NULL, NULL) != 0U) {
                /* Device time is already committed by the dedicated wait. */
#if defined(KNOVGBC_AUDIO)
                picogb_audio_service(false);
#endif
                if (picogb_frame_ready(rendered)) return true;
                continue;
            }
#else
#if defined(KNOVGBC_IDLE_EVENT)
            const word cycles_skipped = picogb_idle_event_try(&core.cpu,
                &core.ppu, &core.fabric, NULL);
            if (cycles_skipped != 0U) {
#else
            const word iterations = picogb_idle_spin_try(&core.cpu,
                &core.ppu, &core.fabric);
            if (iterations != 0U) {
                const word cycles_skipped = iterations * 7U;
#endif
                core.cpu.DIV += cycles_skipped;
                pgf_timer_update(&core.fabric, cycles_skipped);
#if defined(KNOVGBC_AUDIO)
                apu_tick(&core.apu, cycles_skipped, 1);
                picogb_audio_service(false);
#endif
                core.pending_ppu_irq |= ppu_tick(&core.ppu,
                    cycles_skipped << 2);
                if (picogb_frame_ready(rendered)) {
                    return true;
                }
                continue;
            }
#endif /* KNOVGBC_HRAM_WAIT */
        }
#endif

#if defined(KNOVGBC_IDLE_COUNTDOWN)
        if (core.cpu.IR.raw == 0x003DU) {
            const word cycles_skipped = picogb_idle_countdown_try(&core.cpu,
                &core.ppu, &core.fabric, NULL);
            if (cycles_skipped != 0U) {
                core.cpu.DIV += cycles_skipped;
                pgf_timer_update(&core.fabric, cycles_skipped);
#if defined(KNOVGBC_AUDIO)
                apu_tick(&core.apu, cycles_skipped, 1);
                picogb_audio_service(false);
#endif
                core.pending_ppu_irq |= ppu_tick(&core.ppu, cycles_skipped << 2);
                if (picogb_frame_ready(rendered)) return true;
                continue;
            }
        }
#endif

#if defined(KNOVGBC_SUM_LOOP)
        if (core.cpu.IR.raw == 0x0019U) {
            const word grouped_cycles = picogb_sum_loop_try(&core.cpu,
                &core.ppu, &core.fabric, NULL);
            if (grouped_cycles != 0U) {
                core.cpu.DIV += grouped_cycles;
                pgf_timer_update(&core.fabric, grouped_cycles);
                core.pending_ppu_irq |= ppu_tick(&core.ppu, grouped_cycles << 2);
                if (picogb_frame_ready(rendered)) return true;
                continue;
            }
        }
#endif

        if (!core.cpu.HALTING || mbh_irq_get_pending(&core.cpu)) {
            if (core.cpu.HALTING) {
                core.cpu.HALTING = 0U;
            }
#if defined(KNOVGBC_OPCODE_PROFILE)
            /* This is deliberately outside mb_exec(): the diagnostic must
             * observe the real prefetched opcode without making the already
             * cache-sensitive interpreter any larger. An enabled interrupt
             * owns the step instead of that prefetched opcode. */
            if ((core.cpu.IME != 0U) &&
                (mbh_irq_get_pending(&core.cpu) != 0U)) {
                picogb_opcode_profile_interrupt_record();
            }
            else {
                picogb_opcode_profile_instruction_record(core.cpu.IR.low);
            }
#endif
#if defined(KNOVGBC_COMPONENT_PROFILE)
            const uint32_t cpu_profile_token =
                mvp_profiler_component_begin(MVP_PROFILER_COMPONENT_CPU);
#endif
#if defined(KNOVGBC_SH2_SM83_ASM)
            cycles = picogb_sm83_exec(&core.cpu);
#elif defined(KNOVGBC_SM83_COMPACT)
            cycles = picogb_sm83_compact_exec(&core.cpu);
#else
#if defined(KNOVGBC_BLOCK_RECOMPILER)
            if (picogb_try_block()) {
                /* Device time was committed after every translated SM83
                 * opcode.  Do not run the ordinary post-mb_exec commit a
                 * second time; restart at the same IRQ-visible boundary. */
                if (picogb_frame_ready(rendered)) {
                    return true;
                }
                continue;
            }
#endif
#if defined(KNOVGBC_BLOCK_STATIC)
            if (picogb_try_block_static()) {
                /* The portable C executor commits each opcode through the
                 * same timer/PPU sequence as the ordinary interpreter. */
                if (picogb_frame_ready(rendered)) {
                    return true;
                }
                continue;
            }
#endif
            cycles = mb_exec(&core.cpu);
#endif
#if defined(KNOVGBC_COMPONENT_PROFILE)
            mvp_profiler_component_end(MVP_PROFILER_COMPONENT_CPU,
                cpu_profile_token);
#endif
        }
        else if (core.ppu.rLCDC & 0x80U) {
            /* During HALT, jump directly to the next PPU event instead of
             * paying one host loop iteration per idle machine cycle. */
            cycles = core.ppu.next_update_ticks >> 2;
            if (cycles == 0U) {
                cycles = 1U;
            }
        }
        else if (core.fabric.TIMER_CNT & 4U) {
            /* A halted CPU only needs to wake for the timer overflow. TAC
             * selects 4/16/64/256 machine cycles per TIMA increment; account
             * for both that period and cycles already accumulated. */
            const word step_period =
                pgf_timer_step_periods[core.fabric.TIMER_CNT & 3U];
            cycles = ((0x100U - core.fabric.TIMER_ACCUM) * step_period) -
                core.fabric.TIMER_SUB;
        }
        else {
            cycles = 20U;
        }

        if (cycles == 0U) {
            return false;
        }

        if (core.cpu.HALTING && !core.cpu.IR.high) {
            core.cpu.IR.high = 1U;
        }

#if defined(KNOVGBC_SCHEDULER_BATCH)
        picogb_event_scheduler_advance(&core.scheduler, cycles);
        if (picogb_event_scheduler_flush_due(&core.scheduler)) {
            picogb_event_scheduler_flush(&core.scheduler);
        }
#else
        core.cpu.DIV += cycles;
#if defined(KNOVGBC_COMPONENT_PROFILE)
        const uint32_t timer_profile_token =
            mvp_profiler_component_begin(MVP_PROFILER_COMPONENT_TIMER);
#endif
        pgf_timer_update(&core.fabric, cycles);
#if defined(KNOVGBC_AUDIO)
        apu_tick(&core.apu, cycles, 1);
        picogb_audio_service(false);
#endif
#if defined(KNOVGBC_COMPONENT_PROFILE)
        mvp_profiler_component_end(MVP_PROFILER_COMPONENT_TIMER,
            timer_profile_token);
#endif
#endif /* KNOVGBC_SCHEDULER_BATCH */

#if !defined(KNOVGBC_SCHEDULER_BATCH)
        if (core.ppu.rLCDC & 0x80U) {
#if defined(KNOVGBC_COMPONENT_PROFILE)
            const uint32_t ppu_profile_token =
                mvp_profiler_component_begin(MVP_PROFILER_COMPONENT_PPU);
#endif
            core.pending_ppu_irq |= ppu_tick(&core.ppu, cycles << 2);
#if defined(KNOVGBC_COMPONENT_PROFILE)
            mvp_profiler_component_end(MVP_PROFILER_COMPONENT_PPU,
                ppu_profile_token);
#endif
        }
#endif

        if (picogb_frame_ready(rendered)) {
            return true;
        }
    }
}

static void
cache_configure(void)
{
    const uint8_t old_mask = cpu_intc_mask_get();

    cpu_intc_mask_set(15);
    cpu_cache_way_mode_set(CPU_CACHE_MODE_4_WAY);
    cpu_cache_purge();
#if defined(KNOVGBC_CACHE_DATA_NO_REPL)
    /* A/B cache experiment: retain instruction lines in the unified cache by
     * making data misses non-allocating. Existing data-cache hits remain
     * usable; the baseline build leaves data replacement enabled. */
    cpu_cache_repl_disable(CPU_CACHE_TYPE_D);
#endif
    cpu_intc_mask_set(old_mask);
}

int
main(void)
{
    /* Saroo firmware revisions may leave the SH-2 cache in 2-way/2 KiB RAM
     * mode. The emulator needs the full deterministic 4 KiB instruction/data
     * cache and never uses the cache-as-RAM feature. */
    cache_configure();

    saturn_platform_video_init();
    show_startup_marker(MVP_PIXEL_BLUE, 1U);

#if !defined(KNOVGBC_AUDIO)
    picogb_mvp_input_init();
#endif
    show_startup_marker(MVP_PIXEL_YELLOW, 2U);

    if (!embedded_rom_is_supported()) {
        startup_failure(3U);
    }
    show_startup_marker(MVP_PIXEL_MAGENTA, 3U);

    if (!picogb_core_init()) {
        startup_failure(4U);
    }
#if defined(KNOVGBC_SH2_SM83_ASM)
    if (!picogb_sm83_selftest(&core.cpu)) {
        startup_failure(6U);
    }
#endif
#if defined(KNOVGBC_BLOCK_RECOMPILER)
    if (!picogb_block_cache_selftest(&core.block_cache, &core.cpu)) {
        startup_failure(8U);
    }
#endif
    show_startup_marker(MVP_PIXEL_CYAN, 4U);

    saturn_platform_wait_vblank();
#if defined(KNOVGBC_VDP12_NATIVE)
    /* Keep the proven bitmap path for boot diagnostics, then switch video
     * hardware only after the emulator and its initial marker are ready. */
    if (!saturn_vdp12_init()) {
        startup_failure(9U);
    }
#endif
    mvp_profiler_init();
#if defined(KNOVGBC_AUDIO)
    /* SNDON/SNDOFF share SMPC with asynchronous pad INTBACK. Configure
     * audio only with a valid FRT clock and before starting pad requests. */
    if (!picogb_saturn_audio_init()) {
        startup_failure(10U);
    }
    picogb_mvp_input_init();
#endif

#if defined(KNOVGBC_SLAVE_PPU_SHADOW)
    /* Start the Slave only after the proven Master-only boot, VDP2 and FRT
     * setup are complete. A failed shadow handshake is explicit, rather than
     * leaving a black screen during startup. */
    if (!picogb_slave_ppu_shadow_init()) {
        startup_failure(7U);
    }
#endif

    for (;;) {
#if !defined(KNOVGBC_FPS_ONLY)
        const uint32_t frame_start = mvp_profiler_ticks_get();
#endif
        bool presented = false;

#if defined(KNOVGBC_SLAVE_PPU_SHADOW)
        picogb_slave_ppu_shadow_poll();
#endif

        picogb_mvp_input_update(&core.fabric);

#if defined(KNOVGBC_COMPONENT_PROFILE)
        mvp_profiler_component_frame_begin();
#endif
#if !defined(KNOVGBC_FPS_ONLY)
        const uint32_t core_start = mvp_profiler_ticks_get();
#endif
        if (!picogb_run_logical_frame(&presented)) {
            startup_failure(5U);
        }
#if defined(KNOVGBC_AUDIO)
        apu_flush(&core.apu, 1);
        picogb_audio_service(true);
#endif
#if !defined(KNOVGBC_FPS_ONLY)
        const uint32_t core_end = mvp_profiler_ticks_get();
#endif
#if defined(KNOVGBC_COMPONENT_PROFILE)
        mvp_profiler_component_frame_commit();
#endif

#if defined(KNOVGBC_SLAVE_PPU_SHADOW)
        /* The Slave receives a finished visual trace plus this immutable
         * Master reference. It cannot influence presentation or gameplay. */
        picogb_slave_ppu_shadow_frame_complete(&core.core_frame[0][0],
            PICOGB_FRAME_STRIDE, presented);
        picogb_slave_ppu_shadow_poll();
#endif

#if defined(KNOVGBC_FPS_ONLY)
        mvp_profiler_frame_pace();

        if (presented) {
#if defined(KNOVGBC_VDP12_NATIVE)
            saturn_vdp12_present(picogb_vdp12_frame_get());
#elif defined(KNOVGBC_VDP2_INDEXED8)
            saturn_platform_present_indexed8_strided(
#else
            saturn_platform_present_strided(
#endif
#if !defined(KNOVGBC_VDP12_NATIVE)
                &core.core_frame[0][PICOGB_VIEW_X],
                PICOGB_FRAME_STRIDE);
#endif
        }

        mvp_profiler_frame_record(0U, 0U, 0U, 0U, 0U, presented);
#else
        const uint32_t conversion_ticks = 0U;
        uint32_t upload_ticks = 0U;

        const uint32_t wait_ticks = mvp_profiler_frame_pace();

        if (presented) {
            const uint32_t upload_start = mvp_profiler_ticks_get();
#if defined(KNOVGBC_VDP12_NATIVE)
            saturn_vdp12_present(picogb_vdp12_frame_get());
#elif defined(KNOVGBC_VDP2_INDEXED8)
            saturn_platform_present_indexed8_strided(
#else
            saturn_platform_present_strided(
#endif
#if !defined(KNOVGBC_VDP12_NATIVE)
                &core.core_frame[0][PICOGB_VIEW_X],
                PICOGB_FRAME_STRIDE);
#endif
            upload_ticks = mvp_profiler_ticks_get() - upload_start;
        }

        const uint32_t frame_end = mvp_profiler_ticks_get();
        mvp_profiler_frame_record(frame_end - frame_start,
            core_end - core_start, conversion_ticks, wait_ticks,
            upload_ticks, presented);
#endif
    }
}
