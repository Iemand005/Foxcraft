#include "Chunk.hpp"
#include "ChunkMesher.hpp"

void Chunk::UploadToScene(fe::PhysicsFactory* physicsEngine, fe::Scene* scene) {

	if (state == ChunkState::ScheduledForRemoval || state == ChunkState::RemovalPending) {
		std::cout << "Cancelled chunk upload (" << coord.x << ", " << coord.y << ") due to removal request." << std::endl;
		return;
	}

	// HARDCODED TEST: Build mesh on the main thread, no worker involved.
	// Place a few obvious cubes at known positions within the chunk.
	std::vector<fe::VertexArray> verts;
	std::vector<unsigned int> indices;

	auto addCube = [&](float ox, float oy, float oz, float layer) {
		float s = 0.5f;
		float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
		unsigned int base = static_cast<unsigned int>(verts.size());

		// Front face (-Z)
		verts.push_back({ox-s,oy-s,oz-s,  0, 0,-1,  u0,v0,layer});
		verts.push_back({ox-s,oy+s,oz-s,  0, 0,-1,  u0,v1,layer});
		verts.push_back({ox+s,oy+s,oz-s,  0, 0,-1,  u1,v1,layer});
		verts.push_back({ox+s,oy-s,oz-s,  0, 0,-1,  u1,v0,layer});
		// Back face (+Z)
		verts.push_back({ox-s,oy-s,oz+s,  0, 0, 1,  u0,v0,layer});
		verts.push_back({ox+s,oy-s,oz+s,  0, 0, 1,  u1,v0,layer});
		verts.push_back({ox+s,oy+s,oz+s,  0, 0, 1,  u1,v1,layer});
		verts.push_back({ox-s,oy+s,oz+s,  0, 0, 1,  u0,v1,layer});
		// Top face (+Y)
		verts.push_back({ox-s,oy+s,oz-s,  0, 1, 0,  u0,v0,layer});
		verts.push_back({ox-s,oy+s,oz+s,  0, 1, 0,  u0,v1,layer});
		verts.push_back({ox+s,oy+s,oz+s,  0, 1, 0,  u1,v1,layer});
		verts.push_back({ox+s,oy+s,oz-s,  0, 1, 0,  u1,v0,layer});
		// Bottom face (-Y)
		verts.push_back({ox-s,oy-s,oz-s,  0,-1, 0,  u0,v0,layer});
		verts.push_back({ox+s,oy-s,oz-s,  0,-1, 0,  u1,v0,layer});
		verts.push_back({ox+s,oy-s,oz+s,  0,-1, 0,  u1,v1,layer});
		verts.push_back({ox-s,oy-s,oz+s,  0,-1, 0,  u0,v1,layer});
		// Right face (+X)
		verts.push_back({ox+s,oy-s,oz-s,  1, 0, 0,  u0,v0,layer});
		verts.push_back({ox+s,oy+s,oz-s,  1, 0, 0,  u0,v1,layer});
		verts.push_back({ox+s,oy+s,oz+s,  1, 0, 0,  u1,v1,layer});
		verts.push_back({ox+s,oy-s,oz+s,  1, 0, 0,  u1,v0,layer});
		// Left face (-X)
		verts.push_back({ox-s,oy-s,oz-s, -1, 0, 0,  u0,v0,layer});
		verts.push_back({ox-s,oy-s,oz+s, -1, 0, 0,  u1,v0,layer});
		verts.push_back({ox-s,oy+s,oz+s, -1, 0, 0,  u1,v1,layer});
		verts.push_back({ox-s,oy+s,oz-s, -1, 0, 0,  u0,v1,layer});

		for (unsigned int face = 0; face < 6; ++face) {
			unsigned int fb = base + face * 4;
			indices.push_back(fb + 0);
			indices.push_back(fb + 1);
			indices.push_back(fb + 2);
			indices.push_back(fb + 0);
			indices.push_back(fb + 2);
			indices.push_back(fb + 3);
		}
	};

	// One cube at local (8, 26, 8) with texture layer 0 (dirt)
	addCube(8.0f, 26.0f, 8.0f, 0.0f);
	// One cube at local (8, 30, 8) with texture layer 1 (grass top)
	addCube(8.0f, 30.0f, 8.0f, 1.0f);

	std::cerr << "[Chunk] HARDCODED MESH: " << verts.size() << " verts, " << indices.size()
			  << " indices at world pos (" << GetWorldPosition().x << "," << GetWorldPosition().y << "," << GetWorldPosition().z << ")" << std::endl;

	mesh = fe::Mesh<fe::VertexArray>(std::move(verts), std::move(indices));
	mesh.loadTextureArray(ChunkMesher::BlockTextures(), fe::TextureScaling::Nearest);

	sceneObject = std::make_shared<fe::Object<fe::VertexArray>>(std::move(mesh));
	sceneObject->name = "Chunk";
	sceneObject->state.position = GetWorldPosition();

	scene->AddObject(sceneObject);
	state = ChunkState::InScene;
}