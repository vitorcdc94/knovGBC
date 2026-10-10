#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool a;
    bool b;
    bool x;
    bool start;
    bool right;
    bool left;
    bool up;
    bool down;
} smpc_mock_buttons_t;

typedef struct {
    bool connected;
    struct {
        smpc_mock_buttons_t button;
    } pressed;
} smpc_peripheral_digital_t;

extern volatile uint8_t mock_smpc_sf;

#define SMPC(register_name) (&mock_smpc_sf)
#define MEMORY_READ(width, address) (*(volatile uint8_t *)(address))

void smpc_peripheral_init(void);
void smpc_peripheral_process(void);
void smpc_peripheral_digital_port(unsigned int port,
    smpc_peripheral_digital_t *digital);
void smpc_peripheral_intback_issue(void);
