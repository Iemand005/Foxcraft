#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_sdl3.h>
#include <imgui/backends/imgui_impl_opengl3.h>

#include <EditableGame.hpp>
#include <Primitives.hpp>

#include <audio/AudioVisualiser.hpp>
#include <ScreenSaverMode.hpp>

#include "Chunk.hpp"

class Foxcraft : public fe::EditableGame {
public:

	bool showDebugUI = false;

	std::vector<glm::vec3> path;
	int windowStart = 0;
	float pathIndex = 1.0f;
	std::vector<std::shared_ptr<fe::Object>> chunks;
	glm::vec3 lastUp = glm::vec3(0, 1, 0);
	glm::vec3 lastRight = glm::vec3(1, 0, 0);
	glm::vec3 prevEndForward{0};
	bool hasPrevEnd = false;

	static constexpr int POINTS_PER_CHUNK = 4;
	static constexpr int SHIFT = 3;
	static constexpr int MAX_CHUNKS = 32;
	static constexpr int TUNNEL_SEGMENTS = 64;
	static constexpr int SUBDIVISIONS_PER_SEG = 48;

	int NUM_CHUNKS = 4;

	float lightSpeed = 0.3f;

	float bgColorFreq = 0.3f;
	float visualizerScale = 8.0f;

	float audioAmplitudeScale = 10.0f;
	float audioSpeedMultiplier = 0.15f;
	float baseSpeedElapsedTimeBumpy = 0.0002f;
	float baseSpeedElapsedTime = 0.0002f;

	float cameraSpeed = 1.0f;
	float motionAmount = 1.2f;
	float tunnelRoundness = 0.0f;
	float animationSpeed = 1.0f;
	float farPlane = 500.0f;
	bool freeCamera = false;
	float freeCamSpeed = 15.0f;
	float segmentLength = 12.0f;

	Chunk chunk;

	Foxcraft(int width = 1000, int height = 1000, bool vr = false) : fe::EditableGame(width, height, vr, false) {

		SetClearColor(1, 1, 0);

		LoadShaders("resources/shaders/VertexShader.glsl", "resources/shaders/FragmentShader.glsl");

		LoadModels();
	}

	void LoadModels() {

		this->player = std::make_shared<fe::Character>();
		this->scene->AddObject(player);

		AddMonoBlock("resources/textures/dirt.png");
		AddMonoBlock("resources/textures/dirt.png", {1, 0, 0});


		chunk.Generate();

		fe::Mesh mesh = chunk.GenerateMesh();
		std::cout << "Vertices: " << mesh.vertices.size()
		<< " Indices: " << mesh.indices.size() << std::endl;
		// mesh.loadTexture("resources/textures/dirt.png", fe::TextureScaling::Nearest);

		std::vector<std::string> blocks = {
			"textures/dirt.png",
			"textures/grass_side.png",
			"textures/grass_top.png",
			"textures/stone.png",
			"textures/bedrock.png" 
		};

		mesh.loadTextureArray(blocks, fe::TextureScaling::Nearest);


		auto cubeObject = std::make_shared<fe::Object>(mesh);

		cubeObject->name = "Chunk";
		this->scene->AddObject(cubeObject);
	}

	void AddBlock(std::string topTexturePath, std::string sideTexturePath, std::string bottomTexturePath) {
		fe::UVRect cakeTopBtmUV;
		cakeTopBtmUV.u0 = 1.0f / 16.0f;
		cakeTopBtmUV.u1 = 15.0f / 16.0f;
		cakeTopBtmUV.v0 = 1.0f / 16.0f;
		cakeTopBtmUV.v1 = 15.0f / 16.0f;

		fe::UVRect cakeSideUV;
		cakeSideUV.u0 = 1.0f / 16.0f;
		cakeSideUV.u1 = 15.0f / 16.0f;
		cakeSideUV.v0 = 0.0f / 16.0f;
		cakeSideUV.v1 = 8.0f / 16.0f;

		fe::CubeUVs cakeUVs;


		cakeUVs.top = cakeUVs.bottom = cakeTopBtmUV;
		cakeUVs.front = cakeUVs.back = cakeUVs.left = cakeUVs.right = cakeSideUV;

		auto planeMesh = fe::Primitives::GenerateCube({fe::PlaneDirection::Top}, cakeUVs);
		planeMesh.loadTexture(topTexturePath, fe::TextureScaling::Nearest);

		auto sideMesh = fe::Primitives::GenerateCube({fe::PlaneDirection::Front, fe::PlaneDirection::Left, fe::PlaneDirection::Right, fe::PlaneDirection::Back}, cakeUVs);
		sideMesh.loadTexture(sideTexturePath, fe::TextureScaling::Nearest);

		auto bottomMesh = fe::Primitives::GenerateCube({fe::PlaneDirection::Bottom}, cakeUVs);
		bottomMesh.loadTexture(bottomTexturePath, fe::TextureScaling::Nearest);
		bottomMesh.hasTransparency = true;

		auto CAKEObject = std::make_shared<fe::Object>(planeMesh);
		CAKEObject->meshes.push_back(sideMesh);
		CAKEObject->meshes.push_back(bottomMesh);

		CAKEObject->name = "Cake";
		CAKEObject->state.position.y = 0.25f;
		CAKEObject->state.scale.x = CAKEObject->state.scale.z = 14.0f / 16.0f;
		CAKEObject->state.scale.y = 0.5f;
		this->scene->AddObject(CAKEObject);

	}

