#pragma once

#include <vector>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <thread>
#include <atomic>


#include <glm/glm.hpp>

#include "Chunk.hpp"

#include <Scene.hpp>


struct ChunkCoordHash {
	size_t operator()(const glm::ivec2& c) const {
		return std::hash<int64_t>()((int64_t(c.x) << 32) ^ uint32_t(c.y));
	}
};

class ChunkManager {
public:
	
	ChunkManager(int numWorkers = 2) : running(true) {
		for (int i = 0; i < numWorkers; i++)
			workers.emplace_back(&ChunkManager::WorkerLoop, this);
	}

	~ChunkManager() {
		running = false;
		queueCV.notify_all();
		for (auto& t : workers) t.join();
	}

	void RequestChunk(glm::ivec2 coord) {
		std::lock_guard<std::mutex> lock(chunksMutex);
		if (chunks.count(coord)) return;

		auto chunk = std::make_shared<Chunk>();
		chunk->coord = coord;
		chunk->state = ChunkState::TerrainPending;
		chunks[coord] = chunk;

		{
			std::lock_guard<std::mutex> qlock(queueMutex);
			pendingQueue.push_back(chunk);
		}
		queueCV.notify_one();
	}

	void UnloadChunk(glm::ivec2 coord) {
		std::lock_guard<std::mutex> lock(chunksMutex);
		auto it = chunks.find(coord);
		if (it != chunks.end())
			it->second->state = ChunkState::ScheduledForRemoval;
	}

	void Update(int maxUploads, fe::PhysicsFactory* physicsEngine, fe::Scene* scene,
	            glm::ivec2 center, int meshDist, int physicsDist = -1) {
		int uploaded = 0;
		while (uploaded < maxUploads) {
			std::shared_ptr<Chunk> chunk;
			{
				std::lock_guard<std::mutex> lock(completedMutex);
				if (completedQueue.empty()) break;
				chunk = completedQueue.front();
				completedQueue.pop();
			}
			UploadAndInsert(chunk, physicsEngine, scene, center, physicsDist);
			uploaded++;
		}

		int meshDistSq = meshDist * meshDist;
		int physicsDistSq = physicsDist >= 0 ? physicsDist * physicsDist : -1;

		{
			std::lock_guard<std::mutex> lock(chunksMutex);

			for (auto& [coord, chunk] : chunks) {
				if (chunk->state != ChunkState::TerrainReady || !AllNeighborsPresent(coord))
					continue;

				int dx = coord.x - center.x;
				int dz = coord.y - center.y;
				if (dx * dx + dz * dz > meshDistSq)
					continue;

				chunk->state = ChunkState::MeshPending;
				{
					std::lock_guard<std::mutex> qlock(queueMutex);
					meshQueue.push_back(chunk);
				}
				queueCV.notify_one();
			}

			for (auto& [coord, chunk] : chunks) {
				if (chunk->state != ChunkState::InScene)
					continue;

				if (physicsDistSq < 0) {
					if (!chunk->GetSceneObject()->physicsObject)
						chunk->AddPhysics(physicsEngine);
					continue;
				}

				int dx = coord.x - center.x;
				int dz = coord.y - center.y;
				bool inRange = dx * dx + dz * dz <= physicsDistSq;

				if (inRange && !chunk->GetSceneObject()->physicsObject)
					chunk->AddPhysics(physicsEngine);
				else if (!inRange && chunk->GetSceneObject()->physicsObject)
					chunk->RemovePhysics();
			}

			for (auto it = chunks.begin(); it != chunks.end(); ) {
				if (it->second->state == ChunkState::ScheduledForRemoval) {
					auto sco = it->second->GetSceneObject();
					if (!sco || RemoveFromScene(it->second, physicsEngine, scene))
						it = chunks.erase(it);
					else
						++it;
				} else {
					++it;
				}
			}
		}
	}

	void UpdatePausedState(glm::ivec2 center, glm::vec2 forward) {
		{
			std::lock_guard<std::mutex> lock(chunksMutex);
			for (auto& [coord, chunk] : chunks) {
				glm::vec2 offset = glm::vec2(coord.x - center.x, coord.y - center.y);
				chunk->paused = glm::dot(offset, forward) < 0.0f;
			}
		}
		queueCV.notify_all();
	}

	void LoadChunksInsideRange(glm::ivec2 center, int loadDistance, glm::vec2 forward = glm::vec2(0.0f)) {
		int distSq = loadDistance * loadDistance;
		bool useDirection = glm::length(forward) > 0.001f;
		for (int d = 0; d <= loadDistance; d++) {
			for (int dz = -d; dz <= d; dz++) {
				for (int dx = -d; dx <= d; dx++) {
					if (std::max(std::abs(dx), std::abs(dz)) != d) continue;
					if (dx * dx + dz * dz > distSq) continue;
					if (useDirection) {
						glm::vec2 offset = glm::vec2(dx, dz);
						if (glm::dot(offset, forward) < 0.0f)
							continue;
					}
					RequestChunk(center + glm::ivec2{dx, dz});
				}
			}
		}
	}

