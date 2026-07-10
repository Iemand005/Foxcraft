#include "Chunk.hpp"
#include "ChunkMesher.hpp"

void Chunk::UploadToScene(fe::PhysicsFactory* physicsEngine, fe::Scene* scene) {

	if (state == ChunkState::ScheduledForRemoval || state == ChunkState::RemovalPending) {
		std::cout << "Cancelled chunk upload (" << coord.x << ", " << coord.y << ") due to removal request." << std::endl;

		return; // got cancelled after the caller's check but before we ran
	}
	std::cout << "Uploading chunk (" << coord.x << ", " << coord.y << "): " << "Vertices: " << mesh.vertices.size() << " Indices: " << mesh.indices.size() << std::endl;
	
	// GPU coies
	mesh.CopyToGPU();
	mesh.loadTextureArray(ChunkMesher::BlockTextures(), fe::TextureScaling::Nearest);

	std::vector<glm::vec3> colliderVertices;
	colliderVertices.reserve(mesh.vertices.size());
	for (const auto& vertex : mesh.vertices)
		colliderVertices.push_back(vertex.position);

	std::vector<uint32_t> colliderIndices(mesh.indices.begin(), mesh.indices.end());
	
	
	sceneObject = std::make_shared<fe::Object>(std::move(mesh));
	sceneObject->name = "Chunk";
	sceneObject->state.position = GetWorldPosition();
	
	auto physobj = physicsEngine->CreateObject(colliderVertices, colliderIndices);
	if (physobj) {
		physobj->SetPosition(sceneObject->state.position);
	}
	mesh.SetPhysicsObject(std::move(physobj));
	
	mesh.FreeCpuData();

	scene->AddObject(sceneObject);
	state = ChunkState::InScene;
}