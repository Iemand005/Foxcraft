
#pragma once

#define WIN32_LEAN_AND_MEAN

#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <atomic>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/noise.hpp>

#include <Primitives.hpp>
#include <Mesh.hpp>
#include <Object.hpp>
#include <Scene.hpp>

class ChunkBatcher;

#include "PackedVertex.hpp"

enum class BlockType : uint8_t {
	Air = 0,
	Stone = 1,
	Dirt = 2,
	Grass = 3,
	Cobblestone = 4,
	Bedrock = 5,
};

enum class ChunkState {
    Unloaded,
    TerrainPending,
    TerrainGenerating,
    TerrainReady,
    MeshPending,
    MeshGenerating,
    MeshReady,
    InScene,
    ScheduledForRemoval,
    Dirty,
    RemovalPending
};

class Chunk {
	private:
	std::vector<BlockType> blocks;
	
	std::shared_ptr<fe::Object<fe::VertexArray>> sceneObject;

	std::string name;
	
public:
	ChunkBatcher* batcher_ = nullptr;
	uint32_t batcherSlot_ = UINT32_MAX;

	void SetBatcher(ChunkBatcher* b) { batcher_ = b; }
	static constexpr int WIDTH = 32, HEIGHT = 128, DEPTH = 32;
	glm::ivec2 coord;
	std::atomic<ChunkState> state;
	std::atomic<bool> paused{false};
	fe::Mesh<FoxcraftPackedVertex> mesh;

	Chunk() : blocks(WIDTH * HEIGHT * DEPTH, BlockType::Air) {}
	Chunk(glm::ivec2 position) : Chunk() {
		coord = position;
		name = "Chunk_" + std::to_string(coord.x) + "_" + std::to_string(coord.y);;
	}
	Chunk(int x, int y) : Chunk(glm::ivec2{x, y}) {}

	BlockType GetBlock(int x, int y, int z) const {
		if(x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH)
			return BlockType::Air;
		return blocks[x * HEIGHT * DEPTH + y * DEPTH + z];
	}

	void SetBlock(int x, int y, int z, BlockType type) {
		blocks[x * HEIGHT * DEPTH + y * DEPTH + z] = type;
	}

	void SetBlock(const glm::ivec3& pos, BlockType type) {
		blocks[pos.x * HEIGHT * DEPTH + pos.y * DEPTH + pos.z] = type;
	}
		
	BlockType GetBlock(const glm::ivec3& pos) const {
		return GetBlock(pos.x, pos.y, pos.z);
	}

	glm::vec2 GetPosition() {
		return coord;
	}

	glm::vec3 GetWorldPosition() {
		return {coord.x * WIDTH, 0, coord.y * DEPTH};
	}

	BlockType GenerateBlockAt(const glm::ivec3& position) {

	}

	void Save() {
		std::ofstream file(name, std::ios::binary);
		file.write(reinterpret_cast<const char*>(blocks.data()), blocks.size() * sizeof(BlockType));
	}

	bool Load() {
		std::ifstream file(name, std::ios::binary);
		if (!file) return false;
		file.read(reinterpret_cast<char*>(blocks.data()), blocks.size() * sizeof(BlockType));
		return true;
	}

	void Generate() {
		float heightAmplitude = 6.0f;
		float heightFrequency = 0.03f;
		float heightOffset = 24.0f;

		const int octaves = 4;
		const float persistence = 0.5f;
		const float lacunarity = 2.0f; 

		for (int x = 0; x < WIDTH; x++) {
			for (int z = 0; z < DEPTH; z++) {
				float worldX = coord.x * WIDTH + x;
				float worldZ = coord.y * DEPTH + z;

				float noiseSum = 0.0f;
				float amplitude = 1.0f;
				float frequency = heightFrequency;
				float maxAmplitude = 0.0f;

				for (int o = 0; o < octaves; o++) {
					noiseSum += glm::perlin(glm::vec2(worldX * frequency, worldZ * frequency)) * amplitude;
					maxAmplitude += amplitude;
					amplitude *= persistence;
					frequency *= lacunarity;
				}

				float normalizedNoise = noiseSum / maxAmplitude;
				float waveHeight = heightOffset + normalizedNoise * heightAmplitude;
				int maxHeight = glm::clamp(static_cast<int>(std::round(waveHeight)), 1, HEIGHT - 1);

				float bedrockNoise = glm::perlin(glm::vec2(worldX * 0.25f, worldZ * 0.25f));
				int bedrockThickness = glm::clamp(
					1 + static_cast<int>((bedrockNoise * 0.5f + 0.5f) * 3.0f), 1, 3);

				int stoneEnd = std::max(bedrockThickness, maxHeight - 4);

				for (int y = 0; y < HEIGHT; y++) {
					if (y < bedrockThickness) {
						SetBlock(x, y, z, BlockType::Bedrock);
					} else if (y < stoneEnd) {
						SetBlock(x, y, z, BlockType::Stone);
					} else if (y < maxHeight - 1) {
						SetBlock(x, y, z, BlockType::Dirt);
					} else if (y == maxHeight - 1) {
						SetBlock(x, y, z, BlockType::Grass);
					}
				}
			}
		}
	}

	static glm::ivec3 GetOffsetAt(const glm::ivec3& pos, fe::PlaneDirection direction) {
		switch (direction) {
			case fe::PlaneDirection::Front:
				return pos + glm::ivec3(0, 0, -1);
			case fe::PlaneDirection::Back:
				return pos + glm::ivec3(0, 0, 1);
			case fe::PlaneDirection::Right:
				return pos + glm::ivec3(-1, 0, 0);
			case fe::PlaneDirection::Left:
				return pos + glm::ivec3(1, 0, 0);
			case fe::PlaneDirection::Top:
				return pos + glm::ivec3(0, 1, 0);
			case fe::PlaneDirection::Bottom:
				return pos + glm::ivec3(0, -1, 0);
		}
		return pos;
	}

	bool NeedsFace(const glm::ivec3& pos, fe::PlaneDirection direction) {
		BlockType neighbor = GetBlock(GetOffsetAt(pos, direction));
		return neighbor == BlockType::Air;
	}

	void UploadToScene(fe::PhysicsFactory* physicsEngine, fe::Scene* scene, bool createPhysics = true, bool addToScene = true);
	void AddPhysics(fe::PhysicsFactory* physicsEngine);
	void RemovePhysics();

	std::shared_ptr<fe::Object<fe::VertexArray>> GetSceneObject() { return this->sceneObject; }
};
