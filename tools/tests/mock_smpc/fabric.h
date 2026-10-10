#pragma once

#include <stdint.h>

struct mb_state {
    uint8_t IF;
};

struct pgf_userdata_t {
    struct mb_state *mb;
    uint32_t JOYP_RAW;
};
