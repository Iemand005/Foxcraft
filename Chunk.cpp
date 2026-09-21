#include "Chunk.hpp"
#include "ChunkMesher.hpp"
#ifdef FC_INCLUDE_VULKAN
#include "ChunkBatcher.hpp"
#endif
#include "Mesh.hpp"
#include "physics/PhysicsFactory.hpp"

#include <algorithm>
#include <deque>

void Chunk::ComputeBlockLight() {
	std::fill(blockLight.begin(), blockLight.end(), 0);

	const int strideX = HEIGHT * DEPTH;
	const int strideY = DEPTH;
	const int strideZ = 1;

	auto index = [&](int x, int y, int z) { return x * strideX + y * strideY + z; };

	std::deque<int> queue;

	for (int x = 0; x < WIDTH; x++) {
		for (int y = 0; y < HEIGHT; y++) {
			for (int z = 0; z < DEPTH; z++) {
				int emission = GetLightEmission(GetBlock(x, y, z));
				if (emission > 0) {
					int i = index(x, y, z);
					blockLight[i] = static_cast<uint8_t>(emission);
					queue.push_back(i);
				}
			}
		}
	}

	const int dirs[6][3] = {
		{ 1, 0, 0}, {-1, 0, 0},
		{ 0, 1, 0}, { 0,-1, 0},
		{ 0, 0, 1}, { 0, 0,-1},
	};

	while (!queue.empty()) {
		int i = queue.front();
		queue.pop_front();

		uint8_t level = blockLight[i];
		if (level <= 1) continue;

		int x = i / strideX;
		int y = (i % strideX) / strideY;
		int z = i % strideY;

		uint8_t next = static_cast<uint8_t>(level - 1);
		for (int d = 0; d < 6; d++) {
			int nx = x + dirs[d][0];
			int ny = y + dirs[d][1];
			int nz = z + dirs[d][2];
			if (nx < 0 || nx >= WIDTH || ny < 0 || ny >= HEIGHT || nz < 0 || nz >= DEPTH)
				continue;
			if (!IsLightTransparent(GetBlock(nx, ny, nz)))
				continue;
			int ni = index(nx, ny, nz);
			if (blockLight[ni] < next) {
				blockLight[ni] = next;
				queue.push_back(ni);
			}
		}
	}
}

void Chunk::UploadToScene(fe::PhysicsFactory* PhysicsFactory, fe::Scene* scene, bool createPhysics, bool addToScene) {
    if (state == ChunkState::ScheduledForRemoval || state == ChunkState::RemovalPending) {
        std::cout << "Cancelled chunk upload (" << coord.x << ", " << coord.y << ") due to removal request." << std::endl;
        return;
    }

    bool isRemesh = sceneObject != nullptr;

    if (isRemesh) {
        std::cout << "Remeshing chunk (" << coord.x << ", " << coord.y << "): ";
        RemovePhysics();
        if (batcher_ && batcherSlot_ != UINT32_MAX) {
#ifdef FC_INCLUDE_VULKAN
            batcher_->RemoveChunk({batcherSlot_});
#endif
        }
        batcherSlot_ = UINT32_MAX;
    } else {
        std::cout << "Uploading chunk (" << coord.x << ", " << coord.y << "): ";
    }

    std::cout << "Vertices: " << mesh.vertices.size() << " Indices: " << mesh.indices.size() << std::endl;

    std::unique_ptr<fe::Mesh<FoxcraftVertex>> gpuMesh;

    if (batcher_) {
#ifdef FC_INCLUDE_VULKAN
        auto handle = batcher_->UploadChunk(mesh.vertices, mesh.indices, GetWorldPosition());
        batcherSlot_ = handle.index;
#endif
    } else if (!mesh.vertices.empty() && !mesh.indices.empty()) {
        gpuMesh = std::make_unique<fe::Mesh<FoxcraftVertex>>(
            std::vector<FoxcraftVertex>(mesh.vertices),
            std::vector<unsigned int>(mesh.indices));
        gpuMesh->loadTextureArray(ChunkMesher::BlockTextures(), fe::TextureScaling::Nearest);
    }

    if (isRemesh) {
        if (gpuMesh) {
            sceneObject->meshes.clear();
            sceneObject->meshes.push_back(std::move(gpuMesh));
        }
        if (createPhysics)
            AddPhysics(PhysicsFactory);
    } else {
        sceneObject = std::make_shared<fe::Object>();
        sceneObject->name = "Chunk";
        sceneObject->state.position = GetWorldPosition();
        sceneObject->isStatic = true;
        sceneObject->boundingCenterOffset = {WIDTH / 2.0f, HEIGHT / 2.0f, DEPTH / 2.0f};
        sceneObject->boundingRadius = glm::length(sceneObject->boundingCenterOffset);

        if (gpuMesh)
            sceneObject->meshes.push_back(std::move(gpuMesh));

        if (createPhysics)
            AddPhysics(PhysicsFactory);

        mesh.FreeCpuData();

        if (addToScene)
            scene->AddObject(sceneObject);
    }

    state = ChunkState::InScene;
}

void Chunk::AddPhysics(fe::PhysicsFactory* PhysicsFactory) {
	if (!sceneObject)
		return;
	if (sceneObject->physicsObject)
		return;

	// Refresh the cached collider whenever the CPU mesh is resident (first
	// upload or remesh). After the mesh is freed we fall back to the cache, so
	// physics objects can be recreated when the player re-enters range.
	if (!mesh.vertices.empty() && !mesh.indices.empty()) {
		colliderVertices_.clear();
		colliderVertices_.reserve(mesh.vertices.size());
		for (const auto& v : mesh.vertices)
			colliderVertices_.push_back(v.position);
		colliderIndices_.assign(mesh.indices.begin(), mesh.indices.end());
	}

	if (colliderVertices_.empty() || colliderIndices_.empty())
		return;

	auto physobj = PhysicsFactory->CreateObject(colliderVertices_, colliderIndices_);
	if (physobj)
		physobj->SetPosition(GetWorldPosition());
	sceneObject->SetPhysicsObject(std::move(physobj));
}

void Chunk::RemovePhysics() {
	if (!sceneObject || !sceneObject->physicsObject)
		return;
	sceneObject->physicsObject->Destroy();
	sceneObject->physicsObject.reset();
}
