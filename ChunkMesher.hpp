#pragma once

#include <MeshArray.hpp>
#include "CChunk.hpp"

class ChunkMesher {
public:
    static MeshArray BuildMesh(Chunk& chunk, ChunkManager& manager) {
        Mesh mesh;
        ChunkCoord coord = chunk.GetCoord();

        for (int x = 0; x < WIDTH; x++) {
            for (int y = 0; y < HEIGHT; y++) {
                for (int z = 0; z < DEPTH; z++) {
                    BlockType block = chunk.GetBlock(x, y, z);
                    if (block == BlockType::Air) continue;

                    int worldX = coord.x * WIDTH + x;
                    int worldZ = coord.y * DEPTH + z;

                    // for each of 6 directions, check neighbor via manager
                    if (manager.GetBlock(worldX - 1, y, worldZ) == BlockType::Air)
                        mesh.AddFace(Face::Left, x, y, z, block);
                    // ...repeat for +x, -y, +y, -z, +z
                }
            }
        }
        return mesh;
    }
};