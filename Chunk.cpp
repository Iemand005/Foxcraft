#include "Chunk.hpp"
#include "ChunkMesher.hpp"
#include "ChunkBatcher.hpp"

void Chunk::UploadToScene(fe::PhysicsFactory* physicsEngine, fe::Scene* scene, bool createPhysics, bool addToScene) {

	if (state == ChunkState::ScheduledForRemoval || state == ChunkState::RemovalPending) {
		std::cout << "Cancelled chunk upload (" << coord.x << ", " << coord.y << ") due to removal request." << std::endl;
		return;
	}

	if (sceneObject) {
		if (!mesh.vertices.empty() && !mesh.indices.empty()) {
			std::cout << "Remeshing chunk (" << coord.x << ", " << coord.y << "): " << "Vertices: " << mesh.vertices.size() << " Indices: " << mesh.indices.size() << std::endl;

			RemovePhysics();

			if (batcher_ && batcherSlot_ != UINT32_MAX) {
				batcher_->RemoveChunk({batcherSlot_});
			}
			batcherSlot_ = UINT32_MAX;

			if (batcher_) {
				auto handle = batcher_->UploadChunk(mesh.vertices, mesh.indices, GetWorldPosition());
				batcherSlot_ = handle.index;
			}

			if (createPhysics)
				AddPhysics(physicsEngine);
		}
		state = ChunkState::InScene;
		return;
	}

	if (!mesh.vertices.empty() && !mesh.indices.empty()) {
		std::cout << "Uploading chunk (" << coord.x << ", " << coord.y << "): " << "Vertices: " << mesh.vertices.size() << " Indices: " << mesh.indices.size() << std::endl;

		if (batcher_) {
			auto handle = batcher_->UploadChunk(mesh.vertices, mesh.indices, GetWorldPosition());
			batcherSlot_ = handle.index;
		}

		if (createPhysics) {
			std::vector<glm::vec3> colliderVertices;
			colliderVertices.reserve(mesh.vertices.size());
			for (const auto& vertex : mesh.vertices)
				colliderVertices.push_back(vertex.position);

			std::vector<uint32_t> colliderIndices(mesh.indices.begin(), mesh.indices.end());

			auto physobj = physicsEngine->CreateObject(colliderVertices, colliderIndices);
			if (physobj)
				physobj->SetPosition(GetWorldPosition());
			mesh.SetPhysicsObject(std::move(physobj));
		}
	}

	sceneObject = std::make_shared<fe::Object<fe::VertexArray>>();
	sceneObject->name = "Chunk";
	sceneObject->state.position = GetWorldPosition();
	sceneObject->isStatic = true;
	sceneObject->boundingCenterOffset = {WIDTH / 2.0f, HEIGHT / 2.0f, DEPTH / 2.0f};
	sceneObject->boundingRadius = glm::length(sceneObject->boundingCenterOffset);

	if (mesh.physicsObject)
		sceneObject->SetPhysicsObject(std::move(mesh.physicsObject));

	scene->AddObject(sceneObject);
	state = ChunkState::InScene;
}

void Chunk::AddPhysics(fe::PhysicsFactory* physicsEngine) {
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

	auto physobj = physicsEngine->CreateObject(colliderVertices, colliderIndices);
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