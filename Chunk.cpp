#include "Chunk.hpp"
#include "ChunkMesher.hpp"
#include "ChunkBatcher.hpp"
#include "PackedVertex.hpp"

void Chunk::UploadToScene(fe::PhysicsFactory* physicsEngine, fe::Scene* scene, bool createPhysics, bool addToScene) {
	std::shared_ptr<fe::Mesh<fe::VertexArray>> convertedMesh;

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
			} else {
				std::vector<fe::VertexArray> vaVerts;
				vaVerts.reserve(mesh.vertices.size());
				for (const auto& v : mesh.vertices) {
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
				convertedMesh = std::make_shared<fe::Mesh<fe::VertexArray>>(std::move(vaVerts), std::vector<unsigned int>(mesh.indices.begin(), mesh.indices.end()));
				convertedMesh->loadTexture(ChunkMesher::BlockTextures()[0]);
			}

			if (convertedMesh) {
				sceneObject->meshes.clear();
				sceneObject->meshes.push_back(std::move(*convertedMesh));
				convertedMesh.reset();
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
		} else {
			std::vector<fe::VertexArray> vaVerts;
			vaVerts.reserve(mesh.vertices.size());
			for (const auto& v : mesh.vertices) {
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
			convertedMesh = std::make_shared<fe::Mesh<fe::VertexArray>>(std::move(vaVerts), std::vector<unsigned int>(mesh.indices.begin(), mesh.indices.end()));
			convertedMesh->loadTexture(ChunkMesher::BlockTextures()[0]);
		}

		if (createPhysics) {
			std::vector<glm::vec3> colliderVertices;
			colliderVertices.reserve(mesh.vertices.size());
			for (const auto& vertex : mesh.vertices)
				colliderVertices.push_back(glm::vec3(vertex.x, vertex.y, vertex.z));

			std::vector<uint32_t> colliderIndices(mesh.indices.begin(), mesh.indices.end());

			auto physobj = physicsEngine->CreateObject(colliderVertices, colliderIndices);
			if (physobj)
				physobj->SetPosition(GetWorldPosition());
			mesh.SetPhysicsObject(std::move(physobj));
		}

		mesh.FreeCpuData();
	}

	sceneObject = std::make_shared<fe::Object<fe::VertexArray>>();
	sceneObject->name = "Chunk";
	sceneObject->state.position = GetWorldPosition();
	sceneObject->isStatic = true;
	sceneObject->boundingCenterOffset = {WIDTH / 2.0f, HEIGHT / 2.0f, DEPTH / 2.0f};
	sceneObject->boundingRadius = glm::length(sceneObject->boundingCenterOffset);

	if (convertedMesh)
		sceneObject->meshes.push_back(std::move(*convertedMesh));

	if (mesh.physicsObject)
		sceneObject->SetPhysicsObject(std::move(mesh.physicsObject));

	if (addToScene)
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
		colliderVertices.push_back(glm::vec3(v.x, v.y, v.z));

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