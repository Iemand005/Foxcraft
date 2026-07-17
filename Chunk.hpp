
#pragma once

#define WIN32_LEAN_AND_MEAN

#include <cmath>
#include <memory>
#include <atomic>

#include <glm/glm.hpp>
#include <glm/gtc/noise.hpp>

#include <Primitives.hpp>
#include <Mesh.hpp>
#include <Object.hpp>
#include <Scene.hpp>

enum class BlockType : short {
	Air = 0,
	Stone = 1,
	Dirt = 2,
	Grass = 3,
	Cobblestone = 4,
	Bedrock = 5,
};

enum class ChunkState {
    Unloaded,				// not tracked yet
    TerrainPending,			// sitting in the work queue, not started
    TerrainGenerating,		// worker thread is building voxel/mesh data right now
    TerrainReady,			// worker finished, waiting for main thread to add to scene
    InScene,				// fully uploaded to GPU and active
    ScheduledForRemoval,	// main thread marked it, needs cleanup
	Dirty,
    RemovalPending			// being removed (freeing GPU buffers etc.)
};

class Chunk {
private:
	std::vector<BlockType> blocks;
	static constexpr int WIDTH = 16, HEIGHT = 128, DEPTH = 16;

	std::shared_ptr<fe::Object<fe::VertexArray>> sceneObject;
	
public:
	glm::ivec2 coord;
	std::atomic<ChunkState> state;
	fe::Mesh<fe::VertexArray> mesh;

	Chunk() : blocks(WIDTH * HEIGHT * DEPTH, BlockType::Air) {}
	Chunk(int x, int y) : Chunk() {
		coord = {x, y};
	}

	BlockType GetBlock(int x, int y, int z) const {
		if(x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH)
			return BlockType::Air;
		return blocks[x * HEIGHT * DEPTH + y * DEPTH + z];
	}

	void SetBlock(int x, int y, int z, BlockType type) {
		blocks[x * HEIGHT * DEPTH + y * DEPTH + z] = type;
	}
		
	BlockType GetBlock(glm::vec3 pos) const {
		return GetBlock(static_cast<int>(pos.x), static_cast<int>(pos.y), static_cast<int>(pos.z));
	}

	glm::vec2 GetPosition() {
		return coord;
	}

	glm::vec3 GetWorldPosition() {
		return {coord.x * WIDTH, 0, coord.y * DEPTH};
	}

	BlockType GenerateBlockAt(glm::ivec3 position) {

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

	static glm::vec3 GetOffsetAt(glm::vec3 pos, fe::PlaneDirection direction) {
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

	void UploadToScene(fe::PhysicsFactory* physicsEngine, fe::Scene* scene);

	std::shared_ptr<fe::Object<fe::VertexArray>> GetSceneObject() { return this->sceneObject; }
};
