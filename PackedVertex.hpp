#pragma once

#include <cstdint>
#include <vector>

#include <Vertex.hpp>

#pragma pack(push, 1)
struct FoxcraftPackedVertex {
    int16_t x, y, z;
    uint8_t normalLayer; // bits: nnnlllll (normal=3, layer=5)

    static std::vector<fe::VertexAttribute> getLayout() {
        return {
            { 0, 3, 0, fe::VertexAttribType::Short },
            { 1, 1, 6, fe::VertexAttribType::UByte },
        };
    }
};
#pragma pack(pop)
static_assert(sizeof(FoxcraftPackedVertex) == 7, "FoxcraftPackedVertex must be 7 bytes");
