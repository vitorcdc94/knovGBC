#include "platform.h"

#include "gb.h"

#include <yaul.h>

#include <stdbool.h>

void
saturn_platform_init(void)
{
    saturn_platform_video_init();
    (void)saturn_platform_services_init();
}

bool
saturn_platform_services_init(void)
{
    /* The MVP ROM is already part of 1ST_READ.BIN. Only controller services
     * are needed; no CD block or CDFS code enters this executable. */
    smpc_peripheral_init();
    smpc_peripheral_intback_issue();
    return true;
}

void
saturn_platform_update_input(GB_gameboy_t *gb)
{
    smpc_peripheral_digital_t digital = {0};
    GB_key_mask_t keys = 0;

    smpc_peripheral_process();
    smpc_peripheral_digital_port(1, &digital);
    smpc_peripheral_intback_issue();

    if (digital.connected) {
        if (digital.pressed.button.right) {
            keys |= GB_KEY_RIGHT_MASK;
        }
        if (digital.pressed.button.left) {
            keys |= GB_KEY_LEFT_MASK;
        }
        if (digital.pressed.button.up) {
            keys |= GB_KEY_UP_MASK;
        }
        if (digital.pressed.button.down) {
            keys |= GB_KEY_DOWN_MASK;
        }
        if (digital.pressed.button.a) {
            keys |= GB_KEY_A_MASK;
        }
        if (digital.pressed.button.b) {
            keys |= GB_KEY_B_MASK;
        }
        if (digital.pressed.button.x) {
            keys |= GB_KEY_SELECT_MASK;
        }
        if (digital.pressed.button.start) {
            keys |= GB_KEY_START_MASK;
        }
    }

    GB_set_key_mask(gb, keys);
}
