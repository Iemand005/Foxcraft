#pragma once

#include <MeshArray.hpp>
#include "Chunk.hpp"
#include "ChunkManager.hpp"

class ChunkMesher {
	static constexpr int WIDTH = 16, HEIGHT = 128, DEPTH = 16;
public:
    static MeshArray BuildMesh(Chunk& chunk, ChunkManager& manager) {
		std::vector<fe::VertexArray> allVertices;
		std::vector<unsigned int> allIndices;

		allVertices.reserve(WIDTH * HEIGHT * DEPTH * 4);
		allIndices.reserve(WIDTH * HEIGHT * DEPTH * 6);

		for (int x = 0; x < WIDTH; x++) {
			for (int y = 0; y < HEIGHT; y++) {
				for (int z = 0; z < DEPTH; z++) {
					BlockType block = chunk.GetBlock(x, y, z);
					if (block == BlockType::Air) continue;

					std::vector<fe::PlaneDirection> visibleFaces;
					visibleFaces.reserve(6);

					for (auto direction : {fe::PlaneDirection::Front, fe::PlaneDirection::Back,
						fe::PlaneDirection::Left, fe::PlaneDirection::Right,
						fe::PlaneDirection::Top, fe::PlaneDirection::Bottom}) {
						if (chunk.NeedsFace(glm::vec3(x, y, z), direction)) {
							visibleFaces.push_back(direction);
						}
					}

					if (!visibleFaces.empty()) {
						fe::Mesh cubeMesh = fe::Primitives::GenerateCube(visibleFaces, 1.0f);
						glm::vec3 offset = glm::vec3(x + 0.5f, y + 0.5f, z + 0.5f);
						unsigned int vertexOffset = static_cast<unsigned int>(allVertices.size());

						for (auto& v : cubeMesh.vertices) {
							fe::VertexArray vv;
							vv.normal = v.normal;
							vv.position = v.position + offset;

							fe::PlaneDirection direction = fe::PlaneDirection::Front;
							if (v.normal.y > 0.5f) direction = fe::PlaneDirection::Top;
							else if (v.normal.y < -0.5f) direction = fe::PlaneDirection::Bottom;

							float layer = GetBlockTextureLayer(block, direction);
							vv.texCoord = glm::vec3(v.uv.x, v.uv.y, layer);
							allVertices.push_back(vv);
						}

						for (auto idx : cubeMesh.indices) {
							allIndices.push_back(static_cast<unsigned int>(idx) + vertexOffset);
						}
					}
				}
			}
		}

		chunk.mesh = fe::MeshArray(std::move(allVertices), std::move(allIndices), false);
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