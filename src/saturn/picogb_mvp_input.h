#pragma once

struct pgf_userdata_t;

/* PicoGB has no platform layer.  These two small functions keep Yaul's SMPC
 * controller polling out of the emulator loop and out of the vendor core. */
void picogb_mvp_input_init(void);
void picogb_mvp_input_update(struct pgf_userdata_t *fabric);

