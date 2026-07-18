#pragma once

#include <Mesh.hpp>
#include "Chunk.hpp"
#include "ChunkManager.hpp"
#include "PackedVertex.hpp"

static int EncodeNormal(const glm::vec3& n) {
    if (n.x > 0) return 0;
    if (n.x < 0) return 1;
    if (n.y > 0) return 2;
    if (n.y < 0) return 3;
    if (n.z > 0) return 4;
    return 5;
}

static FoxcraftPackedVertex MakePackedVertex(const glm::vec3& pos, const glm::vec3& normal, float layer) {
    FoxcraftPackedVertex vtx{};
    vtx.x = static_cast<int16_t>(pos.x);
    vtx.y = static_cast<int16_t>(pos.y);
    vtx.z = static_cast<int16_t>(pos.z);
    uint8_t faceIdx = static_cast<uint8_t>(EncodeNormal(normal));
    vtx.normalLayer = faceIdx | (static_cast<uint8_t>(layer) << 3);
    return vtx;
}

class ChunkMesher {
public:
	static void BuildMesh(std::shared_ptr<Chunk> chunk, ChunkManager *manager, bool cullBottomFaces = true) {
		std::vector<FoxcraftPackedVertex> allVertices;
		std::vector<unsigned int> allIndices;
		allVertices.reserve(4096);
		allIndices.reserve(6144);

		auto getBlockAt = [&](const glm::ivec3& pos) -> BlockType {
			if (pos.x >= 0 && pos.x < Chunk::WIDTH && pos.y >= 0 && pos.y < Chunk::HEIGHT && pos.z >= 0 && pos.z < Chunk::DEPTH)
				return chunk->GetBlock(pos.x, pos.y, pos.z);
			if (pos.y < 0 || pos.y >= Chunk::HEIGHT)
				return BlockType::Air;
			int worldX = chunk->coord.x * Chunk::WIDTH + pos.x;
			int worldZ = chunk->coord.y * Chunk::DEPTH + pos.z;
			auto neighborCoord = manager->WorldToChunkCoord(worldX, worldZ);
			auto neighbor = manager->GetChunk(neighborCoord);
			if (!neighbor) return BlockType::Air;
			int localX = worldX - neighborCoord.x * Chunk::WIDTH;
			int localZ = worldZ - neighborCoord.y * Chunk::DEPTH;
			return neighbor->GetBlock(localX, pos.y, localZ);
		};

		const int dims[3] = {Chunk::WIDTH, Chunk::HEIGHT, Chunk::DEPTH};

		for (int axis = 0; axis < 3; axis++) {
			int u = (axis + 1) % 3;
			int v = (axis + 2) % 3;

			int axisDim = dims[axis];
			int uDim = dims[u];
			int vDim = dims[v];

			glm::ivec3 q(0);
			q[axis] = 1;

			for (bool backFace : {false, true}) {
				fe::PlaneDirection direction;
				if (axis == 0)      direction = backFace ? fe::PlaneDirection::Left   : fe::PlaneDirection::Right;
				else if (axis == 1) direction = backFace ? fe::PlaneDirection::Bottom : fe::PlaneDirection::Top;
				else                direction = backFace ? fe::PlaneDirection::Front  : fe::PlaneDirection::Back;

				std::vector<BlockType> mask(uDim * vDim);

				for (int slice = 0; slice < axisDim; slice++) {
					std::fill(mask.begin(), mask.end(), BlockType::Air);

					for (int ui = 0; ui < uDim; ui++) {
						for (int vi = 0; vi < vDim; vi++) {
							glm::ivec3 pos(0);
							pos[axis] = slice;
							pos[u] = ui;
							pos[v] = vi;

							BlockType block = getBlockAt(pos);
							if (block == BlockType::Air) continue;

							if (cullBottomFaces && direction == fe::PlaneDirection::Bottom && pos.y == 0)
								continue;

							glm::ivec3 neighborPos = pos + (backFace ? -q : q);
							if (getBlockAt(neighborPos) != BlockType::Air) continue;

							mask[ui * vDim + vi] = block;
						}
					}

					for (int ui = 0; ui < uDim; ui++) {
						for (int vi = 0; vi < vDim; ) {
							BlockType type = mask[ui * vDim + vi];
							if (type == BlockType::Air) { vi++; continue; }

							int w = 1;
							while (vi + w < vDim && mask[ui * vDim + (vi + w)] == type) w++;

							int h = 1;
							bool done = false;
							while (ui + h < uDim && !done) {
								for (int k = 0; k < w; k++) {
									if (mask[(ui + h) * vDim + (vi + k)] != type) { done = true; break; }
								}
								if (!done) h++;
							}

							glm::vec3 origin(0.0f);
							origin[axis] = static_cast<float>(slice) + (backFace ? 0.0f : 1.0f);
							origin[u] = static_cast<float>(ui);
							origin[v] = static_cast<float>(vi);

							glm::vec3 du(0.0f), dv(0.0f);
							du[u] = static_cast<float>(h);
							dv[v] = static_cast<float>(w);

							glm::vec3 normal(0.0f);
							normal[axis] = backFace ? -1.0f : 1.0f;

							float layer = static_cast<float>(GetBlockTextureLayer(type, direction));

							unsigned int vo = static_cast<unsigned int>(allVertices.size());

							auto addV = [&](const glm::vec3& p) {
								allVertices.push_back(MakePackedVertex(p, normal, layer));
							};

							if (axis == 2) {
								if (!backFace) {
									addV(origin);
									addV(origin + du);
									addV(origin + du + dv);
									addV(origin + dv);
								} else {
									addV(origin);
									addV(origin + dv);
									addV(origin + du + dv);
									addV(origin + du);
								}
							} else {
								if (!backFace) {
									addV(origin);
									addV(origin + du);
									addV(origin + du + dv);
									addV(origin + dv);
								} else {
									addV(origin);
									addV(origin + dv);
									addV(origin + du + dv);
									addV(origin + du);
								}
							}

							allIndices.push_back(vo + 0);
							allIndices.push_back(vo + 1);
							allIndices.push_back(vo + 2);
							allIndices.push_back(vo + 0);
							allIndices.push_back(vo + 2);
							allIndices.push_back(vo + 3);

							for (int a = 0; a < h; a++)
								for (int b = 0; b < w; b++)
									mask[(ui + a) * vDim + (vi + b)] = BlockType::Air;

							vi += w;
						}
					}
				}
			}
		}

		chunk->mesh = fe::Mesh<FoxcraftPackedVertex>(std::move(allVertices), std::move(allIndices));
	}

	static int GetBlockTextureLayer(BlockType type, fe::PlaneDirection direction) {
		switch (type) {
			case BlockType::Grass:
				if (direction == fe::PlaneDirection::Top)
					return 1;
				if (direction == fe::PlaneDirection::Bottom)
					return 0;
				return 2;
			case BlockType::Dirt:
				return 0;
			case BlockType::Stone:
				return 4;
			case BlockType::Bedrock:
				return 3;
			case BlockType::Cobblestone:
				return 5;
			default:
				return 0;
		}
		return 0;
	}

	static const std::vector<std::string>& BlockTextures() {
		static const std::vector<std::string> blocks = {
			"resources/textures/dirt.png",
			"resources/textures/grass_carried.png",
			"resources/textures/grass_side_carried.png",
			"resources/textures/bedrock.png",
			"resources/textures/stone.png",
			"resources/textures/cobblestone.png",
			"resources/textures/cake_bottom.png",
			"resources/textures/cake_top.png"
		};
		return blocks;
	}
};