#include "picogb_mvp_input.h"

#include "fabric.h"

#include <yaul.h>

#include <stdint.h>

/* PicoGB's JOYP_RAW layout is deliberately host-facing rather than the
 * active-low value read at FF00. */
#define PICOGB_KEY_A      0x01U
#define PICOGB_KEY_B      0x02U
#define PICOGB_KEY_SELECT 0x04U
#define PICOGB_KEY_START  0x08U
#define PICOGB_KEY_RIGHT  0x10U
#define PICOGB_KEY_LEFT   0x20U
#define PICOGB_KEY_UP     0x40U
#define PICOGB_KEY_DOWN   0x80U

void
picogb_mvp_input_init(void)
{
    smpc_peripheral_init();
    smpc_peripheral_intback_issue();
}

void
picogb_mvp_input_update(struct pgf_userdata_t *fabric)
{
    smpc_peripheral_digital_t digital = {0};
    uint32_t keys = 0;

    smpc_peripheral_process();
    smpc_peripheral_digital_port(1, &digital);
#if defined(KNOVGBC_SMPC_NONBLOCKING)
    /* Yaul's issue helper waits for SF to clear before starting INTBACK.
     * If the previous collection is still running, use the last completed
     * pad state and retry on the next logical frame instead of stalling GB. */
    if ((MEMORY_READ(8, SMPC(SF)) & 0x01U) == 0U) {
        smpc_peripheral_intback_issue();
    }
#else
    smpc_peripheral_intback_issue();
#endif

    if (digital.connected) {
        if (digital.pressed.button.right) {
            keys |= PICOGB_KEY_RIGHT;
        }
        if (digital.pressed.button.left) {
            keys |= PICOGB_KEY_LEFT;
        }
        if (digital.pressed.button.up) {
            keys |= PICOGB_KEY_UP;
        }
        if (digital.pressed.button.down) {
            keys |= PICOGB_KEY_DOWN;
        }
        if (digital.pressed.button.a) {
            keys |= PICOGB_KEY_A;
        }
        if (digital.pressed.button.b) {
            keys |= PICOGB_KEY_B;
        }
        if (digital.pressed.button.x) {
            keys |= PICOGB_KEY_SELECT;
        }
        if (digital.pressed.button.start) {
            keys |= PICOGB_KEY_START;
        }
    }

    /* A newly pressed key produces the Game Boy joypad interrupt.  Tetris
     * normally polls FF00, but this also wakes games that wait in HALT for
     * input and avoids retriggering while a key remains held. */
    if ((keys & ~fabric->JOYP_RAW) != 0U) {
        fabric->mb->IF |= 0x10U;
    }
    fabric->JOYP_RAW = keys;
}
