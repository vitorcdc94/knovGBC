/*
 * Isolated PicoGB smoke test for the knovGBC MVP.
 *
 * By default it runs boot-ROM-less DMG Tetris. The opt-in MBC1 mode runs
 * Super Mario Land instead. Both print deterministic framebuffer/state hashes
 * without any Saturn dependency.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mi.h"
#include "microcode.h"
#if defined(KNOVGBC_SMOKE_MBC1)
#include "microcode_dispatch.h"
#endif
#include "ppu.h"
#include "fabric.h"
#if defined(KNOVGBC_VDP12_SMOKE_CHECK)
#include "picogb_vdp12_frame.h"
#endif
#if defined(KNOVGBC_OPCODE_PROFILE)
#include "picogb_opcode_profile.h"
#endif
#if defined(KNOVGBC_SM83_COMPACT)
#include "picogb_sm83_compact.h"
#endif
#if defined(KNOVGBC_SCHEDULER_BATCH)
#include "picogb_event_scheduler.h"
#endif
#if defined(KNOVGBC_IDLE_SPIN)
#include "picogb_idle_spin.h"
#endif
#if defined(KNOVGBC_HRAM_WAIT)
#include "picogb_hram_wait.h"
#endif
#if defined(KNOVGBC_BLOCK_RECOMPILER)
#include "picogb_block_cache.h"
#elif defined(KNOVGBC_BLOCK_STATIC)
#include "picogb_block_executor.h"
#endif

#if defined(KNOVGBC_BLOCK_RECOMPILER) || defined(KNOVGBC_BLOCK_STATIC)
#define KNOVGBC_BLOCK_EXECUTOR_TEST 1
#endif

#if defined(KNOVGBC_SMOKE_MBC1) || defined(KNOVGBC_SMOKE_GAMEPLAY_INPUT)
#define SMOKE_TEST_FRAMES 3600U
#elif defined(KNOVGBC_LDH_F0_PROFILE) || defined(KNOVGBC_SMOKE_LONG)
#define SMOKE_TEST_FRAMES 1800U
#else
#define SMOKE_TEST_FRAMES 600U
#endif

enum
{
    TEST_FRAMES = SMOKE_TEST_FRAMES,
    FRAMEBUFFER_ROWS = 145,
    FRAMEBUFFER_STRIDE = 256,
    VISIBLE_WIDTH = 160,
    VISIBLE_HEIGHT = 144,
    VISIBLE_X = 8
};

static r8 wram[0x8000];
static r8 vram[0x4000];
static r8 sram[0x20000];
static r8 hram[0x100];
static r8 oam[0x100];
static pixel_t framebuffer[FRAMEBUFFER_ROWS][FRAMEBUFFER_STRIDE];
static pixel_t *framebuffer_lines[FRAMEBUFFER_ROWS];

#if CONFIG_APU_ENABLE && defined(KNOVGBC_AUDIO)
static apu_t smoke_apu;
static int16_t smoke_audio_ring[4096];
static uint32_t smoke_audio_consumed;
static uint64_t smoke_audio_nonzero;
static uint64_t smoke_audio_hash = UINT64_C(14695981039346656037);
static int smoke_audio_min, smoke_audio_max;

static void smoke_audio_collect(void)
{
    if ((uint32_t)(smoke_apu.outbuf_pos - smoke_audio_consumed) > 4096U) {
        fprintf(stderr, "Audio producer overran host ring\n");
        exit(9);
    }
    while (smoke_audio_consumed != smoke_apu.outbuf_pos) {
        const int16_t sample = smoke_audio_ring[smoke_audio_consumed & 4095U];
        ++smoke_audio_consumed;
        if (sample != 0) ++smoke_audio_nonzero;
        if (sample < smoke_audio_min) smoke_audio_min = sample;
        if (sample > smoke_audio_max) smoke_audio_max = sample;
        /* Fixed byte order makes the PCM checksum host independent. */
        smoke_audio_hash ^= (uint8_t)sample;
        smoke_audio_hash *= UINT64_C(1099511628211);
        smoke_audio_hash ^= (uint8_t)((uint16_t)sample >> 8);
        smoke_audio_hash *= UINT64_C(1099511628211);
    }
}
#endif

#if defined(KNOVGBC_VDP12_SMOKE_CHECK)
static uint8_t vdp12_reconstructed[VISIBLE_HEIGHT][VISIBLE_WIDTH];
static unsigned vdp12_visual_frames;
static unsigned vdp12_invalid_frames;
static unsigned vdp12_sprite_frames;
static unsigned vdp12_sprite_strips;

static void
smoke_vdp12_hook(unsigned frame_number, const struct ppu_t *ppu)
{
    if ((ppu->state.interlace & 3U) != 3U) return;
    ++vdp12_visual_frames;
    const picogb_vdp12_frame_t *const frame = picogb_vdp12_frame_get();
    if (frame->valid == 0U) {
        ++vdp12_invalid_frames;
        if (vdp12_invalid_frames <= 5U) {
            fprintf(stderr, "vdp12_invalid frame=%u lcdc=%02X\n",
                frame_number, (unsigned)ppu->rLCDC);
        }
        return;
    }
    if (frame->sprite_count != 0U) ++vdp12_sprite_frames;
    vdp12_sprite_strips += frame->sprite_count;

    for (unsigned y = 0U; y < VISIBLE_HEIGHT; y++) {
        for (unsigned x = 0U; x < VISIBLE_WIDTH; x++) {
            const uint8_t *const tile = frame->bg_tiles[
                (y >> 3U) * PICOGB_VDP12_TILE_COLS + (x >> 3U)];
            const uint8_t packed = tile[((y & 7U) << 2U) +
                ((x & 7U) >> 1U)];
            const uint8_t shade = (x & 1U) ?
                (packed & 15U) : (packed >> 4U);
            if (shade < 1U || shade > 4U) {
                fprintf(stderr, "vdp12_bg_bad frame=%u x=%u y=%u value=%u\n",
                    frame_number, x, y, shade);
                exit(8);
            }
            vdp12_reconstructed[y][x] = (uint8_t)(shade - 1U);
        }
    }
    for (unsigned i = 0U; i < frame->sprite_count; i++) {
        const picogb_vdp12_sprite_strip_t *const strip =
            &frame->sprite_strips[i];
        if (strip->y >= VISIBLE_HEIGHT) {
            fprintf(stderr, "vdp12_sprite_y_bad frame=%u strip=%u\n",
                frame_number, i);
            exit(8);
        }
        for (unsigned p = 0U; p < 8U; p++) {
            const int x = (int)strip->x + (int)p;
            const uint8_t packed = strip->pixels[p >> 1U];
            const uint8_t shade = (p & 1U) ?
                (packed & 15U) : (packed >> 4U);
            if (shade > 4U) {
                fprintf(stderr, "vdp12_sprite_bad frame=%u strip=%u\n",
                    frame_number, i);
                exit(8);
            }
            if (shade != 0U && x >= 0 && x < VISIBLE_WIDTH) {
                vdp12_reconstructed[strip->y][x] = (uint8_t)(shade - 1U);
            }
        }
    }
    for (unsigned y = 0U; y < VISIBLE_HEIGHT; y++) {
        for (unsigned x = 0U; x < VISIBLE_WIDTH; x++) {
            const uint8_t software = framebuffer[y][VISIBLE_X + x];
            if (software != vdp12_reconstructed[y][x]) {
                fprintf(stderr,
                    "vdp12_mismatch frame=%u x=%u y=%u software=%u native=%u\n",
                    frame_number, x, y, software,
                    vdp12_reconstructed[y][x]);
                exit(8);
            }
        }
    }
}
#define KNOVGBC_SMOKE_FRAME_HOOK(frame_number) \
    smoke_vdp12_hook((frame_number), &ppu)
#endif

#if defined(KNOVGBC_BLOCK_EXECUTOR_TEST)
/* The candidate commits the accumulated device time after each admitted
 * block. The block guard keeps it strictly before a PPU event and with the
 * timer disabled, making this equal to the ordinary per-opcode integration. */
struct smoke_block_context {
    struct mb_state *mb;
    struct ppu_t *ppu;
    struct pgf_userdata_t *userdata;
    word *delayed_ppu_irq;
};

