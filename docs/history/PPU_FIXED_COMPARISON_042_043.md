# Controlled comparison: kmvpe040, kmvpe041, kmvpe042 and kmvpe043

Date: 2026-09-25. kmvpe041 is the reference selected after Saturn testing:
approximately 47.9 logical FPS (23.9 visual FPS), T21, G16, P06. kmvpe040
recorded approximately 47.9/23.9 FPS, T21, G17, P06. The new 042/043
builds have not yet been measured on hardware.

## Differences between versions

- 040: limit of two visible lines per HRAM/PPU wait group.
- 041: adaptive batching up to the next CPU-visible event (current,
  preserved reference).
- 042: starts from the 040 configuration and attempts four lines per group;
  near VBlank, falls back to two or one line.
- 043: starts from the 040 configuration and attempts eight lines per group;
  near VBlank, falls back to four, two or one line.

042/043 retain the same ROM, SM83, rendering of all lines, VDP2, frameskip
and controls as 040. No image line is discarded. LCD, timer, STAT and IRQ
checks continue blocking batching when necessary. Only the preprocessed
`picogb_hram_wait.c` differs from 040 in each build.

## Host checks

- SH-2 builds 042 and 043 completed. Both include `picogb_hram_wait_try`
  in the final map.
- Comparative contract: 671 cases per version, including line start,
  boundaries of lines 135–143, VBlank, STAT and IRQ. SM83/PPU states,
  cycles, memory and pixels matched instruction-by-instruction execution.
  ASan/UBSan passed for 042 and 043.
- Differential traces over 600, 1,800 and 3,600 frames: 040, 042 and 043
  produced exactly the same per-frame traces, including gameplay input.
  SHA-256 of the 3,600-frame trace for all three:
  `990B09596E1C6C669F774D214B648DA37C6FE14C208185D925909069803066DE`.
- HRAM/PPU wait groups over 3,600 frames: 040 = 256,106; 042 = 138,167;
  043 = 80,119. Admission calls (3,592), actual simulated instructions
  (23,564,405) and interpreter calls (3,125,785) remained identical.
  Fewer groups do not guarantee higher FPS on Saturn.
- 040 and 041 BINs preserved, with SHA-256 respectively
  `B7F6F4CA13F8C91AF53A084FF18DBE86C0C7F73EB9B55F42A18D623459DDA1BA`
  and `A677FE3F82ADF0EB28813DBF2D50FC69835BE59B1F9E3DD3F6DBCFC67EA2E35C`.

## Images to test

- `dist/kmvpe042.bin`: 606,208 bytes, SHA-256
  `90B18E723F381D4C4C1E055A5FA6F242E50DF6FDA91F72100B4C1271ACF499DD`;
  `dist/kmvpe042.cue` references this BIN as MODE1/2048.
- `dist/kmvpe043.bin`: 606,208 bytes, SHA-256
  `8F07A603081FC3FBC4710E5C35F89F8181D4230C25A661B62F721480FBF8560B`;
  `dist/kmvpe043.cue` references this BIN as MODE1/2048.

## Decision after Saturn testing

Test 042 and 043 on the same screen and during a few minutes of gameplay,
comparing logical/visual FPS, T, G, X and P against 041. Observe image,
input and stability. Adopt a new reference only if hardware gains are
consistent and regression-free.
