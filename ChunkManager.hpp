#pragma once

#include <vector>
#include <mutex>
#include <queue>

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

	 

	// Called from main thread when a chunk leaves range
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
		for (auto it = chunks.begin(); it != chunks.end(); ) {
			if (it->second->state == ChunkState::ScheduledForRemoval) {
				RemoveFromScene(it->second, physicsEngine, scene);
				it = chunks.erase(it);
			} else {
				++it;
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

private:
	void WorkerLoop() {
		while (running) {
			std::shared_ptr<Chunk> chunk;
			{
				std::unique_lock<std::mutex> lock(queueMutex);
				queueCV.wait(lock, [this] { return !pendingQueue.empty() || !running; });
				if (!running) return;
				chunk = pendingQueue.front();
				pendingQueue.pop_front();
			}

			chunk->state = ChunkState::Generating;

			chunk->Generate();

			chunk->BuildMesh();

			ChunkState expected = ChunkState::Generating;
			if (chunk->state.compare_exchange_strong(expected, ChunkState::ReadyToUpload)) {
				std::lock_guard<std::mutex> lock(completedMutex);
				completedQueue.push(chunk);
			}
		}
	}

	void UploadAndInsert(std::shared_ptr<Chunk> chunk, fe::PhysicsFactory* physicsEngine, fe::Scene* scene) {
		if (chunk->state == ChunkState::ScheduledForRemoval || chunk->state == ChunkState::Unloading) {
			return; // was removed while we were generating it — abandon, do not upload
		}
		chunk->UploadToScene(physicsEngine, scene);
	}

	void RemoveFromScene(std::shared_ptr<Chunk> chunk, fe::PhysicsFactory* physicsEngine, fe::Scene* scene) {
		chunk->state = ChunkState::Unloading;
		auto sco = chunk->GetSceneObject();
		bool removed = scene->RemoveObject(sco);
		if (sco && !sco->meshArrays.empty()) {
			if (sco->meshArrays[0].physicsObject)
				sco->meshArrays[0].physicsObject->Destroy();
			sco->meshArrays[0].RemoveFromGPU();
		}

		if (!removed) {
            std::cerr << "WARNING: chunk scene object was non-null but not found in scene->objects!\n";
        }
		
	}

	std::unordered_map<glm::ivec2, std::shared_ptr<Chunk>, ChunkCoordHash> chunks;
	std::mutex chunksMutex;

	std::deque<std::shared_ptr<Chunk>> pendingQueue;
	std::mutex queueMutex;
	std::condition_variable queueCV;

	std::queue<std::shared_ptr<Chunk>> completedQueue;
	std::mutex completedMutex;

	std::vector<std::thread> workers;
	std::atomic<bool> running;
	fe::Scene* scene;
};