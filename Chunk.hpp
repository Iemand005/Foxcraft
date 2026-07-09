
#pragma once

#include <memory>
#include <iostream>

#include <Primitives.hpp>
#include <MeshArray.hpp>

enum class BlockType : short {
	Air = 0,
	Stone = 1,
	Dirt = 2,
	Grass = 3,
	Cobblestone = 4,
};

class Chunk {
private:
	std::vector<BlockType> blocks;
	static constexpr int WIDTH = 16, HEIGHT = 128, DEPTH = 16;
	glm::vec2 position;

public:
	Chunk() : blocks(WIDTH * HEIGHT * DEPTH, BlockType::Air) {}
	Chunk(int x, int y) : Chunk() {
		position = {x, y};
	}

	BlockType GetBlock(int x, int y, int z) const {
		if(x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH)
			return BlockType::Air;
		return blocks[x * HEIGHT * DEPTH + y * DEPTH + z];
	}

	BlockType GetBlock(glm::vec3 pos) const {
		return GetBlock(static_cast<int>(pos.x), static_cast<int>(pos.y), static_cast<int>(pos.z));
	}

	glm::vec2 GetPosition() {
		return position;
	}

	glm::vec3 GetWorldPosition() {
		return {position.x * WIDTH, 0, position.y * DEPTH};
	}

	void SetBlock(int x, int y, int z, BlockType type) {
		blocks[x * HEIGHT * DEPTH + y * DEPTH + z] = type;
	}

	void Generate() {
		for(int x = 0; x < WIDTH; x++) {
			for(int y = 0; y < HEIGHT; y++) {
				for(int z = 0; z < DEPTH; z++) {
					if (y < 30) {
						SetBlock(x, y, z, BlockType::Dirt);
					} else if (y < 31)
					{
						SetBlock(x, y, z, BlockType::Grass);
					}
					
				}
			}
		}
	}

	glm::vec3 GetOffsetAt(glm::vec3 pos, fe::PlaneDirection direction) {
		switch (direction) {
			case fe::PlaneDirection::Front:
				return pos + glm::vec3(0, 0, -1);
			case fe::PlaneDirection::Back:
				return pos + glm::vec3(0, 0, 1);
			case fe::PlaneDirection::Right:
				return pos + glm::vec3(-1, 0, 0);
			case fe::PlaneDirection::Left:
				return pos + glm::vec3(1, 0, 0);
			case fe::PlaneDirection::Top:
				return pos + glm::vec3(0, 1, 0);
			case fe::PlaneDirection::Bottom:
				return pos + glm::vec3(0, -1, 0);
		}
		return pos;
	}

	bool NeedsFace(glm::vec3 pos, fe::PlaneDirection direction) {
		glm::vec3 offsetPos = GetOffsetAt(pos, direction);
		BlockType neighbor = GetBlock(offsetPos);
		return neighbor == BlockType::Air;
	}

	int GetBlockLayer(BlockType type, fe::PlaneDirection direction) {
		if (type == BlockType::Grass) {
			if (direction == fe::PlaneDirection::Top) {
				return 1;
			} else if (direction == fe::PlaneDirection::Bottom) {
				return 0;
			} else {
				return 2;
			}
		}
		else if (type == BlockType::Dirt) {
			return 0;
		}
		return 0;
	}

	fe::MeshArray GenerateMesh() {
		std::vector<fe::VertexArray> allVertices;
		std::vector<unsigned int> allIndices;

		int blockCount = 0;
		int faceCount = 0;

		for(int x = 0; x < WIDTH; x++) {
			for(int y = 0; y < HEIGHT; y++) {
				for(int z = 0; z < DEPTH; z++) {
					BlockType block = GetBlock(x, y, z);
					if(block == BlockType::Air) continue;

					blockCount++;

					std::vector<fe::PlaneDirection> visibleFaces;
					for(auto direction : {fe::PlaneDirection::Front, fe::PlaneDirection::Back,
						fe::PlaneDirection::Left, fe::PlaneDirection::Right,
						fe::PlaneDirection::Top, fe::PlaneDirection::Bottom}) {
						if(NeedsFace(glm::vec3(x, y, z), direction)) {
							visibleFaces.push_back(direction);
							faceCount++;
						}
					}

					if(!visibleFaces.empty()) {
						fe::Mesh cubeMesh = fe::Primitives::GenerateCube(visibleFaces, 1.0f);

						// place cube centered at integer block position (so it occupies [x,x+1] etc)
						glm::vec3 offset = glm::vec3(x + 0.5f, y + 0.5f, z + 0.5f);

						unsigned int vertexOffset = allVertices.size();

						for(auto &v : cubeMesh.vertices) {

							fe::VertexArray vv;
							vv.normal  = v.normal;
							vv.position = v.position + offset;
							// vv.position += offset;
							fe::PlaneDirection direction = fe::PlaneDirection::Front;
							if (v.normal.y > 0.5f) {
								direction = fe::PlaneDirection::Top;
							} else if (v.normal.y < -0.5f) {
								direction = fe::PlaneDirection::Bottom;
							}
							
							float layer = GetBlockLayer(block, direction);
							vv.texCoord = glm::vec3(v.uv.x, v.uv.y, layer);
							allVertices.push_back(vv);
						}

						for(auto idx : cubeMesh.indices) {
							allIndices.push_back(static_cast<unsigned int>(idx) + vertexOffset);
						}
					}
				}
			}
		}

		std::cout << "Total blocks: " << blockCount << ", faces: " << faceCount << std::endl;

		return fe::MeshArray(allVertices, allIndices);
	}
};
