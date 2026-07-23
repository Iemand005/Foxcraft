#include "Chunk.hpp"
#include "ChunkMesher.hpp"
#include "ChunkBatcher.hpp"
#include "PackedVertex.hpp"
#include "Mesh.hpp"
#include "physics/PhysicsFactory.hpp"

static std::unique_ptr<fe::Mesh<fe::VertexArray>> ConvertFoxcraftPackedMesh(
    const std::vector<FoxcraftPackedVertex>& vertices,
    const std::vector<uint32_t>& indices)
{
    std::vector<fe::VertexArray> vaVerts;
    vaVerts.reserve(vertices.size());
    for (const auto& v : vertices) {
        glm::vec3 pos(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
        uint8_t face = v.normalLayer & 0x7;
        uint8_t layer = v.normalLayer >> 3;
        glm::vec3 normal;
        switch (face) {
            case 0: normal = glm::vec3(1, 0, 0); break;
            case 1: normal = glm::vec3(-1, 0, 0); break;
            case 2: normal = glm::vec3(0, 1, 0); break;
            case 3: normal = glm::vec3(0, -1, 0); break;
            case 4: normal = glm::vec3(0, 0, 1); break;
            case 5: normal = glm::vec3(0, 0, -1); break;
            default: normal = glm::vec3(0, 1, 0);
        }
        glm::vec2 uv;
        if (face < 2) uv = glm::vec2(static_cast<float>(v.z), static_cast<float>(v.y));
        else if (face < 4) uv = glm::vec2(static_cast<float>(v.x), static_cast<float>(v.z));
        else uv = glm::vec2(static_cast<float>(v.x), static_cast<float>(v.y));
        vaVerts.emplace_back(pos.x, pos.y, pos.z, normal.x, normal.y, normal.z, uv.x, uv.y, static_cast<float>(layer));
    }
    auto out = std::make_unique<fe::Mesh<fe::VertexArray>>(
        std::move(vaVerts),
        std::vector<unsigned int>(indices.begin(), indices.end()));
    out->loadTextureArray(ChunkMesher::BlockTextures(), fe::TextureScaling::Nearest);
    return out;
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
            batcher_->RemoveChunk({batcherSlot_});
        }
        batcherSlot_ = UINT32_MAX;
    } else {
        std::cout << "Uploading chunk (" << coord.x << ", " << coord.y << "): ";
    }

    std::cout << "Vertices: " << mesh.vertices.size() << " Indices: " << mesh.indices.size() << std::endl;

    std::unique_ptr<fe::Mesh<fe::VertexArray>> convertedMesh;

    if (batcher_) {
        auto handle = batcher_->UploadChunk(mesh.vertices, mesh.indices, GetWorldPosition());
        batcherSlot_ = handle.index;
    } else if (!mesh.vertices.empty() && !mesh.indices.empty()) {
        convertedMesh = ConvertFoxcraftPackedMesh(mesh.vertices, mesh.indices);
    }

    if (isRemesh) {
        if (convertedMesh) {
            sceneObject->meshes.clear();
            sceneObject->meshes.push_back(std::move(convertedMesh));
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

        if (convertedMesh)
            sceneObject->meshes.push_back(std::move(convertedMesh));

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
		colliderVertices.push_back(glm::vec3(v.x, v.y, v.z));

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