static struct smoke_block_context smoke_block_context;
#if defined(KNOVGBC_BLOCK_METRICS)
static uint64_t smoke_block_calls;
static uint64_t smoke_block_admissions;
static uint64_t smoke_block_hits;
static uint64_t smoke_block_instructions;
static uint64_t smoke_block_gameplay_hits;
static uint64_t smoke_block_gameplay_instructions;
static uint64_t smoke_block_ime_on;
static uint64_t smoke_block_ime_pending;
static uint64_t smoke_block_ime_ask;
static uint64_t smoke_block_ir_high;
static uint64_t smoke_block_timer_on;
static uint64_t smoke_block_ppu_soon;
#endif

static void
smoke_block_commit(void *opaque, word cycles)
{
    struct smoke_block_context *const context = opaque;

    context->mb->DIV += cycles;
    pgf_timer_update(context->userdata, cycles);
    if (context->ppu->rLCDC & 0x80U) {
        *context->delayed_ppu_irq |= ppu_tick(context->ppu, cycles << 2);
    }
}

static int
smoke_try_block(
#if defined(KNOVGBC_BLOCK_RECOMPILER)
    struct picogb_block_cache *cache,
#else
    struct picogb_block_executor_cache *cache,
#endif
    mb_state *mb, ppu_t *ppu, struct pgf_userdata_t *userdata,
    word *delayed_ppu_irq, uint64_t *instructions, uint64_t *cpu_cycles,
    unsigned frames)
{
#if defined(KNOVGBC_BLOCK_RECOMPILER)
    struct picogb_block_runtime runtime;
#else
    struct picogb_block_executor_runtime runtime;
#endif
    uint32_t safe_ppu_ticks;

#if defined(KNOVGBC_BLOCK_METRICS)
    ++smoke_block_calls;
    if (mb->IME != 0U) ++smoke_block_ime_on;
    if ((mb->IME != 0U) && mbh_irq_get_pending(mb))
        ++smoke_block_ime_pending;
    if (mb->IME_ASK != 0U) ++smoke_block_ime_ask;
    if (mb->IR.high != 0U) ++smoke_block_ir_high;
    if (userdata->TIMER_CNT & 4U) ++smoke_block_timer_on;
    if ((ppu->rLCDC & 0x80U) && (ppu->next_update_ticks <= 4U))
        ++smoke_block_ppu_soon;
#endif

    if (
#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
        ((mb->IME != 0U) && (mbh_irq_get_pending(mb) != 0U)) ||
#else
        (mb->IME != 0U) ||
#endif
#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
        ((mb->IME == 0U) && (mb->IME_ASK != 0U)) ||
#else
        (mb->IME_ASK != 0U) ||
#endif
        (mb->IR.high != 0U)
#if !defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
        || ((userdata->TIMER_CNT & 4U) != 0U)
#endif
        ||
        ((ppu->rLCDC & 0x80U) && (ppu->next_update_ticks <= 4U))) {
        return 0;
    }

    safe_ppu_ticks = (ppu->rLCDC & 0x80U) ?
        ppu->next_update_ticks : UINT32_MAX;
#if defined(KNOVGBC_BLOCK_IMMUTABLE_WINDOW)
    if (userdata->TIMER_CNT & 4U) {
        const uint32_t step =
            pgf_timer_step_periods[userdata->TIMER_CNT & 3U];
        const uint32_t accum = userdata->TIMER_ACCUM;
        const uint32_t sub = userdata->TIMER_SUB;
        uint32_t timer_ticks;

        if ((accum > 0xFFU) || (sub >= step)) {
            return 0;
        }
        timer_ticks = (((0x100U - accum) * step) - sub) * 4U;
        if (timer_ticks < safe_ppu_ticks) {
            safe_ppu_ticks = timer_ticks;
        }
    }
#endif
    smoke_block_context.mb = mb;
    smoke_block_context.ppu = ppu;
    smoke_block_context.userdata = userdata;
    smoke_block_context.delayed_ppu_irq = delayed_ppu_irq;
    runtime.userdata = &smoke_block_context;
    runtime.commit = smoke_block_commit;
    runtime.instructions = 0U;
    runtime.cycles = 0U;
#if defined(KNOVGBC_BLOCK_METRICS)
    ++smoke_block_admissions;
#endif

#if defined(KNOVGBC_BLOCK_RECOMPILER)
    if (!picogb_block_cache_try_execute(cache, mb, &runtime,
            safe_ppu_ticks)) {
#else
    if (!picogb_block_executor_try_execute(cache, mb, &runtime,
            safe_ppu_ticks)) {
#endif
        return 0;
    }

    *instructions += runtime.instructions;
    *cpu_cycles += runtime.cycles;
#if defined(KNOVGBC_BLOCK_METRICS)
    ++smoke_block_hits;
    smoke_block_instructions += runtime.instructions;
    if (frames >= 480U) {
        ++smoke_block_gameplay_hits;
        smoke_block_gameplay_instructions += runtime.instructions;
    }
#else
    (void)frames;
#endif
    return 1;
}
#endif

/* Host-test-only instrumentation for LDH A,(a8) / opcode F0.  The core
 * calls this after it has fetched a8, so the profile remains valid even when
 * code itself executes from a non-ROM memory region. */
#if defined(KNOVGBC_LDH_F0_PROFILE)
enum { LDH_F0_PHASES = 2 };
static uint64_t ldh_f0_haddr_counts[LDH_F0_PHASES][0x100];
static unsigned int ldh_f0_profile_phase;

void
knovgbc_ldh_f0_profile_record(word haddr)
{
    ++ldh_f0_haddr_counts[ldh_f0_profile_phase][haddr & 0xFFU];
}
#endif

#if defined(KNOVGBC_STACK_PROFILE)
/* The core calls this once per two-byte stack operation, before either the
 * direct path or the ordinary memory callbacks. Count the actual addresses,
 * including wraparound at FFFF, rather than inferring them from SP alone. */
enum { STACK_PROFILE_PHASES = 2, STACK_PROFILE_POP = 0, STACK_PROFILE_PUSH = 1 };
static uint64_t stack_pair_total[STACK_PROFILE_PHASES][2];
static uint64_t stack_pair_fixed_wram[STACK_PROFILE_PHASES][2];
static unsigned stack_profile_phase;

void
knovgbc_stack_profile_record(word first, word second, unsigned is_write)
{
    const unsigned direction = is_write != 0U ? STACK_PROFILE_PUSH : STACK_PROFILE_POP;

    ++stack_pair_total[stack_profile_phase][direction];
    if ((first >= 0xC000U) && (first <= 0xCFFFU) &&
        (second >= 0xC000U) && (second <= 0xCFFFU)) {
        ++stack_pair_fixed_wram[stack_profile_phase][direction];
    }
}
#endif

#if defined(KNOVGBC_SCHEDULER_BATCH)
static struct picogb_event_scheduler *smoke_scheduler;

static word
smoke_batch_io(void *userdata, word address, word data, word type)
{
    word result;

    picogb_event_scheduler_flush(smoke_scheduler);
    result = pgf_cb_IO_(userdata, address, data, type);
    picogb_event_scheduler_reschedule(smoke_scheduler);
    return result;
}
#endif

#if defined(KNOVGBC_PPU_FULL_OPT) || defined(KNOVGBC_PPU_DIRTY_SCANLINES)
/* Exercise the exact cache-invalidation path used by the Saturn frontend:
 * CPU writes reach the PPU only through mi_dispatch callbacks. */
#if defined(KNOVGBC_PPU_DIRTY_SCANLINES)
static uint64_t smoke_dirty_vram_writes;
static uint64_t smoke_dirty_oam_writes;
static uint64_t smoke_dirty_vram_pattern_writes;
static uint64_t smoke_dirty_vram_map9800_writes;
static uint64_t smoke_dirty_vram_map9c00_writes;
static uint64_t smoke_dirty_bg_map_row_invalidations;
static uint64_t smoke_dirty_window_map_full_invalidations;
static uint64_t smoke_dirty_io_lcdc_writes;
static uint64_t smoke_dirty_io_scroll_writes;
static uint64_t smoke_dirty_io_scroll_unchanged;
static uint64_t smoke_dirty_io_window_writes;
static uint64_t smoke_dirty_io_palette_writes;
static uint64_t smoke_dirty_io_dma_writes;
static const unsigned *smoke_dirty_frame_counter;
static uint64_t smoke_dirty_pattern_by_phase[3];
static uint64_t smoke_dirty_bg_map_by_phase[3];
static uint64_t smoke_dirty_oam_callbacks_by_phase[3];
static uint64_t smoke_dirty_oam_entries_changed_by_phase[3];
static uint64_t smoke_dirty_io_full_by_phase[3];
static uint64_t smoke_dirty_full_by_phase[3];

static unsigned smoke_dirty_phase(void)
{
    const unsigned frame = *smoke_dirty_frame_counter;
    return frame < 600U ? 0U : frame < 1800U ? 1U : 2U;
}

static word smoke_dirty_io(void *userdata, word address, word data, word type)
{
    if (MB_TYPE_IS_WRITE(type)) {
        struct pgf_userdata_t *const fabric = userdata;
        const unsigned phase = smoke_dirty_phase();
        switch (address) {
            case 0x40U:
                smoke_dirty_io_lcdc_writes++;
                smoke_dirty_io_full_by_phase[phase]++;
                smoke_dirty_full_by_phase[phase]++;
                break;
            case 0x42U:
                smoke_dirty_io_scroll_writes++;
                if (fabric->ppu->rSCY == (data & 0xFFU))
                    smoke_dirty_io_scroll_unchanged++;
                else {
                    smoke_dirty_io_full_by_phase[phase]++;
                    smoke_dirty_full_by_phase[phase]++;
                }
                break;
            case 0x43U:
                smoke_dirty_io_scroll_writes++;
                if (fabric->ppu->rSCX == (data & 0xFFU))
                    smoke_dirty_io_scroll_unchanged++;
                else {
                    smoke_dirty_io_full_by_phase[phase]++;
                    smoke_dirty_full_by_phase[phase]++;
                }
                break;
            case 0x4AU:
                smoke_dirty_io_window_writes++;
                if (fabric->ppu->rWY != (data & 0xFFU)) {
                    smoke_dirty_io_full_by_phase[phase]++;
                    smoke_dirty_full_by_phase[phase]++;
                }
                break;
            case 0x4BU:
                smoke_dirty_io_window_writes++;
                if (fabric->ppu->rWX != (data & 0xFFU)) {
                    smoke_dirty_io_full_by_phase[phase]++;
                    smoke_dirty_full_by_phase[phase]++;
                }
                break;
            case 0x47U:
                smoke_dirty_io_palette_writes++;
                if (fabric->ppu->rBGP != (data & 0xFFU)) {
                    smoke_dirty_io_full_by_phase[phase]++;
                    smoke_dirty_full_by_phase[phase]++;
                }
                break;
            case 0x48U:
                smoke_dirty_io_palette_writes++;
                if (fabric->ppu->rOBP0 != (data & 0xFFU)) {
                    smoke_dirty_io_full_by_phase[phase]++;
                    smoke_dirty_full_by_phase[phase]++;
                }
                break;
            case 0x49U:
                smoke_dirty_io_palette_writes++;
                if (fabric->ppu->rOBP1 != (data & 0xFFU)) {
                    smoke_dirty_io_full_by_phase[phase]++;
                    smoke_dirty_full_by_phase[phase]++;
                }
                break;
            case 0x46U: smoke_dirty_io_dma_writes++; break;
            default: break;
        }
    }
#if defined(KNOVGBC_SCHEDULER_BATCH)
    return smoke_batch_io(userdata, address, data, type);
#else
    return pgf_cb_IO_(userdata, address, data, type);
#endif
}
#endif
static void smoke_vram_write(void *userdata, word offset)
{
    struct pgf_userdata_t *const fabric = userdata;
#if defined(KNOVGBC_PPU_DIRTY_SCANLINES)
    smoke_dirty_vram_writes++;
    if (offset < 0x1800U) {
        smoke_dirty_vram_pattern_writes++;
        smoke_dirty_pattern_by_phase[smoke_dirty_phase()]++;
        smoke_dirty_full_by_phase[smoke_dirty_phase()]++;
    }
    else {
        const word lcdc = fabric->ppu->rLCDC;
        const word map_base = offset & 0x1C00U;
        if (map_base == 0x1800U)
            smoke_dirty_vram_map9800_writes++;
        else
            smoke_dirty_vram_map9c00_writes++;

        if (map_base == (0x1800U | ((lcdc & 0x08U) << 7)))
            smoke_dirty_bg_map_row_invalidations++;
        if (map_base == (0x1800U | ((lcdc & 0x08U) << 7)))
            smoke_dirty_bg_map_by_phase[smoke_dirty_phase()]++;
        if ((lcdc & 0x20U) && (fabric->ppu->rWY < 144U) &&
            (fabric->ppu->rWX < 168U) &&
            (map_base == (0x1800U | ((lcdc & 0x40U) << 4))))
        {
            smoke_dirty_window_map_full_invalidations++;
            smoke_dirty_full_by_phase[smoke_dirty_phase()]++;
        }
    }
#endif
    ppu_dmg_on_vram_write(fabric->ppu, offset);
}

static void smoke_oam_write(void *userdata, word offset)
{
    struct pgf_userdata_t *const fabric = userdata;
#if defined(KNOVGBC_PPU_DIRTY_SCANLINES)
    smoke_dirty_oam_writes++;
    smoke_dirty_oam_callbacks_by_phase[smoke_dirty_phase()]++;
    if ((offset < 160U) && !fabric->ppu->dmg_indexed_oam_valid) {
        smoke_dirty_full_by_phase[smoke_dirty_phase()]++;
    }
    else if (offset < 160U) {
        const word first = offset == 0U ? 0U : (offset >> 2);
        const word end = offset == 0U ? 40U : first + 1U;
        for (word object = first; object != end; object++) {
            const word position = object << 2;
            if (memcmp(&fabric->ppu->dmg_indexed_oam_shadow[position],
                &fabric->ppu->OAM[position], 4U) != 0) {
                smoke_dirty_oam_entries_changed_by_phase[
                    smoke_dirty_phase()]++;
            }
        }
    }
#endif
    ppu_dmg_on_oam_write(fabric->ppu, offset);
}
#endif

static uint64_t fnv1a_byte(uint64_t hash, uint8_t value)
{
    return (hash ^ value) * UINT64_C(1099511628211);
}

static uint64_t fnv1a_u16(uint64_t hash, uint16_t value)
{
    hash = fnv1a_byte(hash, (uint8_t)value);
    return fnv1a_byte(hash, (uint8_t)(value >> 8));
}

static uint64_t fnv1a_u32(uint64_t hash, uint32_t value)
{
    hash = fnv1a_u16(hash, (uint16_t)value);
    return fnv1a_u16(hash, (uint16_t)(value >> 16));
}

static uint64_t fnv1a_bytes(uint64_t hash, const r8 *data, size_t size)
{
    size_t i;
    for (i = 0; i < size; ++i)
        hash = fnv1a_byte(hash, data[i]);
    return hash;
}

static int validate_register_layout(void)
{
    struct mb_state mb;
    memset(&mb, 0, sizeof(mb));

    mb.reg.BC = 0x1234;
    mb.reg.DE = 0x5678;
    mb.reg.HL = 0x9ABC;
    mb.reg.FA = 0xB001;

    return mb.reg.B == 0x12 && mb.reg.C == 0x34 &&
           mb.reg.D == 0x56 && mb.reg.E == 0x78 &&
           mb.reg.H == 0x9A && mb.reg.L == 0xBC &&
           mb.reg.F == 0xB0 && mb.reg.A == 0x01 &&
           MB_REG8(&mb, 1) == 0x12 && MB_REG8(&mb, 0) == 0x34 &&
           MB_REG8(&mb, 6) == 0x01;
}

static r8 *load_rom(const char *path, size_t *padded_size)
{
    FILE *file;
    long file_size;
    size_t capacity;
    r8 *rom;

    file = fopen(path, "rb");
    if (file == NULL)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return NULL;
    }

    capacity = 0x8000;
    while (capacity < (size_t)file_size)
        capacity <<= 1;

    rom = (r8 *)malloc(capacity);
    if (rom == NULL)
    {
        fclose(file);
        return NULL;
    }
    memset(rom, 0xFF, capacity);
    if (fread(rom, 1, (size_t)file_size, file) != (size_t)file_size)
    {
        free(rom);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *padded_size = capacity;
    return rom;
}

static uint64_t framebuffer_hash(void)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    unsigned y;
    unsigned x;
#if defined(KNOVGBC_VDP2_INDEXED8)
    static const uint16_t rgb1555_shades[4] = {
        0x0000U, 0x294AU, 0x56B5U, 0x7FFFU
    };
#endif

    for (y = 0; y < VISIBLE_HEIGHT; ++y)
        for (x = 0; x < VISIBLE_WIDTH; ++x)
        {
            uint16_t pixel = (uint16_t)framebuffer[y][VISIBLE_X + x];
#if defined(KNOVGBC_VDP2_INDEXED8)
            if (pixel >= 4U) {
                fprintf(stderr, "Indexed PPU pixel out of range at (%u,%u): %u\n",
                    x, y, (unsigned)pixel);
                exit(7);
            }
            pixel = rgb1555_shades[pixel];
#endif
            hash = fnv1a_u16(hash, pixel);
        }
    return hash;
}

