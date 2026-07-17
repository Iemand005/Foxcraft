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

	void Update(int maxUploads, fe::PhysicsFactory* physicsEngine, fe::Scene* scene) {
		int uploaded = 0;
		while (uploaded < maxUploads) {
			std::shared_ptr<Chunk> chunk;
			{
				std::lock_guard<std::mutex> lock(completedMutex);
				if (completedQueue.empty()) break;
				chunk = completedQueue.front();
				completedQueue.pop();
			}
			UploadAndInsert(chunk, physicsEngine, scene);
			uploaded++;
		}

		std::lock_guard<std::mutex> lock(chunksMutex);

		for (auto& [coord, chunk] : chunks) {
			if (chunk->state == ChunkState::TerrainReady && AllNeighborsPresent(coord)) {
				chunk->state = ChunkState::MeshPending;
				{
					std::lock_guard<std::mutex> qlock(queueMutex);
					meshQueue.push_back(chunk);
				}
				queueCV.notify_one();
			}
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

	void LoadChunksInsideRange(glm::ivec2 center, int loadDistance) {
		for (int dz = -loadDistance; dz <= loadDistance; dz++) {
			for (int dx = -loadDistance; dx <= loadDistance; dx++) {
				int distance = std::max(std::abs(dx), std::abs(dz));
				if (distance > loadDistance) continue;

				RequestChunk(center + glm::ivec2{dx, dz});
			}
		}
	}

	void UnloadChunksOutsideRange(glm::ivec2 center, int loadDistance) {
		std::lock_guard<std::mutex> lock(chunksMutex);
		for (auto& [coord, chunk] : chunks) {
			int dx = coord.x - center.x;
			int dz = coord.y - center.y;
			int distance = std::max(std::abs(dx), std::abs(dz));

			if (distance > loadDistance && chunk->state != ChunkState::ScheduledForRemoval) {
				chunk->state = ChunkState::ScheduledForRemoval;
			}
		}
	}
	
	const int WIDTH = 16;
	const int DEPTH = 16;

	BlockType GetBlock(int worldX, int y, int worldZ) {
		glm::ivec2 coord = WorldToChunkCoord(worldX, worldZ);
		Chunk* chunk = GetChunk(coord);

		if (!chunk) {
			return BlockType::Air;
		}

		int localX = worldX - coord.x * WIDTH;
		int localZ = worldZ - coord.y * DEPTH;
		return chunk->GetBlock(localX, y, localZ);
	}

	glm::ivec2 WorldToChunkCoord(int worldX, int worldZ) {
		int chunkX = static_cast<int>(std::floor(static_cast<float>(worldX) / WIDTH));
		int chunkZ = static_cast<int>(std::floor(static_cast<float>(worldZ) / DEPTH));
		return { chunkX, chunkZ };
	}

	Chunk* GetChunk(glm::ivec2 coord) {
        auto it = chunks.find(coord);
        if (it == chunks.end())
            return nullptr;
        return it->second.get();
    }

private:
	void WorkerLoop();

	void UploadAndInsert(std::shared_ptr<Chunk> chunk, fe::PhysicsFactory* physicsEngine, fe::Scene* scene) {
		if (chunk->state == ChunkState::ScheduledForRemoval || chunk->state == ChunkState::RemovalPending)
			return;

		chunk->UploadToScene(physicsEngine, scene);
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