	void AddMonoBlock(std::string texturePath, glm::vec3 pos = {}) {
		auto cubeMesh = fe::Primitives::GenerateCube();
		cubeMesh.loadTexture(texturePath, fe::TextureScaling::Nearest);
		cubeMesh.hasTransparency = true;

		auto cubeObject = std::make_shared<fe::Object>(cubeMesh);

		cubeObject->name = "Cube";
		cubeObject->state.position = pos;
		this->scene->AddObject(cubeObject);
	}

	void ProcessInput() {
		SDL_Event event;
		fe::SDLWindow *window = (fe::SDLWindow*)this->window.get();
		while (window->PollSDLEvent(&event)) {
			ImGui_ImplSDL3_ProcessEvent(&event);
			auto io = ImGui::GetIO();
			switch (event.type) {
				case SDL_EVENT_QUIT:
					window->PrepareClose();
					break;
				case SDL_EVENT_MOUSE_BUTTON_DOWN:
					if (event.button.button == SDL_BUTTON_LEFT && !io.WantCaptureMouse) {
						window->StartMouseCapture();
					}
					break;
				case SDL_EVENT_WINDOW_RESIZED:
				case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
					Redraw();
					break;
				case SDL_EVENT_MOUSE_MOTION:
				{
					if (!window->capturingMouse) break;
					float sensitivity = 0.1f;
					camera->yaw   += event.motion.xrel * sensitivity;
					camera->pitch -= event.motion.yrel * sensitivity;
					camera->UpdateDirection();
					camera->pitch = std::clamp(camera->pitch, -89.0f, 89.0f);
					break;
				}
				case SDL_EVENT_KEY_DOWN:
					if (event.key.key == SDLK_F11) {
						window->ToggleFullscreen();
					}
					else if (event.key.key == SDLK_F3) {
						showDebugUI = !showDebugUI;
					}
					else if (event.key.key == SDLK_F2) {
						freeCamera = !freeCamera;
						window->StartMouseCapture();
					}
					break;
			}
		}

		if (!freeCamera) {
			if (window->IsKeyDown(SDL_SCANCODE_W)) this->player->Move(fe::Direction::Forwards, camera.get());
			if (window->IsKeyDown(SDL_SCANCODE_A)) this->player->Move(fe::Direction::Left, camera.get());
			if (window->IsKeyDown(SDL_SCANCODE_S)) this->player->Move(fe::Direction::Backwards, camera.get());
			if (window->IsKeyDown(SDL_SCANCODE_D)) this->player->Move(fe::Direction::Right, camera.get());

			if (window->IsKeyDown(SDL_SCANCODE_SPACE)) this->player->Move(fe::Direction::Up, camera.get());
			if (window->IsKeyDown(SDL_SCANCODE_LSHIFT)) this->player->Move(fe::Direction::Down, camera.get());
		}

		if (window->IsKeyDown(SDL_SCANCODE_ESCAPE)) window->StopMouseCapture();
		if (ImGui::GetIO().WantCaptureMouse) window->StopMouseCapture();
	}

	void Run() {
		auto window = this->GetWindow<fe::SDLWindow>();
		window->Show();
		window->DisableVSync();

		player->state.position.z = 5;
		player->state.position.y = 2;
		camera->farDist = farPlane;
		camera->SetAspect(camera->aspect);
		float elapsedTimeBumpy = 0.0f;
		float elapsedTime = 0.0f;

		while (!window->ShouldClose()) {

			ProcessInput();


			if (freeCamera) {
				float dt = fpsCounter.deltaTime;
				float spd = freeCamSpeed * dt;
				glm::vec3 cp = camera->GetPos();
				glm::vec3 right = glm::normalize(glm::cross(camera->front, camera->up));
				if (window->IsKeyDown(SDL_SCANCODE_W)) cp += camera->front * spd;
				if (window->IsKeyDown(SDL_SCANCODE_S)) cp -= camera->front * spd;
				if (window->IsKeyDown(SDL_SCANCODE_A)) cp -= right * spd;
				if (window->IsKeyDown(SDL_SCANCODE_D)) cp += right * spd;
				if (window->IsKeyDown(SDL_SCANCODE_SPACE)) cp += camera->up * spd;
				if (window->IsKeyDown(SDL_SCANCODE_LSHIFT)) cp -= camera->up * spd;
				camera->SetPos(cp);
			} else {
			}

			Update();
			Redraw();
		}
		Destroy();
	}

	void InitUI() override {}


	void DrawUI() override {
		if (!showDebugUI) return;
		BeginFrame();

		DrawDebugUI();

		EndFrame();
	}
};
