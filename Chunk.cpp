#include "Chunk.hpp"
#include "ChunkMesher.hpp"
#ifdef FC_INCLUDE_VULKAN
#include "ChunkBatcher.hpp"
#endif
#include "Mesh.hpp"
#include "physics/PhysicsFactory.hpp"

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

    std::unique_ptr<fe::Mesh<fe::VertexArray>> gpuMesh;

    if (batcher_) {
#ifdef FC_INCLUDE_VULKAN
        auto handle = batcher_->UploadChunk(mesh.vertices, mesh.indices, GetWorldPosition());
        batcherSlot_ = handle.index;
#endif
    } else if (!mesh.vertices.empty() && !mesh.indices.empty()) {
        gpuMesh = std::make_unique<fe::Mesh<fe::VertexArray>>(
            std::vector<fe::VertexArray>(mesh.vertices),
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
	if (mesh.vertices.empty() || mesh.indices.empty())
		return;
	if (sceneObject->physicsObject)
		return;

	std::vector<glm::vec3> colliderVertices;
	colliderVertices.reserve(mesh.vertices.size());
	for (const auto& v : mesh.vertices)
		colliderVertices.push_back(v.position);

	std::vector<uint32_t> colliderIndices(mesh.indices.begin(), mesh.indices.end());

	auto physobj = PhysicsFactory->CreateObject(colliderVertices, colliderIndices);
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
