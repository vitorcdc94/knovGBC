# knovGBC build for the project-local Yaul installation.
# Run it through tools/build_project_wsl.sh; Yaul intentionally rejects paths
# containing spaces, while that script uses the project's WSL bind mount.

BUILD_MODE ?= emulator
MVP_AUDIT_MODES := mvpfastrepro mvpfastfps mvpfastnext mvpfastidle mvpfastidlecache mvpfastidleevent mvpfastidleresume mvpfastidlecount mvpfastsum mvpfastidlestat mvpfasthramwait mvpfastcpn8 mvpfastvdp2lines mvpfastppueventcoalesce mvpfastpputwolines mvpfastppuadaptive mvpfastppufourlines mvpfastppueightlines mvpfastvdp2dma mvpfastvdp2direct mvpfastsmpcgate mvpfastindexed8 mvpfaststackpair mvpfastdirtyregions mvpfastsml mvpfastzoom mvpfastzoom3 mvpfastzoom60 mvpfastzoomtile mvpfastzoomaudio mvpfastzoomaudiomono mvpfastvdp12
# The Super Mario Land and zoom builds reuse the exact kmvpe050 core features.
MVP_FEATURE_MODE := $(if $(filter mvpfastsml mvpfastzoom mvpfastzoom3 mvpfastzoom60 mvpfastzoomtile mvpfastzoomaudio mvpfastzoomaudiomono,$(BUILD_MODE)),mvpfastdirtyregions,$(BUILD_MODE))

ifeq ($(BUILD_MODE),diagnostic)
SH_PROGRAM := knovgbc-diagnostic
else ifeq ($(BUILD_MODE),bootstrap)
# Eight-character name and a dedicated CUE/BIN pair make this hardware test
# unambiguous in Saroo's per-directory game picker.
SH_PROGRAM := yboot001
else ifeq ($(BUILD_MODE),bootstrapfix)
# Same Yaul-runtime probe, now with the complete SCU callback pair installed
# before __sys_init() enters __cpu_init()/__scu_init().
SH_PROGRAM := yboot002
else ifeq ($(BUILD_MODE),bootstrapstack)
# Full Yaul probe with the corrected, non-overlapping slave SH-2 stack.
SH_PROGRAM := yboot003
else ifeq ($(BUILD_MODE),bootstrapquarantine)
# Full Yaul probe that keeps physical SCU interrupts masked until main().
# It isolates the short interrupt handoff window without changing the final
# emulator's runtime behaviour.
SH_PROGRAM := yboot004
else ifeq ($(BUILD_MODE),masteronlyprobe)
# Full Yaul probe with the Slave SH-2 deliberately kept off. This validates
# the Master-only startup independently before applying it to the emulator.
SH_PROGRAM := yboot005
else ifeq ($(BUILD_MODE),masteronlyvideo)
# Master-only, video-only test of the emulator's actual 160x144 VDP2 bitmap
# path. Its distinct short name avoids any ambiguity in Saroo's game picker.
SH_PROGRAM := yvid0001
else ifeq ($(BUILD_MODE),masteronlyvideofix)
# Same real video path after restoring the Saturn BIOS SCU interrupt-dispatch
# contract. Keep a new identity so Saroo cannot select the previous image.
SH_PROGRAM := yvid0002
else ifeq ($(BUILD_MODE),bootstraptrace)
# A staged Yaul initialisation trace. It keeps the known-good direct VDP2
# output available and displays a marker before each Yaul subsystem.
SH_PROGRAM := ytrc0001
else ifeq ($(BUILD_MODE),stage0)
SH_PROGRAM := knovgbc-stage0
else ifeq ($(BUILD_MODE),stage1)
# Keep the test pair strictly 8.3 as a conservative compatibility measure
# for Saroo's CUE/BIN file lookup.
SH_PROGRAM := stg1
else ifeq ($(BUILD_MODE),stage2)
SH_PROGRAM := stg2
else ifeq ($(BUILD_MODE),masteronly)
# Keep the normal build intact as a fallback while the Master-only variant is
# validated on the actual Saturn hardware.
SH_PROGRAM := knovgbcm
else ifeq ($(BUILD_MODE),emulatorprobe)
# Second complete Master-only emulator image. This revision fixes Yaul's CD
# status decoder; keep a new identity so it cannot be confused with yemu0001.
SH_PROGRAM := yemu0002
else ifeq ($(BUILD_MODE),mvp)
# Second deliberately narrow product build: the same embedded Tetris DMG
# baseline, now without SameBoy's debug-only thread/context checks.  Keep a
# distinct name so the measured hardware result cannot be confused with the
# original slow image.
SH_PROGRAM := kmvp0002
else ifeq ($(BUILD_MODE),mvpfast)
# Third MVP revision: replace the cycle-accurate SameBoy runtime with the
# compact DMG-only PicoGB core while preserving the proven Saturn shell.
SH_PROGRAM := kmvp0003
else ifeq ($(BUILD_MODE),mvpfastos)
# Hardware A/B companion to kmvp0003.  Source and runtime configuration are
# identical; only GCC's size-oriented optimisation policy differs.
SH_PROGRAM := kmvpos04
else ifeq ($(BUILD_MODE),mvpfastprof)
# O3 diagnostic companion. It keeps the fast MVP's logic intact but samples
# the CPU, memory dispatch, PPU and timer work separately on hardware.
SH_PROGRAM := kmvppr05
else ifeq ($(BUILD_MODE),mvpfastmem)
# O3 companion with only the general flat-ROM memory fast path enabled. The
# SM83 interpreter and PPU remain identical to kmvp0003 for an attributable
# hardware comparison.
SH_PROGRAM := kmvpm006
else ifeq ($(BUILD_MODE),mvpfastppu)
# O3 companion to the memory path that caches only DMG palette conversion in
# the emulated PPU. Keep it separate so its hardware effect is attributable.
SH_PROGRAM := kmvpp007
else ifeq ($(BUILD_MODE),mvpfastppufull)
# Full DMG PPU optimisation bundle: cached tile rows, prepared sprites,
# conservative dirty scanlines and a small SH-2 tile blitter.  It stays a
# separate image until the whole package passes the host regressions.
SH_PROGRAM := kmvpf008
else ifeq ($(BUILD_MODE),mvpfastsm83)
# First native-SH-2 SM83 backend experiment.  It retains the working memory
# and PPU setup from kmvpf008, but dispatches dense register-only CPU opcode
# families in assembly and falls back to the C interpreter for the rest.
SH_PROGRAM := kmvpc009
else ifeq ($(BUILD_MODE),mvpfastslaveppu)
# Isolated dual-SH-2 validation. The Master and Slave render identical,
# synthetic DMG PPU fixtures and compare them before the Slave image is shown.
# It intentionally contains no SM83 interpreter or live Tetris state.
SH_PROGRAM := kmvps010
else ifeq ($(BUILD_MODE),mvpfastmastervdp2)
# Stable MVP runtime: the Master SH-2 runs the complete DMG/Tetris core in C
# and VDP2 presents the 160x144 framebuffer.  Keep this distinct from the
# Slave and assembly experiments so it remains a reproducible hardware base.
SH_PROGRAM := kmvpm011
else ifeq ($(BUILD_MODE),mvpfastcache)
# Cache A/B companion to kmvpm011.  It keeps the exact Master+VDP2 all-C
# runtime, but prevents data-cache misses from replacing instruction lines.
SH_PROGRAM := kmvpc015
else ifeq ($(BUILD_MODE),mvpfastfetch)
# Fetch A/B companion to kmvpm011. It keeps the all-C Master+VDP2 runtime and
# only integrates the normal ROM opcode fetch at the shared prefetch site.
SH_PROGRAM := kmvpf016
else ifeq ($(BUILD_MODE),mvpfastldh)
# LDH A,(a8) A/B companion to kmvpf016.  Only FF80-FFFE loads are direct;
# I/O and FFFF retain the existing high-address dispatch semantics.
SH_PROGRAM := kmvpl017
else ifeq ($(BUILD_MODE),mvpfastanda)
# A7/AND A A/B companion to kmvpl017.  It keeps the C interpreter in place
# and only bypasses generic ALU decoding for this standard SM83 operation.
SH_PROGRAM := kmvpa018
else ifeq ($(BUILD_MODE),mvpfastrepro)
# Rebuild the measured A7 baseline under a new name, preserving kmvpa018.
SH_PROGRAM := kmvpa026
else ifeq ($(BUILD_MODE),mvpfastfps)
# Identical A7 core with only the logical/visual FPS counters enabled.
SH_PROGRAM := kmvpf027
else ifeq ($(BUILD_MODE),mvpfastnext)
# One audited core optimisation against the A7 baseline with full HUD.
SH_PROGRAM := kmvpo028
else ifeq ($(BUILD_MODE),mvpfastidle)
# Preserve kmvpo028 and test one guarded, event-bounded polling-loop skip.
SH_PROGRAM := kmvpi029
else ifeq ($(BUILD_MODE),mvpfastidlecache)
# Exact kmvpi029 idle policy plus one cached positive ROM recognition.
SH_PROGRAM := kmvpr030
else ifeq ($(BUILD_MODE),mvpfastidleevent)
# Preserve kmvpi029 and test grouping through the next PPU event boundary.
SH_PROGRAM := kmvpe031
else ifeq ($(BUILD_MODE),mvpfastidleresume)
# Preserve kmvpe031 and resume the same guarded wait loop at partial phases.
SH_PROGRAM := kmvpu032
else ifeq ($(BUILD_MODE),mvpfastidlecount)
# Preserve kmvpu032 and test an event-bounded countdown-loop shortcut.
SH_PROGRAM := kmvpd033
else ifeq ($(BUILD_MODE),mvpfastsum)
# Preserve kmvpd033 and test one event-bounded repeated-addition loop.
SH_PROGRAM := kmvps034
else ifeq ($(BUILD_MODE),mvpfastidlestat)
# Exact kmvpd033 baseline plus one event-bounded STAT wait-loop shortcut.
SH_PROGRAM := kmvpt035
else ifeq ($(BUILD_MODE),mvpfasthramwait)
# Exact kmvpd033 baseline plus a dedicated, event-bounded HRAM wait runner.
SH_PROGRAM := kmvph036
else ifeq ($(BUILD_MODE),mvpfastcpn8)
# Preserve kmvph036 and test only the direct CP n8 interpreter path.
SH_PROGRAM := kmvpc037
else ifeq ($(BUILD_MODE),mvpfastvdp2lines)
# Exact kmvph036 core, writing only changed Game Boy scanlines to VDP2.
SH_PROGRAM := kmvpl038
else ifeq ($(BUILD_MODE),mvpfastppueventcoalesce)
# Exact kmvph036 core plus only the guarded PPU event coalescing experiment.
SH_PROGRAM := kmvpe039
else ifeq ($(BUILD_MODE),mvpfastpputwolines)
# Exact kmvpe039 core plus only the guarded two-visible-line PPU experiment.
SH_PROGRAM := kmvpe040
else ifeq ($(BUILD_MODE),mvpfastppuadaptive)
# Exact kmvpe040 core plus event-bounded adaptive PPU scanline batching.
SH_PROGRAM := kmvpe041
else ifeq ($(BUILD_MODE),mvpfastppufourlines)
# Exact kmvpe040 configuration with a fixed four-visible-line batch limit.
SH_PROGRAM := kmvpe042
else ifeq ($(BUILD_MODE),mvpfastppueightlines)
# Exact kmvpe040 configuration with a fixed eight-visible-line batch limit.
SH_PROGRAM := kmvpe043
else ifeq ($(BUILD_MODE),mvpfastvdp2dma)
# Exact kmvpe041 configuration with experimental VDP2 DMA presentation.
SH_PROGRAM := kmvpe044
else ifeq ($(BUILD_MODE),mvpfastvdp2direct)
# Exact kmvpe041 configuration with DMA reading the emulator's framebuffer directly.
SH_PROGRAM := kmvpe045
else ifeq ($(BUILD_MODE),mvpfastsmpcgate)
# Exact kmvpe045 configuration; do not wait to reissue INTBACK while SMPC is busy.
SH_PROGRAM := kmvpe046
else ifeq ($(BUILD_MODE),mvpfastindexed8)
# Exact kmvpe046 core and controls; test one-byte VDP2 indexed bitmap output.
SH_PROGRAM := kmvpe047
else ifeq ($(BUILD_MODE),mvpfaststackpair)
# Exact kmvpe047 configuration plus a guarded direct WRAM stack-pair path.
SH_PROGRAM := kmvpe048
else ifeq ($(BUILD_MODE),mvpfastdirtyregions)
# Exact kmvpe048 runtime plus precise PPU dirty-scanline tracking.
SH_PROGRAM := kmvpe050
else ifeq ($(BUILD_MODE),mvpfastsml)
# The kmvpe050 runtime with a separate embedded Super Mario Land cartridge.
SH_PROGRAM := kmvps051
else ifeq ($(BUILD_MODE),mvpfastzoom)
# Tetris kmvpe050, changing only VDP2 display zoom and HUD placement.
SH_PROGRAM := kmvpz052
else ifeq ($(BUILD_MODE),mvpfastzoom3)
# Exact kmvpz052 display, with only compact three-digit millisecond labels.
SH_PROGRAM := kmvpz053
else ifeq ($(BUILD_MODE),mvpfastzoom60)
# Exact kmvpz053 display, but draw every logical frame; omit redundant V FPS.
SH_PROGRAM := kmvpz054
else ifeq ($(BUILD_MODE),mvpfastzoomtile)
# Exact kmvpz054 full-rate build, with selective tile-pattern invalidation.
SH_PROGRAM := kmvpz055
else ifeq ($(BUILD_MODE),mvpfastzoomaudio)
# Preserve 055 and add DMG audio synthesis plus a dedicated SCSP PCM stream.
SH_PROGRAM := kmvpa056
else ifeq ($(BUILD_MODE),mvpfastzoomaudiomono)
# Exact 056 runtime, testing only the equivalent cached mono APU mixer.
SH_PROGRAM := kmvpa057
else ifeq ($(BUILD_MODE),mvpfastvdp12)
# Preserve kmvpe048 CPU/input/PPU timing; render with VDP2 cells and VDP1 strips.
SH_PROGRAM := kmvpe049
else ifeq ($(BUILD_MODE),mvpfastjrz)
# JR Z,e8 A/B companion to kmvpa018. It retains all prior fast paths and
# bypasses only the generic top-row decoder for this conditional branch.
SH_PROGRAM := kmvpj019
else ifeq ($(BUILD_MODE),mvpfastcb)
# CB-prefixed instructions are uncommon in Tetris. Keep their complete
# generic handler out of the hot interpreter body while retaining all-C code.
SH_PROGRAM := kmvpb020
else ifeq ($(BUILD_MODE),mvpfastopprof)
# Diagnostic companion to the measured kmvpa018 baseline. It retains the
# exact all-C Master+VDP2 configuration and adds only sampled opcode/memory
# counters; its output is evidence for the next isolated optimisation.
SH_PROGRAM := kmvpo021
else ifeq ($(BUILD_MODE),mvpfastf0tight)
# Exact kmvpa018 runtime plus a complete straight-line F0 path. It retains
# the HRAM-only direct read and the established I/O/FFFF fallback, but avoids
# the internal generic label/cycle path after the F0 opcode is decoded.
SH_PROGRAM := kmvpf022
else ifeq ($(BUILD_MODE),mvpfastblock)
# First generic, conservative SH-2 block-recompiler experiment.  It starts
# from the measured kmvpa018 configuration and adds only a ROM-only native
# block cache; all unsupported code retains PicoGB's C interpreter.
SH_PROGRAM := kmvpr023
else ifeq ($(BUILD_MODE),mvpfastblockc)
# Portable-C successor to kmvpr023. It keeps only a compact predecoded ROM
# plan; no SH-2 code is generated, no executable arena exists and the normal
# C interpreter remains the fallback for every unsupported instruction.
SH_PROGRAM := kmvpb025
else ifeq ($(BUILD_MODE),mvpfastcold)
# Cache-footprint companion to kmvpa018.  It remains entirely in C and moves
# only complete, rare SM83 handlers out of mb_exec's hot instruction body.
SH_PROGRAM := kmvph024
else ifeq ($(BUILD_MODE),mvpfastslaveppushadow)
# Live dual-CPU validation: the Master remains authoritative and VDP2 still
# presents its framebuffer. The Slave only replays a recorded PPU frame in
# private P2 memory and reports a comparison result.
SH_PROGRAM := kmvps012
else ifeq ($(BUILD_MODE),mvpfastsched)
# Master-only scheduler experiment. It preserves the all-C/VDP2 baseline and
# batches only timer/PPU bookkeeping between observable hardware events.
SH_PROGRAM := kmvpb013
else ifeq ($(BUILD_MODE),mvpfastcompact)
# Compact-C SM83 experiment. It keeps the Master+VDP2 baseline and routes
# frequent, generic SM83 opcode families through a cache-conscious C front
# end, retaining PicoGB's complete decoder as the compatibility fallback.
SH_PROGRAM := kmvpc014
else
SH_PROGRAM := knovgbc
endif
SH_BUILD_DIR := build
SH_OUTPUT_DIR := dist
ifneq ($(filter $(BUILD_MODE),$(MVP_AUDIT_MODES)),)
SH_BUILD_DIR := build/$(SH_PROGRAM)
endif
IMAGE_DIRECTORY := $(SH_BUILD_DIR)/cdroot
# The Saturn BIOS/Saroo loads the first ordinary ISO9660 root entry as the
# initial program. Keep this conventional 8.3 name ahead of ABS/BIB/CPY.
IMAGE_1ST_READ_BIN := 1ST_READ.BIN
AUDIO_TRACKS_DIRECTORY := build/audio-tracks
SATURN_ISO := $(abspath $(SH_OUTPUT_DIR))/$(SH_PROGRAM).iso
SATURN_BIN := $(abspath $(SH_OUTPUT_DIR))/$(SH_PROGRAM).bin
SATURN_CUE := $(abspath $(SH_OUTPUT_DIR))/$(SH_PROGRAM).cue

