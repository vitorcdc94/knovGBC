#include "fabric.h"
#include "picogb_mvp_input.h"
#include "yaul.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

volatile uint8_t mock_smpc_sf;

static unsigned int request_count;
static unsigned int process_count;
static smpc_peripheral_digital_t latest;
static smpc_peripheral_digital_t completed;
static bool completion_ready;

void
smpc_peripheral_init(void)
{
    mock_smpc_sf = 0;
    request_count = 0;
    process_count = 0;
    completion_ready = false;
    latest = (smpc_peripheral_digital_t){0};
}

void
smpc_peripheral_process(void)
{
    process_count++;
    if (completion_ready) {
        latest = completed;
        completion_ready = false;
    }
}

void
smpc_peripheral_digital_port(unsigned int port,
    smpc_peripheral_digital_t *digital)
{
    assert(port == 1);
    *digital = latest;
}

void
smpc_peripheral_intback_issue(void)
{
    /* This would spin in real Yaul while SF is busy. */
    assert((mock_smpc_sf & 1U) == 0U);
    assert(!completion_ready);
    request_count++;
    mock_smpc_sf = 1;
}

static void
mark_collection_complete(bool connected, smpc_mock_buttons_t buttons)
{
    assert(mock_smpc_sf == 1U);
    completed = (smpc_peripheral_digital_t){0};
    completed.connected = connected;
    completed.pressed.button = buttons;
    completion_ready = true;
}

static void
finish_collection(bool connected, smpc_mock_buttons_t buttons)
{
    mark_collection_complete(connected, buttons);
    mock_smpc_sf = 0;
}

static void
step_and_check(struct pgf_userdata_t *fabric, unsigned int expected_requests,
    uint32_t expected_keys, uint8_t expected_joypad_irq)
{
    fabric->mb->IF = 0;
    picogb_mvp_input_update(fabric);
    assert(request_count == expected_requests);
    assert(fabric->JOYP_RAW == expected_keys);
    assert(fabric->mb->IF == expected_joypad_irq);
}

int
main(void)
{
    struct mb_state cpu = {0};
    struct pgf_userdata_t fabric = {.mb = &cpu};

    picogb_mvp_input_init();
    assert(request_count == 1U);
    assert(mock_smpc_sf == 1U);

    /* Busy: keep using the old sample, without trying a second command. */
    step_and_check(&fabric, 1U, 0U, 0U);
    step_and_check(&fabric, 1U, 0U, 0U);

    /* Yaul can mark the collection complete before SF clears. Consume its
     * sample now, but do not request another command until SMPC is free. */
    mark_collection_complete(true, (smpc_mock_buttons_t){.a = true});
    step_and_check(&fabric, 1U, 0x01U, 0x10U);
    mock_smpc_sf = 0;
    step_and_check(&fabric, 2U, 0x01U, 0U);
    step_and_check(&fabric, 2U, 0x01U, 0U);

    finish_collection(true,
        (smpc_mock_buttons_t){.a = true, .right = true});
    step_and_check(&fabric, 3U, 0x11U, 0x10U);

    finish_collection(true,
        (smpc_mock_buttons_t){.a = true, .right = true});
    step_and_check(&fabric, 4U, 0x11U, 0U);

    finish_collection(true, (smpc_mock_buttons_t){0});
    step_and_check(&fabric, 5U, 0U, 0U);

    finish_collection(true, (smpc_mock_buttons_t){
        .a = true, .b = true, .x = true, .start = true,
        .right = true, .left = true, .up = true, .down = true
    });
    step_and_check(&fabric, 6U, 0xFFU, 0x10U);

    finish_collection(false, (smpc_mock_buttons_t){0});
    step_and_check(&fabric, 7U, 0U, 0U);

    assert(process_count == 10U);
    puts("nonblocking SMPC input contract: PASS");
    return 0;
}