#if defined(KNOVGBC_SMOKE_MBC1) && defined(KNOVGBC_SMOKE_PGM_CAPTURE)
static void smoke_save_pgm(unsigned frame, const char *directory)
{
    char path[1024];
    const int length = snprintf(path, sizeof(path),
        "%s/sml_mbc1_frame_%04u.pgm", directory, frame);
    if (length < 0 || (size_t)length >= sizeof(path)) {
        fputs("PGM capture path too long\n", stderr);
        exit(14);
    }

    FILE *const file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "Cannot write PGM capture: %s\n", path);
        exit(14);
    }
    if (fprintf(file, "P5\n160 144\n255\n") < 0) {
        fputs("Cannot write PGM header\n", stderr);
        fclose(file);
        exit(14);
    }
    for (unsigned y = 0U; y < VISIBLE_HEIGHT; ++y) {
        for (unsigned x = 0U; x < VISIBLE_WIDTH; ++x) {
            const pixel_t shade = framebuffer[y][VISIBLE_X + x];
            if (shade > 3U || fputc((int)(shade * 85U), file) == EOF) {
                fputs("Cannot write indexed PGM pixel\n", stderr);
                fclose(file);
                exit(14);
            }
        }
    }
    if (fclose(file) != 0) {
        fputs("Cannot finish PGM capture\n", stderr);
        exit(14);
    }
}
#endif

