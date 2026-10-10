#pragma once

#include <stdint.h>

#define CPU_FRT_PAL_320_128_COUNT_1MS 208U
#define CPU_FRT_NTSC_320_128_COUNT_1MS 210U
#define VDP2_TVMD_TV_STANDARD_PAL 1

uint8_t cpu_intc_mask_get(void);
void cpu_intc_mask_set(uint8_t mask);
uint8_t smpc_smc_sndoff_call(void);
uint8_t smpc_smc_sndon_call(void);
int vdp2_tvmd_tv_standard_get(void);
