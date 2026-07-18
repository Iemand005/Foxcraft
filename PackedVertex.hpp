#pragma once

#include <cstdint>

#pragma pack(push, 1)
struct FoxcraftPackedVertex {
    int16_t x, y, z;
    uint8_t normalLayer; // bits: nnnlllll (normal=3, layer=5)
};
#pragma pack(pop)
static_assert(sizeof(FoxcraftPackedVertex) == 7, "FoxcraftPackedVertex must be 7 bytes");