static uint64_t machine_hash(const struct mb_state *mb,
                             const struct mi_dispatch *memory,
                             const struct ppu_t *ppu,
                             const struct pgf_userdata_t *fabric)
{
    uint64_t hash = UINT64_C(14695981039346656037);

    hash = fnv1a_u16(hash, mb->reg.BC);
    hash = fnv1a_u16(hash, mb->reg.DE);
    hash = fnv1a_u16(hash, mb->reg.HL);
    hash = fnv1a_byte(hash, mb->reg.A);
    hash = fnv1a_byte(hash, mb->reg.F);
    hash = fnv1a_u16(hash, mb->PC);
    hash = fnv1a_u16(hash, mb->SP);
    hash = fnv1a_byte(hash, mb->IE);
    hash = fnv1a_byte(hash, mb->IF);
    hash = fnv1a_byte(hash, mb->IME);
    hash = fnv1a_byte(hash, mb->IME_ASK);
    hash = fnv1a_u16(hash, mb->IR.raw);
    hash = fnv1a_u16(hash, mb->IMM.raw);
    hash = fnv1a_u32(hash, (uint32_t)mb->HALTING);
    hash = fnv1a_u32(hash, mb->DIV);
    hash = fnv1a_u16(hash, mb->FR1);
    hash = fnv1a_u16(hash, mb->FR2);
    hash = fnv1a_u32(hash, (uint32_t)mb->FMC_MODE);
    hash = fnv1a_byte(hash, (uint8_t)ppu->state.scanY);
    hash = fnv1a_byte(hash, (uint8_t)ppu->rLCDC);
    hash = fnv1a_u32(hash, (uint32_t)ppu->next_update_ticks);
    hash = fnv1a_u32(hash, (uint32_t)ppu->next_update_n);
    hash = fnv1a_byte(hash, (uint8_t)ppu->rSTAT);
    hash = fnv1a_byte(hash, (uint8_t)ppu->rLYC);
    hash = fnv1a_byte(hash, (uint8_t)ppu->IF_SCHED);
    hash = fnv1a_u32(hash, (uint32_t)fabric->TIMER_SUB);
    hash = fnv1a_u32(hash, (uint32_t)fabric->TIMER_CNT);
    hash = fnv1a_u32(hash, (uint32_t)fabric->TIMER_ACCUM);
    hash = fnv1a_u32(hash, (uint32_t)fabric->TIMER_LOAD);
#if defined(KNOVGBC_SMOKE_MBC1)
    hash = fnv1a_u32(hash, (uint32_t)memory->BANK_ROM);
#endif
    hash = fnv1a_bytes(hash, memory->WRAM, 0x2000);
    hash = fnv1a_bytes(hash, memory->VRAM, 0x2000);
    hash = fnv1a_bytes(hash, memory->OAM, 0xA0);
    return hash;
}

