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

    // Called from main thread when a chunk enters range
    void RequestChunk(glm::ivec2 coord) {
        std::lock_guard<std::mutex> lock(chunksMutex);
        if (chunks.count(coord)) return; // already tracked

        auto chunk = std::make_shared<Chunk>();
        chunk->coord = coord;
        chunk->state = ChunkState::Queued;
        chunks[coord] = chunk;

        {
            std::lock_guard<std::mutex> qlock(queueMutex);
            pendingQueue.push_back(chunk);
        }
        queueCV.notify_one();
    }

    // Called from main thread when a chunk leaves range
    void UnloadChunk(glm::ivec2 coord) {
        std::lock_guard<std::mutex> lock(chunksMutex);
        auto it = chunks.find(coord);
        if (it != chunks.end())
            it->second->state = ChunkState::ScheduledForRemoval;
    }

    // Called once per frame from main thread. maxUploads caps GPU work per frame
    // so a burst of finished chunks doesn't spike frame time.
    void Update(int maxUploads = 2) {
        // 1. Insert newly generated chunks into the scene
        int uploaded = 0;
        while (uploaded < maxUploads) {
            std::shared_ptr<Chunk> chunk;
            {
                std::lock_guard<std::mutex> lock(completedMutex);
                if (completedQueue.empty()) break;
                chunk = completedQueue.front();
                completedQueue.pop();
            }
            UploadAndInsert(chunk);
            uploaded++;
        }

        // 2. Handle removals (cheap, so no cap needed, but you could add one)
        std::lock_guard<std::mutex> lock(chunksMutex);
        for (auto it = chunks.begin(); it != chunks.end(); ) {
            if (it->second->state == ChunkState::ScheduledForRemoval) {
                RemoveFromScene(it->second);
                it = chunks.erase(it);
            } else {
                ++it;
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

			chunk->GenerateMesh();

            // GenerateChunk(*chunk);   // your existing Generate() voxel logic
            // BuildMeshData(*chunk);   // CPU-side vertices/indices only, no GL calls here

            chunk->state = ChunkState::ReadyToUpload;
            {
                std::lock_guard<std::mutex> lock(completedMutex);
                completedQueue.push(chunk);
            }
        }
    }

    void UploadAndInsert(std::shared_ptr<Chunk> chunk) {
        // Safe here: this runs on main thread, GL context is current
        // glGenVertexArrays(1, &chunk->vao);
        // glGenBuffers(1, &chunk->vbo);
        // glGenBuffers(1, &chunk->ebo);

        // glBindVertexArray(chunk->vao);
        // glBindBuffer(GL_ARRAY_BUFFER, chunk->vbo);
        // glBufferData(GL_ARRAY_BUFFER, chunk->vertexData.size() * sizeof(Vertex),
        //              chunk->vertexData.data(), GL_STATIC_DRAW);

        // glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk->ebo);
        // glBufferData(GL_ELEMENT_ARRAY_BUFFER, chunk->indexData.size() * sizeof(uint32_t),
        //              chunk->indexData.data(), GL_STATIC_DRAW);
        // // ... vertex attrib pointers ...

        // chunk->sceneNode = scene->AddChunkNode(chunk);
        // chunk->state = ChunkState::InScene;

        // // Free CPU-side copies now that they're on the GPU
        // chunk->vertexData.clear();
        // chunk->vertexData.shrink_to_fit();
        // chunk->indexData.clear();
        // chunk->indexData.shrink_to_fit();
    }

    void RemoveFromScene(std::shared_ptr<Chunk> chunk) {
        chunk->state = ChunkState::Unloading;
		// scene->RemoveObject(chunk->)
        // if (chunk->sceneNode) scene->RemoveNode(chunk->sceneNode);
        // if (chunk->vao) glDeleteVertexArrays(1, &chunk->vao);
        // if (chunk->vbo) glDeleteBuffers(1, &chunk->vbo);
        // if (chunk->ebo) glDeleteBuffers(1, &chunk->ebo);
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