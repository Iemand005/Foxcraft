#pragma once

#include <algorithm>
#include <deque>

#include <Mesh.hpp>
#include "Chunk.hpp"
#include "ChunkManager.hpp"

class ChunkMesher {
public:
	static void BuildMesh(std::shared_ptr<Chunk> chunk, ChunkManager *manager, bool cullBottomFaces = true, bool smoothLighting = false) {
		std::vector<FoxcraftVertex> allVertices;
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

		// --- Block-light for this mesh -------------------------------------
		// Start from this chunk's stored light map, pull light that would flow
		// in across the four side faces from neighbouring chunks, then flood
		// fill. We only ever write to the local "meshLight" scratch copy, so
		// neighbouring worker threads never see in-progress writes.
		const int strideX = Chunk::HEIGHT * Chunk::DEPTH;
		const int strideY = Chunk::DEPTH;

		auto lightIndex = [&](int x, int y, int z) { return x * strideX + y * strideY + z; };
		auto lightInBounds = [&](int x, int y, int z) {
			return x >= 0 && x < Chunk::WIDTH && y >= 0 && y < Chunk::HEIGHT && z >= 0 && z < Chunk::DEPTH;
		};

		auto getBlockLightAt = [&](const glm::ivec3& pos) -> uint8_t {
			if (lightInBounds(pos.x, pos.y, pos.z))
				return chunk->GetBlockLight(pos.x, pos.y, pos.z);
			if (pos.y < 0 || pos.y >= Chunk::HEIGHT)
				return 0;
			int worldX = chunk->coord.x * Chunk::WIDTH + pos.x;
			int worldZ = chunk->coord.y * Chunk::DEPTH + pos.z;
			auto neighborCoord = manager->WorldToChunkCoord(worldX, worldZ);
			auto neighbor = manager->GetChunk(neighborCoord);
			if (!neighbor) return 0;
			int localX = worldX - neighborCoord.x * Chunk::WIDTH;
			int localZ = worldZ - neighborCoord.y * Chunk::DEPTH;
			return neighbor->GetBlockLight(localX, pos.y, localZ);
		};

		std::vector<uint8_t> meshLight = chunk->GetLightMap();
		std::deque<int> lightQueue;

		auto seedBorder = [&](const glm::ivec3& outside, const glm::ivec3& inside) {
			uint8_t flow = getBlockLightAt(outside);
			if (flow <= 1) return;
			uint8_t value = static_cast<uint8_t>(flow - 1);
			int ni = lightIndex(inside.x, inside.y, inside.z);
			if (meshLight[ni] < value) {
				meshLight[ni] = value;
				lightQueue.push_back(ni);
			}
		};

		for (int y = 0; y < Chunk::HEIGHT; y++) {
			for (int z = 0; z < Chunk::DEPTH; z++) {
				seedBorder({ -1, y, z }, { 0, y, z });
				seedBorder({ Chunk::WIDTH, y, z }, { Chunk::WIDTH - 1, y, z });
			}
			for (int x = 0; x < Chunk::WIDTH; x++) {
				seedBorder({ x, y, -1 }, { x, y, 0 });
				seedBorder({ x, y, Chunk::DEPTH }, { x, y, Chunk::DEPTH - 1 });
			}
		}

		const int dirs[6][3] = {
			{ 1, 0, 0}, {-1, 0, 0},
			{ 0, 1, 0}, { 0,-1, 0},
			{ 0, 0, 1}, { 0, 0,-1},
		};

		while (!lightQueue.empty()) {
			int i = lightQueue.front();
			lightQueue.pop_front();

			uint8_t level = meshLight[i];
			if (level <= 1) continue;

			int x = i / strideX;
			int y = (i % strideX) / strideY;
			int z = i % strideY;

			uint8_t next = static_cast<uint8_t>(level - 1);
			for (int d = 0; d < 6; d++) {
				int nx = x + dirs[d][0];
				int ny = y + dirs[d][1];
				int nz = z + dirs[d][2];
				if (!lightInBounds(nx, ny, nz)) continue;
				if (!Chunk::IsLightTransparent(getBlockAt({ nx, ny, nz }))) continue;
				int ni = lightIndex(nx, ny, nz);
				if (meshLight[ni] < next) {
					meshLight[ni] = next;
					lightQueue.push_back(ni);
				}
			}
		}

		// Refined light value for a cell: scratch inside the chunk, neighbour's
		// stored (already finalised) map outside the chunk.
		auto getMeshLightAt = [&](const glm::ivec3& pos) -> uint8_t {
			if (lightInBounds(pos.x, pos.y, pos.z))
				return meshLight[lightIndex(pos.x, pos.y, pos.z)];
			return getBlockLightAt(pos);
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

			auto cornerLight = [&](const glm::ivec3& P, int outwardSign) -> float {
				// The cell immediately in front of the face's plane corner (it
				// is always air, since the face is only emitted against an air
				// block). Sampling just this corner-aligned cell keeps the
				// result symmetric — no poking into cells beyond the face's own
				// corners — so shared edges between faces always agree.
				glm::ivec3 n(0);
				n[axis] = outwardSign;

				glm::ivec3 base = (outwardSign == -1) ? P + n : P;
				return static_cast<float>(getMeshLightAt(base)) / static_cast<float>(Chunk::MAX_LIGHT);
			};

			for (bool backFace : {false, true}) {
				fe::PlaneDirection direction;
				if (axis == 0)      direction = backFace ? fe::PlaneDirection::Left   : fe::PlaneDirection::Right;
				else if (axis == 1) direction = backFace ? fe::PlaneDirection::Bottom : fe::PlaneDirection::Top;
				else                direction = backFace ? fe::PlaneDirection::Front  : fe::PlaneDirection::Back;

				int outwardSign = backFace ? -1 : 1;

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

							glm::ivec3 o(0), duI(0), dvI(0);
							o[axis] = slice + (backFace ? 0 : 1);
							o[u] = ui;
							o[v] = vi;
							duI[u] = h;
							dvI[v] = w;

							glm::ivec3 corners[4] = { o, o + duI, o + duI + dvI, o + dvI };
							float quadLight = 0.0f;
							for (int c = 0; c < 4; c++)
								quadLight += cornerLight(corners[c], outwardSign);
							quadLight /= 4.0f;

							unsigned int vo = static_cast<unsigned int>(allVertices.size());

							auto addV = [&](const glm::vec3& p) {
								glm::vec2 uv;
								if (axis == 0) uv = glm::vec2(p.z, p.y);
								else if (axis == 1) uv = glm::vec2(p.x, p.z);
								else uv = glm::vec2(p.x, p.y);

								float light = smoothLighting
									? cornerLight(glm::ivec3(glm::round(p)), outwardSign)
									: quadLight;

								allVertices.emplace_back(p.x, p.y, p.z, normal.x, normal.y, normal.z, uv.x, uv.y, layer, light);
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

		chunk->mesh = fe::Mesh<FoxcraftVertex>(std::move(allVertices), std::move(allIndices));
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
			case BlockType::Glowstone:
				return 6;
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
				"resources/textures/glowstone.png",
				"resources/textures/cake_bottom.png",
				"resources/textures/cake_top.png"
			};
			return blocks;
		}
};