#include "Chunk.hpp"
#include "ChunkMesher.hpp"

void Chunk::UploadToScene(fe::PhysicsFactory* physicsEngine, fe::Scene* scene) {

	if (state == ChunkState::ScheduledForRemoval || state == ChunkState::RemovalPending) {
		std::cout << "Cancelled chunk upload (" << coord.x << ", " << coord.y << ") due to removal request." << std::endl;

		return; // got cancelled after the caller's check but before we ran
	}
	std::cout << "Uploading chunk (" << coord.x << ", " << coord.y << "): " << "Vertices: " << mesh.vertices.size() << " Indices: " << mesh.indices.size() << std::endl;

	std::vector<glm::vec3> colliderVertices;
	colliderVertices.reserve(mesh.vertices.size());
	for (const auto& vertex : mesh.vertices)
		colliderVertices.push_back(vertex.position);

	std::vector<uint32_t> colliderIndices(mesh.indices.begin(), mesh.indices.end());

	auto physobj = physicsEngine->CreateObject(colliderVertices, colliderIndices);
	if (physobj) {
		physobj->SetPosition(GetWorldPosition());
	}
	mesh.SetPhysicsObject(std::move(physobj));

	mesh.loadTextureArray(ChunkMesher::BlockTextures(), fe::TextureScaling::Nearest);
	mesh.CopyToGPU();
	mesh.FreeCpuData();

	sceneObject = std::make_shared<fe::Object<fe::VertexArray>>(std::move(mesh));
	sceneObject->name = "Chunk";
	sceneObject->state.position = GetWorldPosition();

	scene->AddObject(sceneObject);
	state = ChunkState::InScene;
}