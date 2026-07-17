#pragma once

#include <Mesh.hpp>
#include "Chunk.hpp"
#include "ChunkManager.hpp"

class ChunkMesher {
	static constexpr int WIDTH = 16, HEIGHT = 128, DEPTH = 16;
public:
	static void BuildMesh(std::shared_ptr<Chunk> chunk, ChunkManager *manager, bool cullBottomFaces = true) {
		std::vector<fe::VertexArray> allVertices;
		std::vector<unsigned int> allIndices;
		allVertices.reserve(4096);
		allIndices.reserve(6144);

		auto getBlockAt = [&](const glm::ivec3& pos) -> BlockType {
			if (pos.x >= 0 && pos.x < WIDTH && pos.y >= 0 && pos.y < HEIGHT && pos.z >= 0 && pos.z < DEPTH)
				return chunk->GetBlock(pos.x, pos.y, pos.z);
			if (pos.y < 0 || pos.y >= HEIGHT)
				return BlockType::Air;
			int worldX = chunk->coord.x * WIDTH + pos.x;
			int worldZ = chunk->coord.y * DEPTH + pos.z;
			auto neighborCoord = manager->WorldToChunkCoord(worldX, worldZ);
			Chunk* neighbor = manager->GetChunk(neighborCoord);
			if (!neighbor) return BlockType::Air;
			int localX = worldX - neighborCoord.x * WIDTH;
			int localZ = worldZ - neighborCoord.y * DEPTH;
			return neighbor->GetBlock(localX, pos.y, localZ);
		};

		const int dims[3] = {WIDTH, HEIGHT, DEPTH};

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

							float layer = GetBlockTextureLayer(type, direction);
							float fh = static_cast<float>(h);
							float fw = static_cast<float>(w);

							fe::VertexArray v0, v1, v2, v3;
							v0.position = origin;             v0.normal = normal; v0.texCoord = {0.0f, 0.0f, layer};
							v1.position = origin + du;        v1.normal = normal; v1.texCoord = {fh,   0.0f, layer};
							v2.position = origin + du + dv;   v2.normal = normal; v2.texCoord = {fh,   fw,   layer};
							v3.position = origin + dv;        v3.normal = normal; v3.texCoord = {0.0f, fw,   layer};

							unsigned int vo = static_cast<unsigned int>(allVertices.size());

							if (!backFace) {
								allVertices.push_back(v0); allVertices.push_back(v1);
								allVertices.push_back(v2); allVertices.push_back(v3);
							} else {
								allVertices.push_back(v0); allVertices.push_back(v3);
								allVertices.push_back(v2); allVertices.push_back(v1);
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

		chunk->mesh = fe::Mesh<fe::VertexArray>(std::move(allVertices), std::move(allIndices));
	}

	static int GetBlockTextureLayer(BlockType type, fe::PlaneDirection direction) {
		if (type == BlockType::Grass) {
			if (direction == fe::PlaneDirection::Top) {
				return 1;
			} else if (direction == fe::PlaneDirection::Bottom) {
				return 0;
			} else {
				return 2;
			}
		} else if (type == BlockType::Dirt) {
			return 0;
		} else if (type == BlockType::Stone) {
			return 4;
		} else if (type == BlockType::Bedrock) {
			return 3;
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
			"resources/textures/cake_bottom.png",
			"resources/textures/cake_top.png"
		};
		return blocks;
	}
};