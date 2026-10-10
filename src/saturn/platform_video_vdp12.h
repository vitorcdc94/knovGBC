#pragma once

#include "picogb_vdp12_frame.h"

#include <stdbool.h>

/* Experimental native presentation. The original bitmap presenter remains
 * available in platform_video.c for the baseline build. */
/* Returns false without changing display hardware if VDP1 VRAM is too small. */
bool saturn_vdp12_init(void);
void saturn_vdp12_present(const struct picogb_vdp12_frame *frame);
void saturn_vdp12_hud_draw(const char *line_1, const char *line_2,
    const char *line_3);
