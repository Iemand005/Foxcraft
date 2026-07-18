#include "Chunk.hpp"
#include "ChunkMesher.hpp"

void Chunk::UploadToScene(fe::PhysicsFactory* physicsEngine, fe::Scene* scene, bool createPhysics) {

	if (state == ChunkState::ScheduledForRemoval || state == ChunkState::RemovalPending) {
		std::cout << "Cancelled chunk upload (" << coord.x << ", " << coord.y << ") due to removal request." << std::endl;
		return;
	}

	if (sceneObject) {
		// Remesh: update existing scene object
		if (!mesh.vertices.empty() && !mesh.indices.empty()) {
			std::cout << "Remeshing chunk (" << coord.x << ", " << coord.y << "): " << "Vertices: " << mesh.vertices.size() << " Indices: " << mesh.indices.size() << std::endl;

			RemovePhysics();

			sceneObject->meshes[0].RemoveFromGPU();
			sceneObject->meshes[0].vertices = std::move(mesh.vertices);
			sceneObject->meshes[0].indices = std::move(mesh.indices);
			sceneObject->meshes[0].init();

			if (createPhysics)
				AddPhysics(physicsEngine);
		}
		state = ChunkState::InScene;
		return;
	}

	if (createPhysics && !mesh.vertices.empty() && !mesh.indices.empty()) {
		std::cout << "Uploading chunk (" << coord.x << ", " << coord.y << "): " << "Vertices: " << mesh.vertices.size() << " Indices: " << mesh.indices.size() << std::endl;

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

	mesh.loadTextureArray(ChunkMesher::BlockTextures(), fe::TextureScaling::Nearest);

	sceneObject = std::make_shared<fe::Object<fe::VertexArray>>(std::move(mesh));
	sceneObject->name = "Chunk";
	sceneObject->state.position = GetWorldPosition();
	sceneObject->isStatic = true;
	sceneObject->boundingCenterOffset = {WIDTH / 2.0f, HEIGHT / 2.0f, DEPTH / 2.0f};
	sceneObject->boundingRadius = glm::length(sceneObject->boundingCenterOffset);

	scene->AddObject(sceneObject);
	state = ChunkState::InScene;
}

void Chunk::AddPhysics(fe::PhysicsFactory* physicsEngine) {
	if (!sceneObject || sceneObject->meshes.empty())
		return;
	auto& m = sceneObject->meshes[0];
	if (m.vertices.empty() || m.indices.empty())
		return;
	if (sceneObject->physicsObject)
		return;

	std::vector<glm::vec3> colliderVertices;
	colliderVertices.reserve(m.vertices.size());
	for (const auto& v : m.vertices)
		colliderVertices.push_back(v.position);

	std::vector<uint32_t> colliderIndices(m.indices.begin(), m.indices.end());

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