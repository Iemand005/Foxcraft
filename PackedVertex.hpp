#pragma once

#include <cstdint>

struct FoxcraftPackedVertex {
    int16_t x, y, z;
    uint8_t normal;
    uint8_t u;
    uint8_t v;
    uint8_t layer;
};
