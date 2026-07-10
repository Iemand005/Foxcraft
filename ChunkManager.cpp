#include "ChunkManager.hpp"
#include "ChunkMesher.hpp"

void ChunkManager::WorkerLoop() {
	while (running) {
		std::shared_ptr<Chunk> chunk;
		{
			std::unique_lock<std::mutex> lock(queueMutex);
			queueCV.wait(lock, [this] { return !pendingQueue.empty() || !running; });
			if (!running) return;
			chunk = pendingQueue.front();
			pendingQueue.pop_front();
		}

		ChunkState expected = ChunkState::TerrainPending;
		if (!chunk->state.compare_exchange_strong(expected, ChunkState::TerrainGenerating)) {
			continue; // was ScheduledForRemoval (or something else) — drop it
		}

		chunk->Generate();

		if (chunk->state != ChunkState::TerrainGenerating)
			continue;


		ChunkMesher::BuildMesh(chunk, this);

		expected = ChunkState::TerrainGenerating;
		if (chunk->state.compare_exchange_strong(expected, ChunkState::TerrainReady)) {
			std::lock_guard<std::mutex> lock(completedMutex);
			completedQueue.push(chunk);
		}
	}
}