SAMEBOY := third_party/SameBoy/Core
PICOGB := third_party/PicoGB
# Publication wrappers may provide generated ROM assembly without replacing the
# historical embedded cartridges or changing the emulator's configuration.
MVP_ROM_SOURCE ?= $(if $(filter mvpfastsml,$(BUILD_MODE)),src/saturn/mvp_rom_sml.sx,src/saturn/mvp_rom.sx)
PICOGB_CORE_SRCS := \
	$(PICOGB)/microcode.c \
	$(PICOGB)/microcode_dispatch.c \
	$(PICOGB)/mi.c \
	$(PICOGB)/ppu.c \
	$(PICOGB)/fabric.c
CORE_SRCS := \
	$(filter-out \
		$(SAMEBOY)/cheats.c \
		$(SAMEBOY)/cheat_search.c \
		$(SAMEBOY)/debugger.c \
		$(SAMEBOY)/rewind.c \
		$(SAMEBOY)/sm83_disassembler.c \
		$(SAMEBOY)/symbol_hash.c, \
		$(wildcard $(SAMEBOY)/*.c))

# These translation units are already completely discarded from the working
# image by LTO/--gc-sections. Omit them from the MVP build explicitly so its
# source list reflects the promised feature set without touching coupled core
# modules such as APU, SGB, camera or mapper handling before profiling them.
MVP_CORE_SRCS := \
	$(filter-out \
		$(SAMEBOY)/save_state.c \
		$(SAMEBOY)/printer.c \
		$(SAMEBOY)/workboy.c, \
		$(CORE_SRCS))

ifeq ($(BUILD_MODE),diagnostic)
# Keep this image deliberately independent of the emulator, CD filesystem and
# peripheral stack. It is used only to establish that the Saroo can execute a
# Yaul program from this disc image before a hardware-specific change.
SH_SRCS := src/saturn/diagnostic_main.c
else ifeq ($(BUILD_MODE),bootstrap)
# This is the first Yaul test that does not use Yaul's stock crt0.  The custom
# entry point establishes VBR before __sys_init reaches __cpu_init().
SH_SRCS := src/saturn/boot_entry.sx src/saturn/bootstrap_probe_main.c
else ifeq ($(BUILD_MODE),bootstrapfix)
# The confirmed missing SCU mask callback is installed by user_early_init(),
# which runs before Yaul's CPU and SCU initialization.
SH_SRCS := src/saturn/boot_entry.sx src/saturn/bootstrap_probe_main.c \
	src/saturn/saroo_bios_shim.c
else ifeq ($(BUILD_MODE),bootstrapstack)
# Same minimal Yaul probe as bootstrapfix, now with separate master/slave
# stacks supplied through the common IP metadata below.
SH_SRCS := src/saturn/boot_entry.sx src/saturn/bootstrap_probe_main.c \
	src/saturn/saroo_bios_shim.c
else ifeq ($(BUILD_MODE),bootstrapquarantine)
# Use a separately compiled shim variant so the normal emulator never inherits
# this diagnostic-only physical interrupt quarantine.
SH_SRCS := src/saturn/boot_entry.sx src/saturn/bootstrap_probe_main.c \
	src/saturn/saroo_bios_shim_quarantine.c
else ifeq ($(BUILD_MODE),masteronlyprobe)
# Exercise the complete Yaul path with the same Master-only interposition that
# the emulator variant uses, but retain the visible magenta/cyan probe.
SH_SRCS := src/saturn/boot_entry.sx src/saturn/bootstrap_probe_main.c \
	src/saturn/saroo_bios_shim.c src/saturn/master_only_cpu_init.c
else ifeq ($(BUILD_MODE),masteronlyvideo)
# Validate the real VDP2 setup and framebuffer upload in isolation. This
# target links only the split video module, not the pad/CD platform module.
SH_SRCS := src/saturn/boot_entry.sx src/saturn/platform_video_probe_main.c \
	src/saturn/platform_video.c src/saturn/saroo_bios_shim.c \
	src/saturn/master_only_cpu_init.c
else ifeq ($(BUILD_MODE),masteronlyvideofix)
# Use the same isolated sources as yvid0001. The distinct target exists to
# make the corrected interrupt bridge unmistakable on the SD card/hardware.
SH_SRCS := src/saturn/boot_entry.sx src/saturn/platform_video_probe_main.c \
	src/saturn/platform_video.c src/saturn/saroo_bios_shim.c \
	src/saturn/master_only_cpu_init.c
else ifeq ($(BUILD_MODE),bootstraptrace)
# This intentionally bypasses __sys_init. The trace clears BSS itself, then
# invokes the same Yaul subsystems one at a time with visible checkpoints.
SH_SRCS := src/saturn/bootstrap_trace_entry.sx \
	src/saturn/bootstrap_trace_main.c src/saturn/saroo_bios_shim.c
else ifeq ($(BUILD_MODE),stage0)
# This is below the Yaul display/sync layer: it only writes the VDP2 backdrop
# register and its single backing colour directly.
SH_SRCS := src/saturn/stage0_main.c
else ifeq ($(BUILD_MODE),stage1)
# Uses the exact Yaul VDP2 debug-console path that produced the earlier
# exception screen, to establish positively that main() is reached.
SH_SRCS := src/saturn/stage1_main.c
else ifeq ($(BUILD_MODE),stage2)
# Reaches main() and deliberately raises Yaul's illegal-instruction handler.
# This probe has no dependency on any normal video rendering code.
SH_SRCS := src/saturn/stage2_main.c
else ifeq ($(BUILD_MODE),masteronly)
# The emulator itself is already single-threaded. This mode only removes the
# Yaul startup path that turns on the otherwise idle Slave SH-2.
SH_SRCS := $(CORE_SRCS) src/saturn/main.c src/saturn/platform.c \
	src/saturn/platform_video.c \
	src/saturn/time_compat.c src/saturn/math_compat.c \
	src/saturn/boot_entry.sx src/saturn/saroo_bios_shim.c \
	src/saturn/master_only_cpu_init.c src/saturn/saturn_heap.c \
	src/saturn/sameboy_boot_roms.sx
else ifeq ($(BUILD_MODE),emulatorprobe)
# Same full emulator as masteronly, with visible startup checkpoints enabled
# through a target-specific compiler definition below.
SH_SRCS := $(CORE_SRCS) src/saturn/main.c src/saturn/platform.c \
	src/saturn/platform_video.c \
	src/saturn/time_compat.c src/saturn/math_compat.c \
	src/saturn/boot_entry.sx src/saturn/saroo_bios_shim.c \
	src/saturn/master_only_cpu_init.c src/saturn/saturn_heap.c \
	src/saturn/sameboy_boot_roms.sx
else ifeq ($(BUILD_MODE),mvp)
# The MVP contains its only ROM and DMG boot ROM in 1ST_READ.BIN. It retains
# only SMPC input and the proven VDP2 video path; CD/CDFS and GBC boot assets
# are not linked.
SH_SRCS := $(MVP_CORE_SRCS) \
	src/saturn/mvp_main.c src/saturn/mvp_platform.c \
	src/saturn/mvp_profiler.c src/saturn/platform_video.c \
	src/saturn/time_compat.c src/saturn/math_compat.c \
	src/saturn/boot_entry.sx src/saturn/saroo_bios_shim.c \
	src/saturn/master_only_cpu_init.c src/saturn/saturn_heap.c \
	src/saturn/mvp_boot_rom.sx src/saturn/mvp_rom.sx
else ifeq ($(BUILD_MODE),mvpfastslaveppu)
# A narrow PPU-only dual-CPU probe. Keep the proven Master-only boot code and
# enable the Slave explicitly from main(), after Yaul/video initialization.
SH_SRCS := $(PICOGB)/ppu.c \
	src/saturn/slave_ppu_probe_main.c src/saturn/platform_video.c \
	src/saturn/boot_entry.sx src/saturn/saroo_bios_shim.c \
	src/saturn/master_only_cpu_init.c
else ifneq ($(filter $(BUILD_MODE),mvpfast mvpfastos mvpfastprof mvpfastmem mvpfastppu mvpfastppufull mvpfastsm83 mvpfastmastervdp2 mvpfastcache mvpfastfetch mvpfastldh mvpfastanda mvpfastjrz mvpfastcb mvpfastopprof mvpfastf0tight mvpfastblock mvpfastblockc mvpfastcold mvpfastslaveppushadow mvpfastsched mvpfastcompact $(MVP_AUDIT_MODES)),)
# DMG single-ROM fast path. State, work RAM and framebuffer are static: no
# SameBoy, APU, boot ROM, filesystem, dynamic heap, time or math shims enter
# this executable.
SH_SRCS := $(PICOGB_CORE_SRCS) \
	src/saturn/picogb_mvp_main.c src/saturn/picogb_mvp_input.c \
	src/saturn/mvp_profiler.c src/saturn/platform_video.c \
	$(if $(filter mvpfastvdp12,$(BUILD_MODE)),src/saturn/picogb_vdp12_frame.c src/saturn/platform_video_vdp12.c) \
	src/saturn/boot_entry.sx src/saturn/saroo_bios_shim.c \
	src/saturn/master_only_cpu_init.c \
	$(MVP_ROM_SOURCE) \
	$(if $(filter mvpfastslaveppushadow,$(BUILD_MODE)),src/saturn/picogb_slave_ppu_shadow.c) \
	$(if $(filter mvpfastppufull mvpfastsm83,$(BUILD_MODE)),src/saturn/picogb_ppu_tile_copy.c src/saturn/picogb_ppu_tile_copy_sh2.sx) \
	$(if $(filter mvpfastsm83,$(BUILD_MODE)),src/saturn/picogb_sm83_sh2.c src/saturn/picogb_sm83_exec_sh2.sx) \
	$(if $(filter mvpfastcompact,$(BUILD_MODE)),src/saturn/picogb_sm83_compact.c) \
	$(if $(filter mvpfastblock,$(BUILD_MODE)),src/saturn/picogb_block_cache.c) \
	$(if $(filter mvpfastblockc,$(BUILD_MODE)),src/saturn/picogb_block_executor.c) \
	$(if $(filter mvpfastidle mvpfastidlecache mvpfastidleevent mvpfastidleresume mvpfastidlecount mvpfastsum mvpfastidlestat mvpfasthramwait mvpfastcpn8 mvpfastvdp2lines mvpfastppueventcoalesce mvpfastpputwolines mvpfastppuadaptive mvpfastppufourlines mvpfastppueightlines mvpfastvdp2dma mvpfastvdp2direct mvpfastsmpcgate mvpfastindexed8 mvpfaststackpair mvpfastdirtyregions mvpfastvdp12,$(MVP_FEATURE_MODE)),src/saturn/picogb_idle_spin.c) \
	$(if $(filter mvpfastidlecount mvpfastsum mvpfastidlestat mvpfasthramwait mvpfastcpn8 mvpfastvdp2lines mvpfastppueventcoalesce mvpfastpputwolines mvpfastppuadaptive mvpfastppufourlines mvpfastppueightlines mvpfastvdp2dma mvpfastvdp2direct mvpfastsmpcgate mvpfastindexed8 mvpfaststackpair mvpfastdirtyregions mvpfastvdp12,$(MVP_FEATURE_MODE)),src/saturn/picogb_idle_countdown.c) \
	$(if $(filter mvpfastsum,$(BUILD_MODE)),src/saturn/picogb_sum_loop.c) \
	$(if $(filter mvpfastidlestat,$(BUILD_MODE)),src/saturn/picogb_idle_stat.c) \
	$(if $(filter mvpfasthramwait mvpfastcpn8 mvpfastvdp2lines mvpfastppueventcoalesce mvpfastpputwolines mvpfastppuadaptive mvpfastppufourlines mvpfastppueightlines mvpfastvdp2dma mvpfastvdp2direct mvpfastsmpcgate mvpfastindexed8 mvpfaststackpair mvpfastdirtyregions mvpfastvdp12,$(MVP_FEATURE_MODE)),src/saturn/picogb_hram_wait.c) \
	$(if $(filter mvpfastopprof,$(BUILD_MODE)),src/saturn/picogb_opcode_profile.c)
ifneq ($(filter mvpfastzoomaudio mvpfastzoomaudiomono,$(BUILD_MODE)),)
SH_SRCS += $(PICOGB)/apu.c src/saturn/picogb_saturn_audio.c
endif
else
SH_SRCS := $(CORE_SRCS) src/saturn/main.c src/saturn/platform.c \
	src/saturn/platform_video.c \
	src/saturn/time_compat.c src/saturn/math_compat.c \
	src/saturn/boot_entry.sx src/saturn/saroo_bios_shim.c \
	src/saturn/saturn_heap.c src/saturn/sameboy_boot_roms.sx
endif

# Do not inject Yaul's crt0.o. yaul.specs provides only the compiler/linker
# defaults; boot_entry.sx supplies _start and init.o retains Yaul's regular
# BSS/heap/CPU/SCU/SMPC/VDP initialisation after VBR is valid.
SH_SPECS := yaul.specs
# Sega Saturn IP.BIN metadata. The read size is derived from the program.
ifeq ($(BUILD_MODE),bootstrapfix)
IP_PRODUCT_ID := T-99998G
IP_VERSION := V0.002
else ifeq ($(BUILD_MODE),bootstraptrace)
IP_PRODUCT_ID := T-99997G
IP_VERSION := V0.003
else ifeq ($(BUILD_MODE),bootstrapstack)
IP_PRODUCT_ID := T-99996G
IP_VERSION := V0.004
else ifeq ($(BUILD_MODE),bootstrapquarantine)
IP_PRODUCT_ID := T-99995G
IP_VERSION := V0.005
else ifeq ($(BUILD_MODE),masteronlyprobe)
IP_PRODUCT_ID := T-99994G
IP_VERSION := V0.006
else ifeq ($(BUILD_MODE),masteronlyvideo)
IP_PRODUCT_ID := T-99993G
IP_VERSION := V0.007
else ifeq ($(BUILD_MODE),masteronlyvideofix)
IP_PRODUCT_ID := T-99992G
IP_VERSION := V0.008
else ifeq ($(BUILD_MODE),masteronly)
IP_PRODUCT_ID := T-99902G
IP_VERSION := V1.001
else ifeq ($(BUILD_MODE),emulatorprobe)
IP_PRODUCT_ID := T-99990G
IP_VERSION := V0.010
else ifeq ($(BUILD_MODE),mvp)
IP_PRODUCT_ID := T-99989G
IP_VERSION := V0.012
else ifeq ($(BUILD_MODE),mvpfast)
IP_PRODUCT_ID := T-99988G
IP_VERSION := V0.013
else ifeq ($(BUILD_MODE),mvpfastos)
IP_PRODUCT_ID := T-99987G
IP_VERSION := V0.014
else ifeq ($(BUILD_MODE),mvpfastprof)
IP_PRODUCT_ID := T-99986G
IP_VERSION := V0.015
else ifeq ($(BUILD_MODE),mvpfastmem)
IP_PRODUCT_ID := T-99985G
IP_VERSION := V0.016
else ifeq ($(BUILD_MODE),mvpfastppu)
IP_PRODUCT_ID := T-99984G
IP_VERSION := V0.017
else ifeq ($(BUILD_MODE),mvpfastppufull)
IP_PRODUCT_ID := T-99983G
IP_VERSION := V0.018
else ifeq ($(BUILD_MODE),mvpfastsm83)
IP_PRODUCT_ID := T-99982G
IP_VERSION := V0.019
else ifeq ($(BUILD_MODE),mvpfastslaveppu)
IP_PRODUCT_ID := T-99981G
IP_VERSION := V0.020
else ifeq ($(BUILD_MODE),mvpfastmastervdp2)
IP_PRODUCT_ID := T-99980G
IP_VERSION := V0.021
else ifeq ($(BUILD_MODE),mvpfastcache)
IP_PRODUCT_ID := T-99976G
IP_VERSION := V0.025
else ifeq ($(BUILD_MODE),mvpfastfetch)
IP_PRODUCT_ID := T-99975G
IP_VERSION := V0.026
else ifeq ($(BUILD_MODE),mvpfastldh)
IP_PRODUCT_ID := T-99974G
IP_VERSION := V0.027
else ifeq ($(BUILD_MODE),mvpfastanda)
IP_PRODUCT_ID := T-99973G
IP_VERSION := V0.028
else ifeq ($(BUILD_MODE),mvpfastrepro)
IP_PRODUCT_ID := T-99965G
IP_VERSION := V0.036
else ifeq ($(BUILD_MODE),mvpfastfps)
IP_PRODUCT_ID := T-99964G
IP_VERSION := V0.037
else ifeq ($(BUILD_MODE),mvpfastnext)
IP_PRODUCT_ID := T-99963G
IP_VERSION := V0.038
else ifeq ($(BUILD_MODE),mvpfastidle)
IP_PRODUCT_ID := T-99962G
IP_VERSION := V0.039
else ifeq ($(BUILD_MODE),mvpfastidlecache)
IP_PRODUCT_ID := T-99961G
IP_VERSION := V0.040
else ifeq ($(BUILD_MODE),mvpfastidleevent)
IP_PRODUCT_ID := T-99960G
IP_VERSION := V0.041
else ifeq ($(BUILD_MODE),mvpfastidleresume)
IP_PRODUCT_ID := T-99959G
IP_VERSION := V0.042
else ifeq ($(BUILD_MODE),mvpfastidlecount)
IP_PRODUCT_ID := T-99958G
IP_VERSION := V0.043
else ifeq ($(BUILD_MODE),mvpfastsum)
IP_PRODUCT_ID := T-99957G
IP_VERSION := V0.044
else ifeq ($(BUILD_MODE),mvpfastidlestat)
IP_PRODUCT_ID := T-99956G
IP_VERSION := V0.045
else ifeq ($(BUILD_MODE),mvpfasthramwait)
IP_PRODUCT_ID := T-99955G
IP_VERSION := V0.046
else ifeq ($(BUILD_MODE),mvpfastcpn8)
IP_PRODUCT_ID := T-99954G
IP_VERSION := V0.047
else ifeq ($(BUILD_MODE),mvpfastvdp2lines)
IP_PRODUCT_ID := T-99953G
IP_VERSION := V0.048
else ifeq ($(BUILD_MODE),mvpfastppueventcoalesce)
IP_PRODUCT_ID := T-99952G
IP_VERSION := V0.049
else ifeq ($(BUILD_MODE),mvpfastpputwolines)
IP_PRODUCT_ID := T-99951G
IP_VERSION := V0.050
else ifeq ($(BUILD_MODE),mvpfastppuadaptive)
IP_PRODUCT_ID := T-99950G
IP_VERSION := V0.051
else ifeq ($(BUILD_MODE),mvpfastppufourlines)
IP_PRODUCT_ID := T-99949G
IP_VERSION := V0.052
else ifeq ($(BUILD_MODE),mvpfastppueightlines)
IP_PRODUCT_ID := T-99948G
IP_VERSION := V0.053
else ifeq ($(BUILD_MODE),mvpfastvdp2dma)
IP_PRODUCT_ID := T-99947G
IP_VERSION := V0.054
else ifeq ($(BUILD_MODE),mvpfastvdp2direct)
IP_PRODUCT_ID := T-99946G
IP_VERSION := V0.055
else ifeq ($(BUILD_MODE),mvpfastsmpcgate)
IP_PRODUCT_ID := T-99945G
IP_VERSION := V0.056
else ifeq ($(BUILD_MODE),mvpfastindexed8)
IP_PRODUCT_ID := T-99944G
IP_VERSION := V0.057
else ifeq ($(BUILD_MODE),mvpfaststackpair)
IP_PRODUCT_ID := T-99943G
IP_VERSION := V0.058
else ifeq ($(BUILD_MODE),mvpfastdirtyregions)
IP_PRODUCT_ID := T-99941G
IP_VERSION := V0.060
else ifeq ($(BUILD_MODE),mvpfastsml)
IP_PRODUCT_ID := T-99940G
IP_VERSION := V0.061
else ifeq ($(BUILD_MODE),mvpfastzoom)
IP_PRODUCT_ID := T-99939G
IP_VERSION := V0.062
else ifeq ($(BUILD_MODE),mvpfastzoom3)
IP_PRODUCT_ID := T-99938G
IP_VERSION := V0.063
else ifeq ($(BUILD_MODE),mvpfastzoom60)
IP_PRODUCT_ID := T-99937G
IP_VERSION := V0.064
else ifeq ($(BUILD_MODE),mvpfastzoomtile)
IP_PRODUCT_ID := T-99936G
IP_VERSION := V0.065
else ifeq ($(BUILD_MODE),mvpfastzoomaudio)
IP_PRODUCT_ID := T-99935G
IP_VERSION := V0.066
else ifeq ($(BUILD_MODE),mvpfastzoomaudiomono)
IP_PRODUCT_ID := T-99934G
IP_VERSION := V0.067
else ifeq ($(BUILD_MODE),mvpfastvdp12)
IP_PRODUCT_ID := T-99942G
IP_VERSION := V0.059
else ifeq ($(BUILD_MODE),mvpfastjrz)
IP_PRODUCT_ID := T-99972G
IP_VERSION := V0.029
else ifeq ($(BUILD_MODE),mvpfastcb)
IP_PRODUCT_ID := T-99971G
IP_VERSION := V0.030
else ifeq ($(BUILD_MODE),mvpfastopprof)
IP_PRODUCT_ID := T-99970G
IP_VERSION := V0.031
else ifeq ($(BUILD_MODE),mvpfastf0tight)
IP_PRODUCT_ID := T-99969G
IP_VERSION := V0.032
else ifeq ($(BUILD_MODE),mvpfastblock)
IP_PRODUCT_ID := T-99968G
IP_VERSION := V0.033
else ifeq ($(BUILD_MODE),mvpfastblockc)
IP_PRODUCT_ID := T-99966G
IP_VERSION := V0.035
else ifeq ($(BUILD_MODE),mvpfastslaveppushadow)
IP_PRODUCT_ID := T-99979G
IP_VERSION := V0.022
else ifeq ($(BUILD_MODE),mvpfastsched)
IP_PRODUCT_ID := T-99978G
IP_VERSION := V0.023
else ifeq ($(BUILD_MODE),mvpfastcompact)
IP_PRODUCT_ID := T-99977G
IP_VERSION := V0.024
else
IP_PRODUCT_ID := T-99901G
IP_VERSION := V1.000
endif
export IP_PRODUCT_ID
IP_RELEASE_DATE := 20260816
IP_AREAS := JUE
IP_PERIPHERALS := J
ifeq ($(BUILD_MODE),bootstrap)
IP_TITLE := knovGBC Yaul Bootstrap 001
else ifeq ($(BUILD_MODE),bootstrapfix)
IP_TITLE := knovGBC Yaul Bootstrap 002
else ifeq ($(BUILD_MODE),bootstraptrace)
IP_TITLE := knovGBC Yaul Trace 001
else ifeq ($(BUILD_MODE),bootstrapstack)
IP_TITLE := knovGBC Yaul Bootstrap 003
else ifeq ($(BUILD_MODE),bootstrapquarantine)
IP_TITLE := knovGBC Yaul Bootstrap 004
else ifeq ($(BUILD_MODE),masteronlyprobe)
IP_TITLE := knovGBC Master Only Probe
else ifeq ($(BUILD_MODE),masteronlyvideo)
IP_TITLE := knovGBC Master Video 001
else ifeq ($(BUILD_MODE),masteronlyvideofix)
IP_TITLE := knovGBC Master Video 002
else ifeq ($(BUILD_MODE),masteronly)
IP_TITLE := knovGBC Master Only
else ifeq ($(BUILD_MODE),emulatorprobe)
IP_TITLE := knovGBC Master Emulator 002
else ifeq ($(BUILD_MODE),mvp)
IP_TITLE := knovGBC MVP Tetris 002
else ifeq ($(BUILD_MODE),mvpfast)
IP_TITLE := knovGBC MVP Fast Tetris 003
else ifeq ($(BUILD_MODE),mvpfastos)
IP_TITLE := knovGBC MVP Os Tetris 004
else ifeq ($(BUILD_MODE),mvpfastprof)
IP_TITLE := knovGBC MVP Profile Tetris 005
else ifeq ($(BUILD_MODE),mvpfastmem)
IP_TITLE := knovGBC MVP Memory Tetris 006
else ifeq ($(BUILD_MODE),mvpfastppu)
IP_TITLE := knovGBC MVP PPU Tetris 007
else ifeq ($(BUILD_MODE),mvpfastppufull)
IP_TITLE := knovGBC MVP PPU Full 008
else ifeq ($(BUILD_MODE),mvpfastsm83)
IP_TITLE := knovGBC MVP SM83 SH2 009
else ifeq ($(BUILD_MODE),mvpfastslaveppu)
IP_TITLE := knovGBC Slave PPU 010
else ifeq ($(BUILD_MODE),mvpfastmastervdp2)
IP_TITLE := knovGBC Master VDP2 011
else ifeq ($(BUILD_MODE),mvpfastcache)
IP_TITLE := knovGBC Cache Data 015
else ifeq ($(BUILD_MODE),mvpfastfetch)
IP_TITLE := knovGBC Fetch 016
else ifeq ($(BUILD_MODE),mvpfastldh)
IP_TITLE := knovGBC LDH HRAM 017
else ifeq ($(BUILD_MODE),mvpfastanda)
IP_TITLE := knovGBC A7 Direct 018
else ifeq ($(BUILD_MODE),mvpfastrepro)
IP_TITLE := knovGBC A7 Rebuild 026
else ifeq ($(BUILD_MODE),mvpfastfps)
IP_TITLE := knovGBC FPS Only 027
else ifeq ($(BUILD_MODE),mvpfastnext)
IP_TITLE := knovGBC Audited Opt 028
else ifeq ($(BUILD_MODE),mvpfastidle)
IP_TITLE := knovGBC Idle Spin 029
else ifeq ($(BUILD_MODE),mvpfastidlecache)
IP_TITLE := knovGBC Idle Recognition 030
else ifeq ($(BUILD_MODE),mvpfastidleevent)
IP_TITLE := knovGBC Idle Event 031
else ifeq ($(BUILD_MODE),mvpfastidleresume)
IP_TITLE := knovGBC Idle Resume 032
else ifeq ($(BUILD_MODE),mvpfastidlecount)
IP_TITLE := knovGBC Idle Countdown 033
else ifeq ($(BUILD_MODE),mvpfastsum)
IP_TITLE := knovGBC Sum Loop 034
else ifeq ($(BUILD_MODE),mvpfastidlestat)
IP_TITLE := knovGBC STAT Wait 035
else ifeq ($(BUILD_MODE),mvpfasthramwait)
IP_TITLE := knovGBC HRAM Wait 036
else ifeq ($(BUILD_MODE),mvpfastcpn8)
IP_TITLE := knovGBC CP n8 Direct 037
else ifeq ($(BUILD_MODE),mvpfastvdp2lines)
IP_TITLE := knovGBC VDP2 Dirty Lines 038
else ifeq ($(BUILD_MODE),mvpfastppueventcoalesce)
IP_TITLE := knovGBC PPU Event Coalesce 039
else ifeq ($(BUILD_MODE),mvpfastpputwolines)
IP_TITLE := knovGBC PPU Two Lines 040
else ifeq ($(BUILD_MODE),mvpfastppuadaptive)
IP_TITLE := knovGBC PPU Adaptive 041
else ifeq ($(BUILD_MODE),mvpfastppufourlines)
IP_TITLE := knovGBC PPU Four Lines 042
else ifeq ($(BUILD_MODE),mvpfastppueightlines)
IP_TITLE := knovGBC PPU Eight Lines 043
else ifeq ($(BUILD_MODE),mvpfastvdp2dma)
IP_TITLE := knovGBC VDP2 DMA 044
else ifeq ($(BUILD_MODE),mvpfastvdp2direct)
IP_TITLE := knovGBC VDP2 DMA Direct 045
else ifeq ($(BUILD_MODE),mvpfastsmpcgate)
IP_TITLE := knovGBC SMPC Gate 046
else ifeq ($(BUILD_MODE),mvpfastindexed8)
IP_TITLE := knovGBC VDP2 Indexed 047
else ifeq ($(BUILD_MODE),mvpfaststackpair)
IP_TITLE := knovGBC WRAM Stack Pair 048
else ifeq ($(BUILD_MODE),mvpfastdirtyregions)
IP_TITLE := knovGBC PPU Dirty Regions 050
else ifeq ($(BUILD_MODE),mvpfastsml)
IP_TITLE := knovGBC Super Mario Land 051
else ifeq ($(BUILD_MODE),mvpfastzoom)
IP_TITLE := knovGBC Tetris Zoom 052
else ifeq ($(BUILD_MODE),mvpfastzoom3)
IP_TITLE := knovGBC Tetris Zoom 053
else ifeq ($(BUILD_MODE),mvpfastzoom60)
IP_TITLE := knovGBC Tetris 60 Visual 054
else ifeq ($(BUILD_MODE),mvpfastzoomtile)
IP_TITLE := knovGBC Tetris Tile Dirty 055
else ifeq ($(BUILD_MODE),mvpfastzoomaudio)
IP_TITLE := knovGBC Tetris Audio 056
else ifeq ($(BUILD_MODE),mvpfastzoomaudiomono)
IP_TITLE := knovGBC Tetris Audio Mono 057
else ifeq ($(BUILD_MODE),mvpfastvdp12)
IP_TITLE := knovGBC VDP1 VDP2 049
else ifeq ($(BUILD_MODE),mvpfastjrz)
IP_TITLE := knovGBC JR Z Direct 019
else ifeq ($(BUILD_MODE),mvpfastcb)
IP_TITLE := knovGBC CB Cold 020
else ifeq ($(BUILD_MODE),mvpfastopprof)
IP_TITLE := knovGBC Opcode Profile 021
else ifeq ($(BUILD_MODE),mvpfastf0tight)
IP_TITLE := knovGBC F0 Tight 022
else ifeq ($(BUILD_MODE),mvpfastblock)
IP_TITLE := knovGBC Block JIT 023
else ifeq ($(BUILD_MODE),mvpfastblockc)
IP_TITLE := knovGBC Block C 025
else ifeq ($(BUILD_MODE),mvpfastslaveppushadow)
IP_TITLE := knovGBC Slave Shadow 012
else ifeq ($(BUILD_MODE),mvpfastsched)
IP_TITLE := knovGBC Scheduler 013
else ifeq ($(BUILD_MODE),mvpfastcompact)
IP_TITLE := knovGBC Compact C 014
else
IP_TITLE := knovGBC - Game Boy and Game Boy Color
endif
IP_MASTER_STACK_ADDR := 0x060FFFF0
# The slave SH-2 is switched on by Yaul during __cpu_init(). Both stacks grow
# downward, so sharing the master top corrupts return frames as soon as the
# slave begins its polling entry. Keep distinct addresses even in Master-only
# builds; the bounded user heap and current BSS remain below both stack areas.
IP_SLAVE_STACK_ADDR := 0x060FBFF0
IP_1ST_READ_ADDR := 0x06004000
IP_1ST_READ_SIZE := -1

.PHONY: package diagnostic bootstrap bootstrapfix bootstrapstack bootstrapquarantine masteronlyprobe masteronlyvideo masteronlyvideofix bootstraptrace stage0 stage1 stage2 masteronly emulatorprobe mvp mvpfast mvpfastos mvpfastprof mvpfastmem mvpfastppu mvpfastppufull mvpfastsm83 mvpfastslaveppu mvpfastmastervdp2 mvpfastcache mvpfastfetch mvpfastldh mvpfastanda mvpfastjrz mvpfastcb mvpfastopprof mvpfastf0tight mvpfastblock mvpfastblockc mvpfastcold mvpfastslaveppushadow mvpfastsched mvpfastcompact $(MVP_AUDIT_MODES) stage-roms clean FORCE

package: $(SATURN_ISO)
	@printf -- "$(V_BEGIN_YELLOW)$(notdir $(SATURN_BIN))$(V_END)\n"
	$(ECHO)cp $< $(SATURN_BIN)
	@printf -- "$(V_BEGIN_YELLOW)$(notdir $(SATURN_CUE))$(V_END)\n"
	$(ECHO)printf 'FILE "$(notdir $(SATURN_BIN))" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n' > $(SATURN_CUE)
	$(ECHO)rm -f $<

diagnostic:
	$(MAKE) BUILD_MODE=diagnostic package

stage0:
	$(MAKE) BUILD_MODE=stage0 package

stage1:
	$(MAKE) BUILD_MODE=stage1 package

stage2:
	$(MAKE) BUILD_MODE=stage2 package

bootstrap:
	$(MAKE) BUILD_MODE=bootstrap package

bootstrapfix:
	$(MAKE) BUILD_MODE=bootstrapfix package

bootstrapstack:
	$(MAKE) BUILD_MODE=bootstrapstack package

bootstrapquarantine:
	$(MAKE) BUILD_MODE=bootstrapquarantine package

masteronlyprobe:
	$(MAKE) BUILD_MODE=masteronlyprobe package

masteronlyvideo:
	$(MAKE) BUILD_MODE=masteronlyvideo package

masteronlyvideofix:
	$(MAKE) BUILD_MODE=masteronlyvideofix package

bootstraptrace:
	$(MAKE) BUILD_MODE=bootstraptrace package

masteronly:
	$(MAKE) BUILD_MODE=masteronly package

emulatorprobe:
	$(MAKE) BUILD_MODE=emulatorprobe package

mvp:
	$(MAKE) BUILD_MODE=mvp package

mvpfast:
	$(MAKE) BUILD_MODE=mvpfast package

mvpfastos:
	$(MAKE) BUILD_MODE=mvpfastos package

mvpfastprof:
	$(MAKE) BUILD_MODE=mvpfastprof package

mvpfastmem:
	$(MAKE) BUILD_MODE=mvpfastmem package

mvpfastppu:
	$(MAKE) BUILD_MODE=mvpfastppu package

mvpfastppufull:
	$(MAKE) BUILD_MODE=mvpfastppufull package

mvpfastsm83:
	$(MAKE) BUILD_MODE=mvpfastsm83 package

mvpfastslaveppu:
	$(MAKE) BUILD_MODE=mvpfastslaveppu package

mvpfastmastervdp2:
	$(MAKE) BUILD_MODE=mvpfastmastervdp2 package

mvpfastcache:
	$(MAKE) BUILD_MODE=mvpfastcache package

mvpfastfetch:
	$(MAKE) BUILD_MODE=mvpfastfetch package

mvpfastldh:
	$(MAKE) BUILD_MODE=mvpfastldh package

mvpfastanda:
	$(MAKE) BUILD_MODE=mvpfastanda package

mvpfastrepro:
	$(MAKE) BUILD_MODE=mvpfastrepro package

mvpfastfps:
	$(MAKE) BUILD_MODE=mvpfastfps package

mvpfastnext:
	$(MAKE) BUILD_MODE=mvpfastnext package

mvpfastidle:
	$(MAKE) BUILD_MODE=mvpfastidle package

mvpfastidlecache:
	$(MAKE) BUILD_MODE=mvpfastidlecache package

mvpfastidleevent:
	$(MAKE) BUILD_MODE=mvpfastidleevent package

mvpfastidleresume:
	$(MAKE) BUILD_MODE=mvpfastidleresume package

mvpfastidlecount:
	$(MAKE) BUILD_MODE=mvpfastidlecount package

mvpfastsum:
	$(MAKE) BUILD_MODE=mvpfastsum package

mvpfastidlestat:
	$(MAKE) BUILD_MODE=mvpfastidlestat package

mvpfasthramwait:
	$(MAKE) BUILD_MODE=mvpfasthramwait package

mvpfastcpn8:
	$(MAKE) BUILD_MODE=mvpfastcpn8 package

mvpfastvdp2lines:
	$(MAKE) BUILD_MODE=mvpfastvdp2lines package

mvpfastppueventcoalesce:
	$(MAKE) BUILD_MODE=mvpfastppueventcoalesce package

mvpfastpputwolines:
	$(MAKE) BUILD_MODE=mvpfastpputwolines package

mvpfastppuadaptive:
	$(MAKE) BUILD_MODE=mvpfastppuadaptive package

mvpfastppufourlines:
	$(MAKE) BUILD_MODE=mvpfastppufourlines package

mvpfastppueightlines:
	$(MAKE) BUILD_MODE=mvpfastppueightlines package

mvpfastvdp2dma:
	$(MAKE) BUILD_MODE=mvpfastvdp2dma package

mvpfastvdp2direct:
	$(MAKE) BUILD_MODE=mvpfastvdp2direct package

mvpfastsmpcgate:
	$(MAKE) BUILD_MODE=mvpfastsmpcgate package

mvpfastindexed8:
	$(MAKE) BUILD_MODE=mvpfastindexed8 package

mvpfaststackpair:
	$(MAKE) BUILD_MODE=mvpfaststackpair package

mvpfastdirtyregions:
	$(MAKE) BUILD_MODE=mvpfastdirtyregions package

mvpfastsml:
	$(MAKE) BUILD_MODE=mvpfastsml package

mvpfastzoom:
	$(MAKE) BUILD_MODE=mvpfastzoom package

mvpfastzoom3:
	$(MAKE) BUILD_MODE=mvpfastzoom3 package

mvpfastzoom60:
	$(MAKE) BUILD_MODE=mvpfastzoom60 package

mvpfastzoomtile:
	$(MAKE) BUILD_MODE=mvpfastzoomtile package

mvpfastzoomaudio:
	$(MAKE) BUILD_MODE=mvpfastzoomaudio package

mvpfastzoomaudiomono:
	$(MAKE) BUILD_MODE=mvpfastzoomaudiomono package

mvpfastvdp12:
	$(MAKE) BUILD_MODE=mvpfastvdp12 package

mvpfastjrz:
	$(MAKE) BUILD_MODE=mvpfastjrz package

mvpfastcb:
	$(MAKE) BUILD_MODE=mvpfastcb package

mvpfastopprof:
	$(MAKE) BUILD_MODE=mvpfastopprof package

mvpfastf0tight:
	$(MAKE) BUILD_MODE=mvpfastf0tight package

mvpfastblock:
	$(MAKE) BUILD_MODE=mvpfastblock package

mvpfastblockc:
	$(MAKE) BUILD_MODE=mvpfastblockc package

mvpfastcold:
	$(MAKE) BUILD_MODE=mvpfastcold package

mvpfastslaveppushadow:
	$(MAKE) BUILD_MODE=mvpfastslaveppushadow package

mvpfastsched:
	$(MAKE) BUILD_MODE=mvpfastsched package

mvpfastcompact:
	$(MAKE) BUILD_MODE=mvpfastcompact package

# ROM files are discovered dynamically by tools/package.py. Force the ISO/CUE
# rules so a new or removed ROM is always reflected by `make package`.
FORCE:
$(SATURN_ISO): FORCE

pre-build-iso: stage-roms

stage-roms:

ifneq ($(filter $(BUILD_MODE),mvp mvpfast mvpfastos mvpfastprof mvpfastmem mvpfastppu mvpfastppufull mvpfastsm83 mvpfastslaveppu mvpfastmastervdp2 mvpfastcache mvpfastfetch mvpfastldh mvpfastanda mvpfastjrz mvpfastcb mvpfastopprof mvpfastf0tight mvpfastblock mvpfastblockc mvpfastcold mvpfastslaveppushadow mvpfastsched mvpfastcompact $(MVP_AUDIT_MODES)),)
	python3 tools/package.py --empty --staging $(IMAGE_DIRECTORY)
else
	python3 tools/package.py --roms roms --staging $(IMAGE_DIRECTORY)
endif

include $(YAUL_INSTALL_ROOT)/share/build.pre.mk

# Yaul's stock IP.BIN rule only depends on the template and 1ST_READ.BIN.
# Its metadata comes from Make variables, so without this prerequisite a title,
# product ID, version or load-address change can silently reuse an old header.
# Rebuild it with each package invocation to keep the final image truthful.
$(SH_BUILD_PATH)/IP.BIN: FORCE

# Linker symbols such as the two SH-2 stack tops are supplied from Makefile
# variables rather than source-file dependencies. Relink on each package so a
# changed address cannot leave an otherwise successful build using old values.
$(SH_BUILD_PATH)/$(SH_PROGRAM).elf: FORCE

# build.pre.mk initializes these variables, so the custom startup link inputs
# must be appended only after it has been included.
SH_CFLAGS += -m2 -mb
ifeq ($(BUILD_MODE),bootstraptrace)
# The trace has its own entry and manually performs the relevant init steps;
# linking init.o would pull __sys_init and hide the stage boundaries.
SH_LDFLAGS += -lyaul -lgcc
else
SH_LDFLAGS += $(YAUL_INSTALL_ROOT)/$(YAUL_ARCH_SH_PREFIX)/lib/init.o -lyaul -lgcc
endif

SH_CFLAGS += \
	-O2 \
	-ffunction-sections \
	-fdata-sections \
	-fno-strict-aliasing \
	-I$(abspath src/saturn)

# Keep the legacy SameBoy compatibility surface completely outside the fast
# MVP.  In particular, do not force-include its math shim into PicoGB source
# files or expose feature macros that have no meaning to the compact core.
ifneq ($(filter $(BUILD_MODE),emulator masteronly emulatorprobe mvp),)
SH_CFLAGS += \
	-DGB_INTERNAL \
	-DGB_VERSION=\"1.0.3\" \
	-DGB_DISABLE_CHEATS \
	-DGB_DISABLE_CHEAT_SEARCH \
	-DGB_DISABLE_DEBUGGER \
	-DGB_DISABLE_REWIND \
	-DGB_DISABLE_TIMEKEEPING \
	-include $(abspath src/saturn/math_compat.h) \
	-I$(abspath $(SAMEBOY))
endif

# Audio output is still disabled (sample rate zero). Avoid constructing the
# expensive band-limited filter with millions of software floating-point
# operations before main(); it will be replaced by a host-generated table
# when the SCSP backend is implemented.
ifneq ($(filter $(BUILD_MODE),emulator masteronly emulatorprobe mvp),)
SH_CFLAGS += -DKNOVGBC_SKIP_AUDIO_FILTER_INIT
endif

# SameBoy enables GB_CONTEXT_SAFETY whenever NDEBUG is absent.  That mode
# calls __errno_location() from the hot memory-access path to compare a thread
# identifier.  The Saturn MVP is deliberately single-threaded (Master SH-2
# only), so these debug checks add enormous cost without protecting a valid
# execution scenario.  Keep the change isolated to this measurable MVP.
ifeq ($(BUILD_MODE),mvp)
SH_CFLAGS += -DNDEBUG
endif

# The fast core is intentionally approximate and compact. Compile it and its
# tiny frontend as one LTO partition and remove diagnostic/unwind scaffolding.
# mvpfast and mvpfastos intentionally differ only in the final optimisation
# policy, providing a controlled hardware A/B test for the SH-2's 4 KiB cache.
ifneq ($(filter $(BUILD_MODE),mvpfast mvpfastos mvpfastprof mvpfastmem mvpfastppu mvpfastppufull mvpfastsm83 mvpfastslaveppu mvpfastmastervdp2 mvpfastcache mvpfastfetch mvpfastldh mvpfastanda mvpfastjrz mvpfastcb mvpfastopprof mvpfastf0tight mvpfastblock mvpfastblockc mvpfastcold mvpfastslaveppushadow mvpfastsched mvpfastcompact $(MVP_AUDIT_MODES)),)
SH_CFLAGS += \
	-DNDEBUG \
	-DPICOGB_CUSTOM=1 \
	-flto-partition=one \
	-fno-unwind-tables \
	-fno-asynchronous-unwind-tables \
	-fno-stack-protector \
	-I$(abspath $(PICOGB))
endif

ifeq ($(BUILD_MODE),mvpfast)
SH_CFLAGS += -O3
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastos)
SH_CFLAGS += -Os
SH_LDFLAGS += -Os -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastprof)
# Keep the O3 policy used by kmvp0003. The only intentional behavioural
# difference is the lightweight component sampler selected below.
SH_CFLAGS += -O3 -DKNOVGBC_COMPONENT_PROFILE=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastmem)
# Keep all O3 policies and MVP feature limits unchanged. This single flag
# selects only the generic direct-map memory fast path in PicoGB.
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastppu)
# Cumulative, one-change PPU experiment: retain the validated memory path and
# cache only the three DMG palette conversions used by rendered pixels.
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastppufull)
# Full, still DMG-only PPU path. The exact C fallback participates in host
# regressions; the Saturn build also links the leaf SH-2 tile-line blitter.
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1 \
	-DKNOVGBC_PPU_FULL_OPT=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastsm83)
# Preserve the exact kmvpf008 configuration.  The sole CPU change is the
# hybrid SM83 assembly backend selected explicitly below.
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1 \
	-DKNOVGBC_PPU_FULL_OPT=1 -DKNOVGBC_SH2_SM83_ASM=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastslaveppu)
# Keep the reference PPU policy (C, O3 and palette cache) but leave all
# interpreter and full-renderer assembly experiments out of this probe.
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastmastervdp2)
# This is the tested all-C runtime configuration: Master-only startup,
# compact memory dispatch and palette-cache PPU, with VDP2 as the sole video
# output. Do not add Slave, VDP1 or assembly experiment flags here.
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastcache)
# Exact kmvpm011 runtime plus one cache policy experiment.  Data hits remain
# valid, but data misses no longer allocate lines in the unified 4 KiB cache.
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1 \
	-DKNOVGBC_CACHE_DATA_NO_REPL=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastfetch)
# Exact kmvpm011 runtime plus a single code-layout experiment: direct ROM
# opcode prefetch at the shared fetch site, with the existing full fallback.
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1 \
	-DKNOVGBC_OPCODE_FETCH_INLINE=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastldh)
# Exact kmvpf016 runtime plus a direct read for LDH A,(a8) within ordinary
# HRAM.  FF00-FF7F and FFFF deliberately remain on the old dispatcher.
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1 \
	-DKNOVGBC_OPCODE_FETCH_INLINE=1 -DKNOVGBC_LDH_HRAM_DIRECT=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastanda)
# Exact kmvpl017 runtime plus a direct, in-interpreter path for A7/AND A.
# It intentionally keeps the generic decoder for every other opcode.
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1 \
	-DKNOVGBC_OPCODE_FETCH_INLINE=1 -DKNOVGBC_LDH_HRAM_DIRECT=1 \
	-DKNOVGBC_A7_DIRECT=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastrepro)
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1 \
	-DKNOVGBC_OPCODE_FETCH_INLINE=1 -DKNOVGBC_LDH_HRAM_DIRECT=1 \
	-DKNOVGBC_A7_DIRECT=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastfps)
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1 \
	-DKNOVGBC_OPCODE_FETCH_INLINE=1 -DKNOVGBC_LDH_HRAM_DIRECT=1 \
	-DKNOVGBC_A7_DIRECT=1 -DKNOVGBC_FPS_ONLY=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastnext)
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1 \
	-DKNOVGBC_OPCODE_FETCH_INLINE=1 -DKNOVGBC_LDH_HRAM_DIRECT=1 \
	-DKNOVGBC_A7_DIRECT=1 -DKNOVGBC_F0_ROM_OPERAND_INLINE=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifneq ($(filter $(MVP_FEATURE_MODE),mvpfastidle mvpfastidlecache mvpfastidleevent mvpfastidleresume mvpfastidlecount mvpfastsum mvpfastidlestat mvpfasthramwait mvpfastcpn8 mvpfastvdp2lines mvpfastppueventcoalesce mvpfastpputwolines mvpfastppuadaptive mvpfastppufourlines mvpfastppueightlines mvpfastvdp2dma mvpfastvdp2direct mvpfastsmpcgate mvpfastindexed8 mvpfaststackpair mvpfastdirtyregions mvpfastvdp12),)
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1 \
	-DKNOVGBC_OPCODE_FETCH_INLINE=1 -DKNOVGBC_LDH_HRAM_DIRECT=1 \
	-DKNOVGBC_A7_DIRECT=1 -DKNOVGBC_F0_ROM_OPERAND_INLINE=1 \
	-DKNOVGBC_IDLE_SPIN=1
ifeq ($(BUILD_MODE),mvpfastidlecache)
SH_CFLAGS += -DKNOVGBC_IDLE_SPIN_CACHE=1
endif
ifneq ($(filter $(MVP_FEATURE_MODE),mvpfastidleevent mvpfastidleresume mvpfastidlecount mvpfastsum mvpfastidlestat mvpfasthramwait mvpfastcpn8 mvpfastvdp2lines mvpfastppueventcoalesce mvpfastpputwolines mvpfastppuadaptive mvpfastppufourlines mvpfastppueightlines mvpfastvdp2dma mvpfastvdp2direct mvpfastsmpcgate mvpfastindexed8 mvpfaststackpair mvpfastdirtyregions mvpfastvdp12),)
SH_CFLAGS += -DKNOVGBC_IDLE_EVENT=1
endif
ifneq ($(filter $(MVP_FEATURE_MODE),mvpfastidleresume mvpfastidlecount mvpfastsum mvpfastidlestat mvpfasthramwait mvpfastcpn8 mvpfastvdp2lines mvpfastppueventcoalesce mvpfastpputwolines mvpfastppuadaptive mvpfastppufourlines mvpfastppueightlines mvpfastvdp2dma mvpfastvdp2direct mvpfastsmpcgate mvpfastindexed8 mvpfaststackpair mvpfastdirtyregions mvpfastvdp12),)
SH_CFLAGS += -DKNOVGBC_IDLE_RESUME=1
endif
ifneq ($(filter $(MVP_FEATURE_MODE),mvpfastidlecount mvpfastsum mvpfastidlestat mvpfasthramwait mvpfastcpn8 mvpfastvdp2lines mvpfastppueventcoalesce mvpfastpputwolines mvpfastppuadaptive mvpfastppufourlines mvpfastppueightlines mvpfastvdp2dma mvpfastvdp2direct mvpfastsmpcgate mvpfastindexed8 mvpfaststackpair mvpfastdirtyregions mvpfastvdp12),)
SH_CFLAGS += -DKNOVGBC_IDLE_COUNTDOWN=1
endif
ifeq ($(BUILD_MODE),mvpfastsum)
SH_CFLAGS += -DKNOVGBC_SUM_LOOP=1
endif
ifeq ($(BUILD_MODE),mvpfastidlestat)
SH_CFLAGS += -DKNOVGBC_IDLE_STAT=1
endif
ifneq ($(filter $(MVP_FEATURE_MODE),mvpfasthramwait mvpfastcpn8 mvpfastvdp2lines mvpfastppueventcoalesce mvpfastpputwolines mvpfastppuadaptive mvpfastppufourlines mvpfastppueightlines mvpfastvdp2dma mvpfastvdp2direct mvpfastsmpcgate mvpfastindexed8 mvpfaststackpair mvpfastdirtyregions mvpfastvdp12),)
SH_CFLAGS += -DKNOVGBC_HRAM_WAIT=1
endif
ifneq ($(filter $(MVP_FEATURE_MODE),mvpfastppueventcoalesce mvpfastpputwolines mvpfastppuadaptive mvpfastppufourlines mvpfastppueightlines mvpfastvdp2dma mvpfastvdp2direct mvpfastsmpcgate mvpfastindexed8 mvpfaststackpair mvpfastdirtyregions mvpfastvdp12),)
SH_CFLAGS += -DKNOVGBC_PPU_EVENT_COALESCE=1
endif
ifneq ($(filter $(MVP_FEATURE_MODE),mvpfastpputwolines mvpfastppuadaptive mvpfastppufourlines mvpfastppueightlines mvpfastvdp2dma mvpfastvdp2direct mvpfastsmpcgate mvpfastindexed8 mvpfaststackpair mvpfastdirtyregions mvpfastvdp12),)
SH_CFLAGS += -DKNOVGBC_PPU_TWO_LINES=1
endif
ifneq ($(filter $(MVP_FEATURE_MODE),mvpfastppuadaptive mvpfastvdp2dma mvpfastvdp2direct mvpfastsmpcgate mvpfastindexed8 mvpfaststackpair mvpfastdirtyregions mvpfastvdp12),)
SH_CFLAGS += -DKNOVGBC_PPU_ADAPTIVE_BATCH=1
endif

ifeq ($(BUILD_MODE),mvpfastvdp2dma)
SH_CFLAGS += -DKNOVGBC_VDP2_DMA=1
endif
ifneq ($(filter $(MVP_FEATURE_MODE),mvpfastvdp2direct mvpfastsmpcgate mvpfastindexed8 mvpfaststackpair mvpfastdirtyregions mvpfastvdp12),)
SH_CFLAGS += -DKNOVGBC_VDP2_DMA_DIRECT=1
endif
ifneq ($(filter $(MVP_FEATURE_MODE),mvpfastsmpcgate mvpfastindexed8 mvpfaststackpair mvpfastdirtyregions mvpfastvdp12),)
SH_CFLAGS += -DKNOVGBC_SMPC_NONBLOCKING=1
endif
ifneq ($(filter $(MVP_FEATURE_MODE),mvpfastindexed8 mvpfaststackpair mvpfastdirtyregions mvpfastvdp12),)
SH_CFLAGS += -DKNOVGBC_VDP2_INDEXED8=1
endif
ifneq ($(filter $(MVP_FEATURE_MODE),mvpfaststackpair mvpfastdirtyregions mvpfastvdp12),)
SH_CFLAGS += -DKNOVGBC_STACK_PAIR_WRAM=1
endif
ifeq ($(MVP_FEATURE_MODE),mvpfastdirtyregions)
SH_CFLAGS += -DKNOVGBC_PPU_DIRTY_SCANLINES=1
endif
ifeq ($(BUILD_MODE),mvpfastsml)
SH_CFLAGS += -DKNOVGBC_SML_MBC1=1
endif
ifneq ($(filter mvpfastzoom mvpfastzoom3 mvpfastzoom60 mvpfastzoomtile mvpfastzoomaudio mvpfastzoomaudiomono,$(BUILD_MODE)),)
SH_CFLAGS += -DKNOVGBC_VDP2_ZOOM=1
endif
ifneq ($(filter mvpfastzoom3 mvpfastzoom60 mvpfastzoomtile mvpfastzoomaudio mvpfastzoomaudiomono,$(BUILD_MODE)),)
SH_CFLAGS += -DKNOVGBC_HUD_3_DIGITS=1
endif
ifneq ($(filter mvpfastzoom60 mvpfastzoomtile mvpfastzoomaudio mvpfastzoomaudiomono,$(BUILD_MODE)),)
SH_CFLAGS += -DKNOVGBC_NO_FRAMESKIP=1
endif
ifneq ($(filter mvpfastzoomtile mvpfastzoomaudio mvpfastzoomaudiomono,$(BUILD_MODE)),)
SH_CFLAGS += -DKNOVGBC_PPU_TILE_DIRTY_LINES=1
endif
ifneq ($(filter mvpfastzoomaudio mvpfastzoomaudiomono,$(BUILD_MODE)),)
SH_CFLAGS += -DKNOVGBC_AUDIO=1
endif
ifeq ($(BUILD_MODE),mvpfastzoomaudiomono)
SH_CFLAGS += -DKNOVGBC_APU_MONO_FAST=1
endif
ifeq ($(BUILD_MODE),mvpfastvdp12)
SH_CFLAGS += -DKNOVGBC_VDP12_NATIVE=1
endif
ifeq ($(BUILD_MODE),mvpfastppufourlines)
SH_CFLAGS += -DKNOVGBC_PPU_FIXED_LINES=4
endif
ifeq ($(BUILD_MODE),mvpfastppueightlines)
SH_CFLAGS += -DKNOVGBC_PPU_FIXED_LINES=8
endif
ifeq ($(BUILD_MODE),mvpfastcpn8)
SH_CFLAGS += -DKNOVGBC_CP_N8_DIRECT=1
endif
ifeq ($(BUILD_MODE),mvpfastvdp2lines)
SH_CFLAGS += -DKNOVGBC_VDP2_DIRTY_LINES=1
endif
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastjrz)
# Exact kmvpa018 runtime plus a direct, in-interpreter path for JR Z,e8.
# Every other conditional branch stays on the existing generic decoder.
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1 \
	-DKNOVGBC_OPCODE_FETCH_INLINE=1 -DKNOVGBC_LDH_HRAM_DIRECT=1 \
	-DKNOVGBC_A7_DIRECT=1 -DKNOVGBC_JR_Z_DIRECT=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastcb)
# Exact kmvpa018 runtime plus an outlined all-C handler for the rare CB
# prefix. No Slave, VDP1, scheduler, compact decoder or SH-2 assembly enters.
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1 \
	-DKNOVGBC_OPCODE_FETCH_INLINE=1 -DKNOVGBC_LDH_HRAM_DIRECT=1 \
	-DKNOVGBC_A7_DIRECT=1 -DKNOVGBC_CB_COLD=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastopprof)
# Exact kmvpa018 configuration plus an isolated diagnostic. No experimental
# decoder, scheduler, Slave or assembly path participates in this build.
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1 \
	-DKNOVGBC_OPCODE_FETCH_INLINE=1 -DKNOVGBC_LDH_HRAM_DIRECT=1 \
	-DKNOVGBC_A7_DIRECT=1 -DKNOVGBC_OPCODE_PROFILE=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastf0tight)
# Exact kmvpa018 baseline. The only additional definition selects the local
# F0 straight-line path in the reference C interpreter.
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1 \
	-DKNOVGBC_OPCODE_FETCH_INLINE=1 -DKNOVGBC_LDH_HRAM_DIRECT=1 \
	-DKNOVGBC_A7_DIRECT=1 -DKNOVGBC_LDH_F0_TIGHT=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastblock)
# Exact kmvpa018 baseline plus the conservative ROM-only block recompiler.
# It does not enable the old assembly backend, scheduler, compact frontend or
# F0-tight experiment; every unsupported opcode remains on mb_exec().
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1 \
	-DKNOVGBC_OPCODE_FETCH_INLINE=1 -DKNOVGBC_LDH_HRAM_DIRECT=1 \
	-DKNOVGBC_A7_DIRECT=1 -DKNOVGBC_BLOCK_RECOMPILER=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastblockc)
# Exact kmvpa018 baseline plus a compact, predecoded C basic-block executor.
# This is intentionally distinct from kmvpr023: no generated Assembly,
# executable arena, cache purge, Slave, scheduler or compact decoder is used.
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1 \
	-DKNOVGBC_OPCODE_FETCH_INLINE=1 -DKNOVGBC_LDH_HRAM_DIRECT=1 \
	-DKNOVGBC_A7_DIRECT=1 -DKNOVGBC_BLOCK_STATIC=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastcold)
# Exact kmvpa018 baseline, plus a single structural change: complete rare
# opcode handlers are physically outlined from mb_exec.  It adds no scheduler,
# Slave, VDP1 or SH-2 assembly path.
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1 \
	-DKNOVGBC_OPCODE_FETCH_INLINE=1 -DKNOVGBC_LDH_HRAM_DIRECT=1 \
	-DKNOVGBC_A7_DIRECT=1 -DKNOVGBC_CB_COLD=1 \
	-DKNOVGBC_HOT_COLD_SPLIT=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastslaveppushadow)
# Preserve the all-C Master+VDP2 baseline. The only added work is a Slave
# shadow renderer that compares private P2 buffers and never drives VDP2.
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1 \
	-DKNOVGBC_SLAVE_PPU_SHADOW=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastsched)
# Same Master-only C/VDP2 baseline, with device time committed only at safe
# timer/PPU/I-O boundaries. No Slave, VDP1 or assembly backend is involved.
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1 \
	-DKNOVGBC_SCHEDULER_BATCH=1
SH_LDFLAGS += -O3 -flto-partition=one
else ifeq ($(BUILD_MODE),mvpfastcompact)
# The all-C Master+VDP2 baseline plus a target-local compact SM83 front end.
# It contains no Slave, VDP1, scheduler or SH-2 assembly backend.
SH_CFLAGS += -O3 -DKNOVGBC_FAST_MEMORY=1 -DKNOVGBC_PPU_PALETTE_CACHE=1 \
	-DKNOVGBC_SM83_COMPACT=1
SH_LDFLAGS += -O3 -flto-partition=one
endif

ifeq ($(BUILD_MODE),emulatorprobe)
SH_CFLAGS += -DKNOVGBC_BOOT_DIAGNOSTICS
endif

include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk

# Yaul maps every build mode to the same object paths, while several modes use
# different compiler definitions and source lists. Recompile the selected
# objects on each explicit build so switching from a probe to the emulator can
# never reuse an object made with the wrong CFLAGS. This also tracks the binary
# files consumed through sameboy_boot_roms.sx's .incbin directives.
$(SH_OBJS_UNIQ): FORCE
