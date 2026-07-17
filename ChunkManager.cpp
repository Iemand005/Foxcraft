#include "ChunkManager.hpp"
#include "ChunkMesher.hpp"

void ChunkManager::WorkerLoop() {
	while (running) {
		std::shared_ptr<Chunk> chunk;
		bool isMesh = false;
		{
			std::unique_lock<std::mutex> lock(queueMutex);
			queueCV.wait(lock, [this] {
				return !pendingQueue.empty() || !meshQueue.empty() || !running;
			});
			if (!running) return;

			if (!pendingQueue.empty()) {
				chunk = pendingQueue.front();
				pendingQueue.pop_front();
			} else {
				chunk = meshQueue.front();
				meshQueue.pop_front();
				isMesh = true;
			}
		}

		if (!isMesh) {
			ChunkState expected = ChunkState::TerrainPending;
			if (!chunk->state.compare_exchange_strong(expected, ChunkState::TerrainGenerating))
				continue;

			chunk->Generate();

			if (chunk->state != ChunkState::TerrainGenerating)
				continue;

			chunk->state = ChunkState::TerrainReady;
		} else {
			ChunkState expected = ChunkState::MeshPending;
			if (!chunk->state.compare_exchange_strong(expected, ChunkState::MeshGenerating))
				continue;

			ChunkMesher::BuildMesh(chunk, this);

			if (chunk->state != ChunkState::MeshGenerating)
				continue;

			chunk->state = ChunkState::MeshReady;

			std::lock_guard<std::mutex> lock(completedMutex);
			completedQueue.push(chunk);
		}
	}
}