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

		allVertices.reserve(WIDTH * HEIGHT * DEPTH * 4);
		allIndices.reserve(WIDTH * HEIGHT * DEPTH * 6);

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

		auto emitQuad = [&](const glm::vec3& v0, const glm::vec3& v1,
		                    const glm::vec3& v2, const glm::vec3& v3,
		                    const glm::vec3& normal, float layer,
		                    float u0, float v0_, float u1, float v1_) {
			unsigned int base = static_cast<unsigned int>(allVertices.size());
			auto addVert = [&](const glm::vec3& pos, float u, float v) {
				fe::VertexArray vv;
				vv.position = pos;
				vv.normal = normal;
				vv.texCoord = glm::vec3(u, v, layer);
				allVertices.push_back(vv);
			};
			addVert(v0, u0, v0_);
			addVert(v1, u1, v0_);
			addVert(v2, u1, v1_);
			addVert(v3, u0, v1_);
			allIndices.push_back(base);
			allIndices.push_back(base + 1);
			allIndices.push_back(base + 2);
			allIndices.push_back(base);
			allIndices.push_back(base + 2);
			allIndices.push_back(base + 3);
		};

		auto doGreedy = [&](fe::PlaneDirection dir,
		                     int sliceAxis, int sliceCount,
		                     int planeA, int planeB, int sizeA, int sizeB,
		                     bool positive) {
			std::vector<bool> mask(sizeA * sizeB);
			std::vector<BlockType> types(sizeA * sizeB);
			int coord[3] = {};

			for (int s = 0; s < sliceCount; s++) {
				coord[sliceAxis] = s;

				for (int a = 0; a < sizeA; a++) {
					coord[planeA] = a;
					for (int b = 0; b < sizeB; b++) {
						coord[planeB] = b;
						BlockType block = chunk->GetBlock(coord[0], coord[1], coord[2]);
						int idx = a * sizeB + b;

						if (block == BlockType::Air) {
							mask[idx] = false;
							continue;
						}

						int neighbor[3] = {coord[0], coord[1], coord[2]};
						neighbor[sliceAxis] = coord[sliceAxis] + (positive ? 1 : -1);
						BlockType neighborBlock = getBlockAt({neighbor[0], neighbor[1], neighbor[2]});

						if (dir == fe::PlaneDirection::Bottom && coord[1] == 0 && cullBottomFaces)
							mask[idx] = false;
						else
							mask[idx] = neighborBlock == BlockType::Air;
						types[idx] = block;
					}
				}

				for (int a = 0; a < sizeA; a++) {
					for (int b = 0; b < sizeB; b++) {
						int idx = a * sizeB + b;
						if (!mask[idx]) continue;

						BlockType type = types[idx];

						int w = 1;
						while (a + w < sizeA && mask[(a + w) * sizeB + b] && types[(a + w) * sizeB + b] == type)
							w++;

						int h = 1;
						while (b + h < sizeB) {
							bool allMatch = true;
							for (int dw = 0; dw < w; dw++) {
								if (!mask[(a + dw) * sizeB + (b + h)] || types[(a + dw) * sizeB + (b + h)] != type) {
									allMatch = false;
									break;
								}
							}
							if (!allMatch) break;
							h++;
						}

						float layer = (float)GetBlockTextureLayer(type, dir);

						float facePos = (float)(s + (positive ? 1 : 0));

						glm::vec3 normal(0.0f);
						normal[sliceAxis] = positive ? 1.0f : -1.0f;

						float p0c[3] = {}, p1c[3] = {}, p2c[3] = {}, p3c[3] = {};
						p0c[sliceAxis] = p1c[sliceAxis] = p2c[sliceAxis] = p3c[sliceAxis] = facePos;
						p0c[planeA] = (float)a;
						p0c[planeB] = (float)b;
						p1c[planeA] = (float)(a + w);
						p1c[planeB] = (float)b;
						p2c[planeA] = (float)(a + w);
						p2c[planeB] = (float)(b + h);
						p3c[planeA] = (float)a;
						p3c[planeB] = (float)(b + h);

						emitQuad(
							glm::vec3(p0c[0], p0c[1], p0c[2]),
							glm::vec3(p1c[0], p1c[1], p1c[2]),
							glm::vec3(p2c[0], p2c[1], p2c[2]),
							glm::vec3(p3c[0], p3c[1], p3c[2]),
							normal, layer, 0.0f, 0.0f, (float)w, (float)h
						);

						for (int dy = 0; dy < h; dy++)
							for (int dx = 0; dx < w; dx++)
								mask[(a + dx) * sizeB + (b + dy)] = false;
					}
				}
			}
		};

		doGreedy(fe::PlaneDirection::Top,    1, HEIGHT, 0, 2, WIDTH, DEPTH,  true);
		doGreedy(fe::PlaneDirection::Bottom, 1, HEIGHT, 0, 2, WIDTH, DEPTH, false);
		doGreedy(fe::PlaneDirection::Front,  2, DEPTH,  0, 1, WIDTH, HEIGHT,  true);
		doGreedy(fe::PlaneDirection::Back,   2, DEPTH,  0, 1, WIDTH, HEIGHT, false);
		doGreedy(fe::PlaneDirection::Right,  0, WIDTH,  1, 2, HEIGHT, DEPTH,  true);
		doGreedy(fe::PlaneDirection::Left,   0, WIDTH,  1, 2, HEIGHT, DEPTH, false);

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