	void UnloadChunksOutsideRange(glm::ivec2 center, int loadDistance) {
		int distSq = loadDistance * loadDistance;
		std::lock_guard<std::mutex> lock(chunksMutex);
		for (auto& [coord, chunk] : chunks) {
			int dx = coord.x - center.x;
			int dz = coord.y - center.y;

			if (dx * dx + dz * dz > distSq && chunk->state != ChunkState::ScheduledForRemoval) {
				chunk->state = ChunkState::ScheduledForRemoval;
			}
		}
	}


	BlockType GetBlock(glm::ivec3 position) {
		glm::ivec2 coord = WorldToChunkCoord(position.x, position.z);
		Chunk* chunk = GetChunk(coord);

		if (!chunk) {
			return BlockType::Air;
		}

		int localX = position.x - coord.x * Chunk::WIDTH;
		int localZ = position.z - coord.y * Chunk::DEPTH;
		return chunk->GetBlock(localX, position.y, localZ);
	}

	BlockType GetBlock(int worldX, int y, int worldZ) {
		return GetBlock({worldX, y, worldZ});
	}

	bool IsBlockSolid(glm::ivec3 position) {
		return GetBlock(position.x, position.y, position.z) != BlockType::Air;
	}

	glm::ivec2 WorldToChunkCoord(int worldX, int worldZ) {
		int chunkX = static_cast<int>(std::floor(static_cast<float>(worldX) / Chunk::WIDTH));
		int chunkZ = static_cast<int>(std::floor(static_cast<float>(worldZ) / Chunk::DEPTH));
		return { chunkX, chunkZ };
	}

	Chunk* GetChunk(glm::ivec2 coord) {
        auto it = chunks.find(coord);
        if (it == chunks.end())
            return nullptr;
        return it->second.get();
    }

	int GetSurfaceHeight(int worldX, int worldZ) {
		glm::ivec2 coord = WorldToChunkCoord(worldX, worldZ);
		Chunk* chunk = GetChunk(coord);
		if (!chunk) return -1;
		int localX = worldX - coord.x * Chunk::WIDTH;
		int localZ = worldZ - coord.y * Chunk::DEPTH;
		for (int y = Chunk::HEIGHT - 1; y >= 0; y--)
			if (chunk->GetBlock(localX, y, localZ) != BlockType::Air)
				return y + 1;
		return -1;
	}

private:
	void WorkerLoop();

	void UploadAndInsert(std::shared_ptr<Chunk> chunk, fe::PhysicsFactory* physicsEngine, fe::Scene* scene,
	                     glm::ivec2 center, int physicsDist) {
		if (chunk->state == ChunkState::ScheduledForRemoval || chunk->state == ChunkState::RemovalPending)
			return;

		bool createPhysics = true;
		if (physicsDist >= 0) {
			int dx = chunk->coord.x - center.x;
			int dz = chunk->coord.y - center.y;
			if (dx * dx + dz * dz > physicsDist * physicsDist)
				createPhysics = false;
		}

		chunk->UploadToScene(physicsEngine, scene, createPhysics);
	}

	bool RemoveFromScene(std::shared_ptr<Chunk> chunk, fe::PhysicsFactory* physicsEngine, fe::Scene* scene) {
		chunk->state = ChunkState::RemovalPending;
		auto sco = chunk->GetSceneObject();
		bool removed = scene->RemoveObject(sco);
		if (sco) {
			if (sco->physicsObject)
				sco->physicsObject->Destroy();
			for (auto& m : sco->meshes) {
				m.RemoveFromGPU();
			}
		}

		if (!removed) {
            std::cerr << "WARNING: chunk scene object was non-null but not found in scene->objects! Uh not Scheduling for removal again..." << std::endl;
        }	
		return removed;
	}

	bool AllNeighborsPresent(glm::ivec2 coord) {
		const glm::ivec2 offsets[4] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
		for (auto& o : offsets) {
			auto it = chunks.find(coord + o);
			if (it == chunks.end()) return false;
			ChunkState s = it->second->state;
			if (s < ChunkState::TerrainReady) return false;
		}
		return true;
	}

	std::unordered_map<glm::ivec2, std::shared_ptr<Chunk>, ChunkCoordHash> chunks;
	std::mutex chunksMutex;

	std::deque<std::shared_ptr<Chunk>> pendingQueue;
	std::deque<std::shared_ptr<Chunk>> meshQueue;
	std::mutex queueMutex;
	std::condition_variable queueCV;

	std::queue<std::shared_ptr<Chunk>> completedQueue;
	std::mutex completedMutex;

	std::vector<std::thread> workers;
	std::atomic<bool> running;
	fe::Scene* scene;
};