int main(int argc, char **argv)
{
    const char *rom_path = argc > 1 ? argv[1] : "roms/tetris.gb";
    struct mi_dispatch memory;
    struct mb_state mb;
    struct ppu_t ppu;
    struct pgf_userdata_t userdata;
    r8 *rom;
    size_t rom_size;
    uint64_t instructions = 0;
    uint64_t cpu_cycles = 0;
#if defined(KNOVGBC_HRAM_WAIT)
    uint64_t hram_wait_calls = 0;
#endif
#if defined(KNOVGBC_IDLE_COUNTDOWN)
    uint64_t countdown_instructions = 0, countdown_batches = 0;
#endif
#if defined(KNOVGBC_SUM_LOOP)
    uint64_t sum_instructions = 0, sum_batches = 0;
#endif
#if defined(KNOVGBC_IDLE_STAT)
    uint64_t stat_instructions = 0, stat_batches = 0;
#endif
#if defined(KNOVGBC_IDLE_SPIN)
#if defined(KNOVGBC_IDLE_EVENT)
    uint64_t idle_event_instructions = 0;
    uint64_t idle_event_batches = 0;
#if defined(KNOVGBC_IDLE_RESUME)
    uint64_t idle_resume_a7 = 0, idle_resume_jr = 0;
#endif
#else
    uint64_t idle_skipped_trips = 0;
#endif
#endif
#if !defined(KNOVGBC_SCHEDULER_BATCH)
    word delayed_ppu_irq = 0;
#else
    struct picogb_event_scheduler scheduler;
#endif
#if defined(KNOVGBC_BLOCK_RECOMPILER)
    struct picogb_block_cache block_cache;
#elif defined(KNOVGBC_BLOCK_STATIC)
    struct picogb_block_executor_cache block_cache;
#endif
    unsigned frames = 0;
    unsigned i;
#if defined(KNOVGBC_PPU_DIRTY_SCANLINES)
    smoke_dirty_frame_counter = &frames;
#endif

    if (!validate_register_layout())
    {
        fprintf(stderr, "PicoGB register layout does not match target byte order\n");
        return 2;
    }

    rom = load_rom(rom_path, &rom_size);
    if (rom == NULL)
    {
        fprintf(stderr, "Cannot read ROM: %s\n", rom_path);
        return 3;
    }
#if defined(KNOVGBC_SMOKE_MBC1) && defined(KNOVGBC_SMOKE_PGM_CAPTURE)
    if (argc < 3) {
        fputs("MBC1 PGM capture requires an output directory argument\n", stderr);
        free(rom);
        return 3;
    }
#endif
#if defined(KNOVGBC_SMOKE_MBC1)
    if (rom_size != 0x10000U ||
        memcmp(&rom[0x134], "SUPER MARIOLAND", 15) != 0 ||
        (rom[0x143] & 0x80U) != 0U || rom[0x147] != 0x01U ||
        rom[0x148] != 0x01U || rom[0x149] != 0x00U)
    {
        fprintf(stderr, "Expected a 64 KiB DMG MBC1 Super Mario Land image without RAM\n");
        free(rom);
        return 4;
    }
#else
    if (rom_size < 0x8000 || memcmp(&rom[0x134], "TETRIS", 6) != 0 ||
        rom[0x143] == 0xC0 || rom[0x147] != 0x00)
    {
        fprintf(stderr, "Expected a ROM-only DMG Tetris image\n");
        free(rom);
        return 4;
    }
#endif

    memset(&memory, 0, sizeof(memory));
    memset(&mb, 0, sizeof(mb));
    memset(&ppu, 0, sizeof(ppu));
    memset(&userdata, 0, sizeof(userdata));
    memset(wram, 0, sizeof(wram));
    memset(vram, 0, sizeof(vram));
    memset(sram, 0, sizeof(sram));
    memset(hram, 0, sizeof(hram));
    memset(oam, 0, sizeof(oam));
    memset(framebuffer, 0, sizeof(framebuffer));

    for (i = 0; i < FRAMEBUFFER_ROWS; ++i)
        framebuffer_lines[i] = framebuffer[i];

    memory.ROM = rom;
    memory.WRAM = wram;
    memory.VRAM = vram;
    memory.SRAM = sram;
    memory.HRAM = hram;
    memory.OAM = oam;
    memory.BANK_ROM = 1;
    memory.BANK_WRAM = 1;
    memory.BANK_VRAM = 0;
    memory.BANK_SRAM = 0;
    memory.N_ROM = (var)(rom_size >> 14);
    memory.N_SRAM = 1;

    mb.mi = &memory;
    micache_invalidate(&mb.micache);
#if defined(KNOVGBC_IDLE_SPIN_CACHE)
    picogb_idle_spin_cache_reset();
#endif
#if defined(KNOVGBC_BLOCK_RECOMPILER)
    picogb_block_cache_init(&block_cache);
#elif defined(KNOVGBC_BLOCK_STATIC)
    picogb_block_executor_cache_init(&block_cache);
#endif
    mi_init_params_from_header(&memory, rom);
    mb.PC = 0x0100;
    mb.SP = 0xFFFE;
    mb.reg.A = 0x01;
    mb.reg.BC = 0x0013;
    mb.reg.DE = 0x00D8;
    mb.reg.HL = 0x014D;
    mb.reg.F = 0xB0;
    mb.DIV = 0x26E168;

    ppu_initialize(&ppu);
    ppu.VRAM = memory.VRAM;
    ppu.OAM = memory.OAM;
    ppu.state.framebuffer = framebuffer_lines;

#if defined(KNOVGBC_SATURN_CONFIG_TEST)
    /* Mirror the no-boot-ROM state and scheduling policy used in the Saturn
     * MVP rather than silently testing RGB565/no-frameskip defaults. */
    ppu.rLCDC = 0x91;
    ppu.rSTAT = 0x85;
    ppu.rBGP = 0xFC;
    ppu.rOBP0 = 0xFF;
    ppu.rOBP1 = 0xFF;
#if defined(KNOVGBC_PPU_PALETTE_CACHE)
    ppu_dmg_palette_cache_refresh(&ppu);
#endif
    ppu.state.scanY = 0x90;
    ppu.state.interlace = 0U;
#else
    ppu.rLCDC = 0x91;
    ppu.rSTAT = 0x85;
    ppu.state.scanY = 0x90;
#endif

    userdata.mb = &mb;
    userdata.ppu = &ppu;
#if CONFIG_APU_ENABLE && defined(KNOVGBC_AUDIO)
    apu_initialize(&smoke_apu);
    smoke_apu.outbuf = smoke_audio_ring;
    smoke_apu.outbuf_size = 4096U;
    smoke_apu.CTR_DIV = mb.DIV;
    apu_write(&smoke_apu, 0x26U, 0x80U);
    apu_write(&smoke_apu, 0x24U, 0x77U);
    apu_write(&smoke_apu, 0x25U, 0xF3U);
    userdata.apu = &smoke_apu;
#else
    userdata.apu = NULL;
#endif
    memory.userdata = &userdata;
#if defined(KNOVGBC_SCHEDULER_BATCH)
    picogb_event_scheduler_init(&scheduler, &mb, &ppu, &userdata);
    smoke_scheduler = &scheduler;
    memory.dispatch_IO = smoke_batch_io;
#else
    memory.dispatch_IO = pgf_cb_IO_;
#endif
#if defined(KNOVGBC_PPU_DIRTY_SCANLINES)
    memory.dispatch_IO = smoke_dirty_io;
#endif
    memory.dispatch_ROM = pgf_cb_ROM_;
#if defined(KNOVGBC_SMOKE_MBC1)
    /* Exercise the same public memory dispatch used by SM83 instructions.
     * Both the data and opcode paths must see the newly selected bank, and
     * mapper writes must evict any stale page-cache entries.  Restore the
     * power-on bank and PC before running the game. */
    for (unsigned bank = 2U; bank <= 3U; ++bank)
    {
        mb.micache.mc_execute[4] = rom;
        mb.micache.mc_read[7] = rom;
        mb.micache.mc_write[5] = wram;
        mch_memory_dispatch_write(&mb, 0x2000U, bank);
        if (memory.BANK_ROM != bank ||
            mb.micache.mc_execute[4] != NULL ||
            mb.micache.mc_read[7] != NULL ||
            mb.micache.mc_write[5] != NULL ||
            mch_memory_dispatch_read(&mb, 0x4000U) != rom[bank << 14] ||
            mch_memory_dispatch_read(&mb, 0x7FFFU) !=
                rom[(bank << 14) | 0x3FFFU])
        {
            fprintf(stderr, "MBC1 data/cache bank %u contract failed\n", bank);
            free(rom);
            return 5;
        }
        mb.PC = 0x4000U;
        if (mch_memory_fetch_PC(&mb) != rom[bank << 14] ||
            mb.PC != 0x4001U)
        {
            fprintf(stderr, "MBC1 opcode bank %u contract failed\n", bank);
            free(rom);
            return 5;
        }
    }
    mch_memory_dispatch_write(&mb, 0x2000U, 0U);
    if (memory.BANK_ROM != 1U) {
        fprintf(stderr, "MBC1 forbidden bank zero remap failed\n");
        free(rom);
        return 5;
    }
    mb.PC = 0x0100U;
#endif
#if defined(KNOVGBC_PPU_FULL_OPT) || defined(KNOVGBC_PPU_DIRTY_SCANLINES)
    memory.dispatch_VRAM_write = smoke_vram_write;
    memory.dispatch_OAM_write = smoke_oam_write;
#endif

#if defined(KNOVGBC_OPCODE_PROFILE)
    /* Match the Saturn frontend: the profile's nonzero sampling seed must be
     * installed before the first CPU memory transaction. */
    picogb_opcode_profile_reset();
#endif

    while (frames < TEST_FRAMES)
    {
        word cycles;
#if defined(KNOVGBC_HRAM_WAIT)
        int hram_committed = 0;
        word hram_groups = 0;
#endif
#if defined(KNOVGBC_IDLE_SPIN)
        word idle_iterations = 0U;
#endif
#if defined(KNOVGBC_BLOCK_EXECUTOR_TEST)
        int block_executed = 0;
#endif
#if !defined(KNOVGBC_SCHEDULER_BATCH)
        word ppu_irq;
#endif

        /* The ordinary smoke test uses one short START press.  The profiling
         * build advances deterministically through Tetris's menus so it also
         * samples sustained play, not only the title/menu loop. */
        {
            word new_joyp;

#if defined(KNOVGBC_SMOKE_MBC1)
            /* One START pulse, then deterministic side-scrolling and jumps.
             * The controls are not tied to a particular PC or ROM offset. */
            new_joyp = (frames >= 180U && frames < 186U) ? 0x08U : 0U;
            if (frames >= 360U) {
                const unsigned phase = frames % 96U;
                new_joyp = (phase < 72U) ? 0x10U : 0U;
                if (phase >= 15U && phase < 21U) new_joyp |= 0x01U;
            }
#elif defined(KNOVGBC_LDH_F0_PROFILE) || defined(KNOVGBC_SMOKE_LONG)
#if defined(KNOVGBC_LDH_F0_PROFILE)
            ldh_f0_profile_phase = (frames >= 480U) ? 1U : 0U;
#endif
#if defined(KNOVGBC_STACK_PROFILE)
            stack_profile_phase = (frames >= 480U) ? 1U : 0U;
#endif
            new_joyp = ((frames >= 300U && frames < 304U) ||
                        (frames >= 360U && frames < 364U) ||
                        (frames >= 420U && frames < 424U)) ? 0x08 : 0;
#if defined(KNOVGBC_SMOKE_GAMEPLAY_INPUT)
            /* Deterministic movement/rotation/drop, plus pause/resume. */
            if (frames >= 480U) {
                const unsigned phase = frames % 120U;
                new_joyp = phase < 20U ? 0x20U :
                    phase < 40U ? 0x10U : phase < 44U ? 0x01U :
                    phase < 48U ? 0x02U : phase < 100U ? 0x80U : 0U;
                if ((frames >= 900U && frames < 904U) ||
                    (frames >= 960U && frames < 964U)) new_joyp = 0x08U;
            }
#endif
#else
            new_joyp = (frames >= 300 && frames < 304) ? 0x08 : 0;
#endif
            if (new_joyp & ~userdata.JOYP_RAW)
                mb.IF |= 0x10;
            userdata.JOYP_RAW = new_joyp;
        }

#if defined(KNOVGBC_SCHEDULER_BATCH)
        {
            word pending_ppu_irq = picogb_event_scheduler_take_ppu_irq(&scheduler);
            if (pending_ppu_irq)
                mb.IF |= pending_ppu_irq;
        }

        if (mb.HALTING && scheduler.pending_cycles)
        {
            picogb_event_scheduler_flush(&scheduler);
            if (ppu._redrawed)
            {
                ppu._redrawed = 0;
                ++frames;
#if defined(KNOVGBC_SMOKE_FRAME_HOOK)
                KNOVGBC_SMOKE_FRAME_HOOK(frames);
#endif
                continue;
            }
            if (scheduler.pending_ppu_irq)
                continue;
        }
#else
        if (delayed_ppu_irq)
        {
            mb.IF |= delayed_ppu_irq;
            delayed_ppu_irq = 0;
        }
#endif

        if (!mb.HALTING || mbh_irq_get_pending(&mb))
        {
            if (mb.HALTING)
                mb.HALTING = 0;
#if defined(KNOVGBC_OPCODE_PROFILE)
            if ((mb.IME != 0U) && (mbh_irq_get_pending(&mb) != 0U))
                picogb_opcode_profile_interrupt_record();
            else
                picogb_opcode_profile_instruction_record(mb.IR.low);
#endif
#if defined(KNOVGBC_SM83_COMPACT)
            cycles = picogb_sm83_compact_exec(&mb);
#else
#if defined(KNOVGBC_IDLE_SPIN)
#if defined(KNOVGBC_IDLE_EVENT)
#if defined(KNOVGBC_IDLE_RESUME)
            const word idle_entry = mb.IR.raw;
            cycles = 0U;
            /* Mirror the Saturn frontend's cheap opcode admission gate. */
            if ((mb.IR.raw == 0x00F0U) ||
                (((mb.IR.raw == 0x00A7U) || (mb.IR.raw == 0x0028U)) &&
                    (mb.reg.A == 0U)))
#endif
#if defined(KNOVGBC_HRAM_WAIT)
            cycles = picogb_hram_wait_try(&mb, &ppu, &userdata,
                &delayed_ppu_irq, &idle_iterations, &hram_groups);
#else
            cycles = picogb_idle_event_try(&mb, &ppu, &userdata,
                &idle_iterations);
#endif
            if (cycles != 0U) {
#if defined(KNOVGBC_IDLE_RESUME)
                if (idle_entry == 0x00A7U) ++idle_resume_a7;
                if (idle_entry == 0x0028U) ++idle_resume_jr;
#endif
#if defined(KNOVGBC_IDLE_STAT)
                /* HRAM wait success always leaves A=0; STAT success never
                 * does (full STAT or mode 1..3). This counts only on host. */
                if (mb.reg.A != 0U) {
                    stat_instructions += idle_iterations;
                    ++stat_batches;
                }
#endif
                instructions += idle_iterations;
                idle_event_instructions += idle_iterations;
#if defined(KNOVGBC_HRAM_WAIT)
                hram_committed = 1;
                ++hram_wait_calls;
                idle_event_batches += hram_groups;
#else
                ++idle_event_batches;
#endif
            }
#else
            idle_iterations = picogb_idle_spin_try(&mb, &ppu, &userdata);
            if (idle_iterations != 0U) {
                cycles = idle_iterations * 7U;
                instructions += (uint64_t)idle_iterations * 3U;
                idle_skipped_trips += idle_iterations;
            }
#endif
            else {
#if defined(KNOVGBC_IDLE_COUNTDOWN)
                word countdown_steps = 0U;
                cycles = 0U;
                if (mb.IR.raw == 0x003DU)
                    cycles = picogb_idle_countdown_try(&mb, &ppu, &userdata,
                        &countdown_steps);
                if (cycles != 0U) {
                    instructions += countdown_steps;
                    countdown_instructions += countdown_steps;
                    ++countdown_batches;
                }
                else
#endif
                {
#if defined(KNOVGBC_SUM_LOOP)
                    word sum_steps = 0U;
                    cycles = 0U;
                    if (mb.IR.raw == 0x0019U)
                        cycles = picogb_sum_loop_try(&mb, &ppu, &userdata,
                            &sum_steps);
                    if (cycles != 0U) {
                        instructions += sum_steps;
                        sum_instructions += sum_steps;
                        ++sum_batches;
                    }
                    else
#endif
                    {
#if defined(KNOVGBC_BLOCK_EXECUTOR_TEST)
                        block_executed = smoke_try_block(&block_cache, &mb,
                            &ppu, &userdata, &delayed_ppu_irq,
                            &instructions, &cpu_cycles, frames);
                        if (block_executed) {
                            cycles = 0U;
                        }
                        else
#endif
                        {
                            cycles = mb_exec(&mb);
                            ++instructions;
                        }
                    }
                }
            }
#else
#if defined(KNOVGBC_BLOCK_EXECUTOR_TEST)
            block_executed = smoke_try_block(&block_cache, &mb,
                &ppu, &userdata, &delayed_ppu_irq,
                &instructions, &cpu_cycles, frames);
            if (block_executed) {
                cycles = 0U;
            }
            else
#endif
            {
                cycles = mb_exec(&mb);
                ++instructions;
            }
#endif
#endif
        }
        else if (ppu.rLCDC & 0x80)
            cycles = ppu.next_update_ticks >> 2;
        else if (userdata.TIMER_CNT & 4)
        {
            word divider_sel = ((userdata.TIMER_CNT + 3) & 3) << 1;
            word step_period = 4U << divider_sel;
            cycles = ((0x100U - userdata.TIMER_ACCUM) * step_period) -
                     userdata.TIMER_SUB;
        }
        else
            cycles = 20;

#if defined(KNOVGBC_BLOCK_EXECUTOR_TEST)
        if (!block_executed) {
#endif
        if (cycles == 0)
        {
            fprintf(stderr, "Core stopped at PC=%04X IR=%04X after %llu instructions\n",
                    mb.PC, mb.IR.raw, (unsigned long long)instructions);
            free(rom);
            return 5;
        }

        if (mb.HALTING && !mb.IR.high)
            mb.IR.high = 1;

        cpu_cycles += cycles;

#if defined(KNOVGBC_SCHEDULER_BATCH)
        picogb_event_scheduler_advance(&scheduler, cycles);
        if (picogb_event_scheduler_flush_due(&scheduler))
            picogb_event_scheduler_flush(&scheduler);
#else
#if defined(KNOVGBC_HRAM_WAIT)
        if (!hram_committed) {
#endif
        mb.DIV += cycles;
        pgf_timer_update(&userdata, cycles);
#if CONFIG_APU_ENABLE && defined(KNOVGBC_AUDIO)
        apu_tick(&smoke_apu, cycles, 1);
#endif

        if (ppu.rLCDC & 0x80)
        {
            ppu_irq = ppu_tick(&ppu, cycles << 2);
            if (ppu_irq)
                delayed_ppu_irq |= ppu_irq;
        }
#if defined(KNOVGBC_HRAM_WAIT)
        }
#endif
#endif
#if defined(KNOVGBC_BLOCK_EXECUTOR_TEST)
        }
#endif

#if CONFIG_APU_ENABLE && defined(KNOVGBC_AUDIO)
        if (smoke_apu.outbuf_pos - smoke_audio_consumed >= 512U)
            smoke_audio_collect();
#endif
        if (ppu._redrawed)
        {
#if CONFIG_APU_ENABLE && defined(KNOVGBC_AUDIO)
            apu_flush(&smoke_apu, 1);
            smoke_audio_collect();
#endif
            ppu._redrawed = 0;
            ++frames;
#if defined(KNOVGBC_SMOKE_MBC1) && defined(KNOVGBC_SMOKE_PGM_CAPTURE)
            if (frames == 300U || frames == 600U || frames == 1200U)
                smoke_save_pgm(frames, argv[2]);
#endif
#if defined(KNOVGBC_SMOKE_FRAME_HOOK)
            KNOVGBC_SMOKE_FRAME_HOOK(frames);
#endif
#if defined(KNOVGBC_FRAME_TRACE)
            /* Host-only oracle: catch temporary divergence rather than only
             * comparing the final frame after the game returns to a loop. */
            printf("frame=%u instructions=%llu cycles=%llu pending=%u image=%016llx state=%016llx hram=%016llx sram=%016llx\n",
                frames, (unsigned long long)instructions,
                (unsigned long long)cpu_cycles, (unsigned)delayed_ppu_irq,
                (unsigned long long)framebuffer_hash(),
                (unsigned long long)machine_hash(&mb, &memory, &ppu, &userdata),
                (unsigned long long)fnv1a_bytes(UINT64_C(14695981039346656037),
                    memory.HRAM, sizeof(hram)),
                (unsigned long long)fnv1a_bytes(UINT64_C(14695981039346656037),
                    memory.SRAM, sizeof(sram)));
#endif
        }

        if (instructions > UINT64_C(200000000))
        {
            fprintf(stderr, "Instruction guard reached before %u frames\n", TEST_FRAMES);
            free(rom);
            return 6;
        }
    }

#if CONFIG_APU_ENABLE && defined(KNOVGBC_AUDIO)
    apu_flush(&smoke_apu, 1);
    smoke_audio_collect();
    if (smoke_audio_consumed != cpu_cycles / 32U ||
        smoke_audio_nonzero == 0U || smoke_audio_min >= 0 || smoke_audio_max <= 0) {
        fprintf(stderr, "Audio clock/synthesis contract failed: samples=%u expected=%llu nonzero=%llu min=%d max=%d\n",
            smoke_audio_consumed, (unsigned long long)(cpu_cycles / 32U),
            (unsigned long long)smoke_audio_nonzero, smoke_audio_min, smoke_audio_max);
        return 9;
    }
    fprintf(stderr, "audio_samples=%u nonzero=%llu min=%d max=%d pcm_fnv1a=%016llx\n",
        smoke_audio_consumed, (unsigned long long)smoke_audio_nonzero,
        smoke_audio_min, smoke_audio_max, (unsigned long long)smoke_audio_hash);
#endif
    printf("endian=%s frames=%u instructions=%llu cycles=%llu\n",
           PICOGB_LITTLE_ENDIAN ? "little" : "big", frames,
           (unsigned long long)instructions, (unsigned long long)cpu_cycles);
    printf("pc=%04X sp=%04X af=%02X%02X bc=%04X de=%04X hl=%04X ly=%02X\n",
           mb.PC, mb.SP, mb.reg.A, mb.reg.F, mb.reg.BC, mb.reg.DE, mb.reg.HL,
           (unsigned)ppu.state.scanY);
    printf("framebuffer_fnv1a=%016llx machine_fnv1a=%016llx\n",
           (unsigned long long)framebuffer_hash(),
           (unsigned long long)machine_hash(&mb, &memory, &ppu, &userdata));
#if defined(KNOVGBC_VDP12_SMOKE_CHECK)
    fprintf(stderr,
        "vdp12_visual_frames=%u invalid_frames=%u sprite_frames=%u sprite_strips=%u\n",
        vdp12_visual_frames, vdp12_invalid_frames,
        vdp12_sprite_frames, vdp12_sprite_strips);
#endif

#if defined(KNOVGBC_OPCODE_PROFILE)
    {
        struct picogb_opcode_profile_snapshot profile;

        picogb_opcode_profile_snapshot_take(&profile);
        printf("profile_instructions=%u profile_interrupts=%u profile_data=%u profile_samples=%u\n",
            (unsigned int)profile.instructions,
            (unsigned int)profile.interrupts,
            (unsigned int)profile.memory_accesses,
            (unsigned int)profile.memory_samples);
        for (i = 0; i < 0x100U; ++i)
        {
            if (profile.opcode[i] != 0U)
                printf("profile_opcode_%02X=%u\n", i,
                    (unsigned int)profile.opcode[i]);
        }
        for (unsigned int access = 0U;
             access < PICOGB_OPCODE_PROFILE_ACCESS_COUNT; ++access) {
            for (unsigned int memory_class = 0U;
                 memory_class < PICOGB_OPCODE_PROFILE_MEMORY_CLASS_COUNT;
                 ++memory_class) {
                const uint32_t count = profile.memory[access][memory_class];

                if (count != 0U) {
                    printf("profile_memory_%u_%u=%u\n", access,
                        memory_class, (unsigned int)count);
                }
            }
        }
    }
#endif

#if defined(KNOVGBC_LDH_F0_PROFILE)
    {
        uint64_t f0_total = 0;
        uint64_t f0_io = 0;
        uint64_t f0_hram = 0;
        uint64_t f0_ffff = 0;
        uint64_t f0_phase_total[LDH_F0_PHASES] = { 0, 0 };

        for (i = 0; i < 0x100U; ++i)
        {
            const uint64_t count = ldh_f0_haddr_counts[0][i] +
                                   ldh_f0_haddr_counts[1][i];

            f0_total += count;
            f0_phase_total[0] += ldh_f0_haddr_counts[0][i];
            f0_phase_total[1] += ldh_f0_haddr_counts[1][i];
            if (i < 0x80U)
                f0_io += count;
            else if (i < 0xFFU)
                f0_hram += count;
            else
                f0_ffff += count;
        }

        printf("ldh_f0_total=%llu ldh_f0_io=%llu ldh_f0_hram=%llu ldh_f0_ffff=%llu title_menu=%llu gameplay=%llu\n",
               (unsigned long long)f0_total,
               (unsigned long long)f0_io,
               (unsigned long long)f0_hram,
               (unsigned long long)f0_ffff,
               (unsigned long long)f0_phase_total[0],
               (unsigned long long)f0_phase_total[1]);
        for (i = 0; i < 0x100U; ++i)
        {
            const uint64_t count = ldh_f0_haddr_counts[0][i] +
                                   ldh_f0_haddr_counts[1][i];
            if (count != 0U)
                printf("ldh_f0_%02X=%llu title_menu=%llu gameplay=%llu\n", i,
                       (unsigned long long)count,
                       (unsigned long long)ldh_f0_haddr_counts[0][i],
                       (unsigned long long)ldh_f0_haddr_counts[1][i]);
        }
    }
#endif

#if defined(KNOVGBC_STACK_PROFILE)
    {
        unsigned phase;
        unsigned direction;
        uint64_t total = 0U;
        uint64_t eligible = 0U;

        for (phase = 0; phase < STACK_PROFILE_PHASES; ++phase) {
            for (direction = 0; direction < 2U; ++direction) {
                const uint64_t count = stack_pair_total[phase][direction];
                const uint64_t fixed = stack_pair_fixed_wram[phase][direction];

                total += count;
                eligible += fixed;
                fprintf(stderr,
                    "stack_phase=%s operation=%s total=%llu fixed_wram=%llu percent=%.2f\n",
                    phase == 0U ? "boot_menu" : "gameplay",
                    direction == STACK_PROFILE_PUSH ? "write_pair" : "read_pair",
                    (unsigned long long)count,
                    (unsigned long long)fixed,
                    count != 0U ? 100.0 * (double)fixed / (double)count : 0.0);
            }
        }
        fprintf(stderr, "stack_total=%llu fixed_wram=%llu percent=%.2f\n",
            (unsigned long long)total, (unsigned long long)eligible,
            total != 0U ? 100.0 * (double)eligible / (double)total : 0.0);
    }
#endif

#if defined(KNOVGBC_IDLE_SPIN)
#if defined(KNOVGBC_IDLE_EVENT)
#if defined(KNOVGBC_IDLE_RESUME)
    fprintf(stderr, "idle_resume_a7=%llu idle_resume_jr=%llu\n",
        (unsigned long long)idle_resume_a7, (unsigned long long)idle_resume_jr);
#if !defined(KNOVGBC_HRAM_WAIT) && !defined(KNOVGBC_SMOKE_MBC1)
    if (idle_resume_a7 == 0U || idle_resume_jr == 0U) {
        fputs("Both idle resume phases must be exercised\n", stderr);
        free(rom);
        return 9;
    }
#endif
#endif
    fprintf(stderr, "idle_event_instructions=%llu batches=%llu interpreter_calls=%llu\n",
        (unsigned long long)idle_event_instructions,
        (unsigned long long)idle_event_batches,
        (unsigned long long)(instructions - idle_event_instructions
#if defined(KNOVGBC_IDLE_COUNTDOWN)
            - countdown_instructions
#endif
#if defined(KNOVGBC_SUM_LOOP)
            - sum_instructions
#endif
        ));
#else
    fprintf(stderr, "idle_spin_skipped_trips=%llu\n",
        (unsigned long long)idle_skipped_trips);
#endif
#if !defined(KNOVGBC_SMOKE_MBC1)
#if defined(KNOVGBC_IDLE_EVENT)
    if (idle_event_instructions == 0U) {
#else
    if (idle_skipped_trips == 0U) {
#endif
        fprintf(stderr, "Idle spin shortcut was never exercised\n");
        free(rom);
        return 7;
    }
#endif
#endif
#if defined(KNOVGBC_IDLE_SPIN_CACHE_STATS)
    fprintf(stderr, "idle_spin_cache_hits=%u idle_spin_cache_misses=%u\n",
        (unsigned)picogb_idle_spin_cache_hits,
        (unsigned)picogb_idle_spin_cache_misses);
    if (picogb_idle_spin_cache_hits == 0U) {
        fprintf(stderr, "Idle recognition cache was never reused\n");
        free(rom);
        return 8;
    }
#endif

#if defined(KNOVGBC_IDLE_COUNTDOWN)
    fprintf(stderr, "countdown_instructions=%llu batches=%llu\n",
        (unsigned long long)countdown_instructions,
        (unsigned long long)countdown_batches);
#if !defined(KNOVGBC_SMOKE_MBC1)
    if (countdown_instructions == 0U) {
        fputs("Countdown shortcut was never exercised\n", stderr);
        free(rom);
        return 10;
    }
#endif
#endif

#if defined(KNOVGBC_SUM_LOOP)
    fprintf(stderr, "sum_instructions=%llu batches=%llu\n",
        (unsigned long long)sum_instructions, (unsigned long long)sum_batches);
#if defined(KNOVGBC_SMOKE_GAMEPLAY_INPUT)
    if (sum_instructions == 0U) {
        fputs("Sum-loop shortcut was never exercised during gameplay\n", stderr);
        free(rom);
        return 11;
    }
#endif
#endif

#if defined(KNOVGBC_IDLE_STAT)
    fprintf(stderr, "stat_instructions=%llu batches=%llu\n",
        (unsigned long long)stat_instructions, (unsigned long long)stat_batches);
#if defined(KNOVGBC_SMOKE_GAMEPLAY_INPUT)
    if (stat_instructions == 0U) {
        fputs("STAT shortcut was never exercised during gameplay\n", stderr);
        free(rom);
        return 12;
    }
#endif
#endif

#if defined(KNOVGBC_HRAM_WAIT)
    fprintf(stderr, "hram_wait_calls=%llu groups=%llu repeated_admissions_avoided=%llu\n",
        (unsigned long long)hram_wait_calls,
        (unsigned long long)idle_event_batches,
        (unsigned long long)(idle_event_batches - hram_wait_calls));
#if !defined(KNOVGBC_SMOKE_MBC1)
    if (hram_wait_calls == 0U || idle_event_batches <= hram_wait_calls) {
        fputs("Persistent HRAM wait was not exercised\n", stderr);
        free(rom);
        return 13;
    }
#endif
#endif
#if defined(KNOVGBC_BLOCK_METRICS)
    fprintf(stderr, "block_admissions=%llu hits=%llu instructions=%llu gameplay_hits=%llu gameplay_instructions=%llu\n",
        (unsigned long long)smoke_block_admissions,
        (unsigned long long)smoke_block_hits,
        (unsigned long long)smoke_block_instructions,
        (unsigned long long)smoke_block_gameplay_hits,
        (unsigned long long)smoke_block_gameplay_instructions);
    fprintf(stderr, "block_calls=%llu ime_on=%llu ime_pending=%llu ime_ask=%llu ir_high=%llu timer_on=%llu ppu_soon=%llu\n",
        (unsigned long long)smoke_block_calls,
        (unsigned long long)smoke_block_ime_on,
        (unsigned long long)smoke_block_ime_pending,
        (unsigned long long)smoke_block_ime_ask,
        (unsigned long long)smoke_block_ir_high,
        (unsigned long long)smoke_block_timer_on,
        (unsigned long long)smoke_block_ppu_soon);
#endif
#if defined(KNOVGBC_PPU_DIRTY_SCANLINES)
    fprintf(stderr, "dirty_callbacks_vram=%llu oam=%llu\n",
        (unsigned long long)smoke_dirty_vram_writes,
        (unsigned long long)smoke_dirty_oam_writes);
    fprintf(stderr,
        "dirty_vram_pattern=%llu map9800=%llu map9c00=%llu "
        "bg_map_row=%llu window_map_full=%llu\n",
        (unsigned long long)smoke_dirty_vram_pattern_writes,
        (unsigned long long)smoke_dirty_vram_map9800_writes,
        (unsigned long long)smoke_dirty_vram_map9c00_writes,
        (unsigned long long)smoke_dirty_bg_map_row_invalidations,
        (unsigned long long)smoke_dirty_window_map_full_invalidations);
    fprintf(stderr,
        "dirty_io_lcdc=%llu scroll=%llu scroll_unchanged=%llu "
        "window=%llu palette=%llu "
        "oam_dma=%llu bg_row_events=%llu\n",
        (unsigned long long)smoke_dirty_io_lcdc_writes,
        (unsigned long long)smoke_dirty_io_scroll_writes,
        (unsigned long long)smoke_dirty_io_scroll_unchanged,
        (unsigned long long)smoke_dirty_io_window_writes,
        (unsigned long long)smoke_dirty_io_palette_writes,
        (unsigned long long)smoke_dirty_io_dma_writes,
        (unsigned long long)smoke_dirty_bg_map_row_invalidations);
    for (unsigned phase = 0U; phase != 3U; phase++) {
        fprintf(stderr,
            "dirty_phase_%u pattern_full=%llu bg_row=%llu oam_callbacks=%llu "
            "oam_entries_changed=%llu io_full=%llu full_events=%llu\n",
            phase,
            (unsigned long long)smoke_dirty_pattern_by_phase[phase],
            (unsigned long long)smoke_dirty_bg_map_by_phase[phase],
            (unsigned long long)smoke_dirty_oam_callbacks_by_phase[phase],
            (unsigned long long)smoke_dirty_oam_entries_changed_by_phase[phase],
            (unsigned long long)smoke_dirty_io_full_by_phase[phase],
            (unsigned long long)smoke_dirty_full_by_phase[phase]);
    }
    if (smoke_dirty_vram_writes == 0U || smoke_dirty_oam_writes == 0U) {
        fprintf(stderr, "dirty scanline callbacks were not exercised\n");
        free(rom);
        return 1;
    }
#endif
    free(rom);
    return 0;
}
