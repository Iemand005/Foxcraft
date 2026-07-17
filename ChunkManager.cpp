#include "ChunkManager.hpp"
#include "ChunkMesher.hpp"

void ChunkManager::WorkerLoop() {
	while (running) {
		std::shared_ptr<Chunk> chunk;
		bool isMesh = false;
		{
			std::unique_lock<std::mutex> lock(queueMutex);
			queueCV.wait(lock, [this] {
				auto hasNonPaused = [](const std::deque<std::shared_ptr<Chunk>>& q) {
					for (auto& c : q)
						if (!c->paused) return true;
					return false;
				};
				return hasNonPaused(pendingQueue) || hasNonPaused(meshQueue) || !running;
			});
			if (!running) return;

			auto popNonPaused = [](std::deque<std::shared_ptr<Chunk>>& q) -> std::shared_ptr<Chunk> {
				for (auto it = q.begin(); it != q.end(); ++it) {
					if (!(*it)->paused) {
						auto c = *it;
						q.erase(it);
						return c;
					}
				}
				return nullptr;
			};

			chunk = popNonPaused(pendingQueue);
			if (chunk) {
				isMesh = false;
			} else {
				chunk = popNonPaused(meshQueue);
				if (chunk) isMesh = true;
			}
		}

		if (!chunk) continue;

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