#pragma once

#include <MeshArray.hpp>
#include "Chunk.hpp"
#include "ChunkManager.hpp"

class ChunkMesher {
	static constexpr int WIDTH = 16, HEIGHT = 128, DEPTH = 16;
public:
    static MeshArray BuildMesh(Chunk& chunk, ChunkManager& manager) {
        MeshArray mesh;
        glm::ivec2 coord = chunk.coord;

        for (int x = 0; x < WIDTH; x++) {
            for (int y = 0; y < HEIGHT; y++) {
                for (int z = 0; z < DEPTH; z++) {
                    BlockType block = chunk.GetBlock(x, y, z);
                    if (block == BlockType::Air) continue;

                    int worldX = coord.x * WIDTH + x;
                    int worldZ = coord.y * DEPTH + z;

                    // for each of 6 directions, check neighbor via manager
                    if (manager.GetBlock(worldX - 1, y, worldZ) == BlockType::Air)
                        mesh.AddFace(fe::PlaneDirection::Left, x, y, z, block);
                    // ...repeat for +x, -y, +y, -z, +z
                }
            }
        }
        return mesh;
    }
};