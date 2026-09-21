#pragma once
#include "XRGame.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <algorithm>
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

#ifndef EXCLUDE_JOLT
#include <physics/BasicDebugRenderer.hpp>
#endif

#include "ChunkManager.hpp"
#ifdef FC_INCLUDE_VULKAN
#include "ChunkMesher.hpp"
#endif

class Foxcraft : public fe::EditableGame
{
public:
	bool showDebugUI = false;

	enum class DebugViewMode { Off, Physics, Full };
	DebugViewMode debugViewMode = DebugViewMode::Off;
	bool gamepadStartWasDown_ = false;
	// Edge detection for gamepad one-shot actions (block break/place, hotbar
	// switching, debug view toggle). Triggered on the rising edge.
	bool gamepadLtWasDown_ = false;
	bool gamepadRtWasDown_ = false;
	bool gamepadDpadWasDown_ = false;
	bool gamepadLeftShoulderWasDown_ = false;
	bool gamepadRightShoulderWasDown_ = false;

	// Cycles through debug views: off -> physics debug -> physics debug + UI.
	void CycleDebugView()
	{
		switch (debugViewMode)
		{
			case DebugViewMode::Off:
				debugViewMode = DebugViewMode::Physics;
#ifndef EXCLUDE_JOLT
				BasicDebugRenderer::DebugRenderingEnabled() = true;
#endif
				break;
			case DebugViewMode::Physics:
				debugViewMode = DebugViewMode::Full;
				showDebugUI = true;
				break;
			case DebugViewMode::Full:
			default:
				debugViewMode = DebugViewMode::Off;
#ifndef EXCLUDE_JOLT
				BasicDebugRenderer::DebugRenderingEnabled() = false;
#endif
				showDebugUI = false;
				break;
		}
	}

	bool useRectangularPlayerHitbox = true;

	float walkSpeed = 5.0f;
	float sprintSpeed = 9.0f;

	// Hotbar of placeable block types, cycled with LT/RT (gamepad), the XR grip
	// buttons (L1/R1) and the G key. Defaults to Cobblestone (index 0).
	std::vector<BlockType> hotbarBlocks_ = {
		BlockType::Cobblestone,
		BlockType::Glowstone,
		BlockType::Stone,
		BlockType::Dirt,
		BlockType::Grass,
		BlockType::Bedrock,
	};
	size_t hotbarIndex_ = 0;
	BlockType placedBlockType = BlockType::Cobblestone;

	void SelectBlock(size_t index)
	{
		if (hotbarBlocks_.empty())
			return;
		hotbarIndex_ = index % hotbarBlocks_.size();
		placedBlockType = hotbarBlocks_[hotbarIndex_];
	}
	void SelectPreviousBlock()
	{
		SelectBlock((hotbarIndex_ + hotbarBlocks_.size() - 1) % hotbarBlocks_.size());
	}
	void SelectNextBlock()
	{
		SelectBlock((hotbarIndex_ + 1) % hotbarBlocks_.size());
	}
	bool smoothLighting = false;

	std::vector<glm::vec3> path;
	int windowStart = 0;
	float pathIndex = 1.0f;
	std::vector<std::shared_ptr<fe::Object>> chunkObjects; // Track loaded chunk objects
	std::vector<bool> chunksLoaded;						   // Track which chunks have been meshed
	glm::vec3 lastUp = glm::vec3(0, 1, 0);
	glm::vec3 lastRight = glm::vec3(1, 0, 0);
	glm::vec3 prevEndForward{0};
	bool hasPrevEnd = false;

	static constexpr int POINTS_PER_CHUNK = 4;
	static constexpr int SHIFT = 3;
	static constexpr int MAX_CHUNKS = 32;
	static constexpr int TUNNEL_SEGMENTS = 64;
	static constexpr int SUBDIVISIONS_PER_SEG = 48;
	int CHUNK_LOAD_DISTANCE = 6;
	int chunkOutgenDistance = 1;
	int physicsDistance = 2;
	glm::ivec2 playerCenter_{0, 0};
	static constexpr int GRID_WIDTH = 1; // 5x5 grid
	static constexpr int GRID_HEIGHT = 1;

	int NUM_CHUNKS = 4;

	float lightSpeed = 0.3f;

	float bgColorFreq = 0.3f;
	float visualizerScale = 8.0f;

	float cameraSpeed = 1.0f;
	float motionAmount = 1.2f;
	float tunnelRoundness = 0.0f;
	float animationSpeed = 1.0f;
	float farPlane = 1200.0f;
	bool freeCamera = false;
	float freeCamSpeed = 15.0f;
	float segmentLength = 12.0f;

	std::shared_ptr<fe::Object> testCube;

	std::unique_ptr<ChunkManager> chunkManager = std::make_unique<ChunkManager>(6);
#ifdef FC_INCLUDE_VULKAN
	std::unique_ptr<ChunkBatcher> chunkBatcher_;
#endif
	bool useBatcherPath_ = false;

	Foxcraft(fe::XRGameOptions options) : fe::EditableGame(options)
	{

		SetClearColor(0.1f, 0.3f, 1.0f);

#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
		if (!options.useVulkan)
			LoadShaders("resources/shaders/VertexShader_foxcraft_es.glsl", "resources/shaders/FragmentShader_foxcraft_es.glsl");
#else
		if (!options.useVulkan)
			LoadShaders("resources/shaders/VertexShader.glsl", "resources/shaders/FragmentShader.glsl");
#endif

#ifdef FC_INCLUDE_VULKAN
		if (options.useVulkan)
		{
			LoadArrayShaders("resources/shaders/VertexShader_vk_array.spv", "resources/shaders/FragmentShader_vk_array.spv");
			LoadFoxcraftShaders("resources/shaders/VertexShader_vk_foxcraft.spv", "resources/shaders/FragmentShader_vk_array.spv");
			useBatcherPath_ = true;
			chunkBatcher_ = std::make_unique<ChunkBatcher>(
				static_cast<VulkanDevice *>(renderDevice.get()),
				ChunkMesher::BlockTextures());
			chunkManager->SetBatcher(chunkBatcher_.get());
			chunkManager->SetUseBatcherPath(true);
		}
#endif

		GetPhysicsFactory()->SetGravity(glm::vec3(0.0f, -20.0f, 0.0f));

		LoadModels();
	}

	void OnDraw() override
	{
		fe::EditableGame::OnDraw();
#ifdef FC_INCLUDE_VULKAN
		if (chunkBatcher_ && useVulkan)
		{
			chunkBatcher_->Update(camera->GetPos());
			chunkBatcher_->Draw();
		}
#endif
	}

	void RebuildPlayerPhysicsBody()
	{
		fe::PhysicsFactory *physicsFactory = GetPhysicsFactory();
		if (!player || !physicsFactory)
			return;

		const glm::vec3 size = useRectangularPlayerHitbox ? glm::vec3(0.4f, 1.5f, 0.4f) : glm::vec3(1.0f, 1.0f, 1.0f);
		float radius = std::min(size.x, size.z) * 0.5f;

		auto physicsCharacter = physicsFactory->CreateCharacter(size.y, radius, this->player->state.position, useRectangularPlayerHitbox);
		if (!physicsCharacter)
			return;

		this->player->SetPhysicsCharacter(std::move(physicsCharacter));
		if (this->player->physicsCharacter)
		{
			this->player->physicsCharacter->SetPosition(this->player->state.position);
			this->player->physicsCharacter->SetJumpSpeed(this->player->jumpSpeed);
		}
	}

	void LoadModels()
	{

		this->player = std::make_shared<fe::Character>();
		this->scene->AddObject(player);
		this->player->state.position = glm::vec3(0.0f, 35.0f, 5.0f);
		this->player->gravityEnabled = true;
		this->player->jumpSpeed = 7.0f;
		this->player->moveSpeed = walkSpeed;
		RebuildPlayerPhysicsBody();
		UpdateLoadedChunks();
	}

	void UpdateLoadedChunks()
	{
		glm::vec3 playerPos = camera->GetPos();

		int playerChunkX = static_cast<int>(std::floor(playerPos.x / static_cast<float>(Chunk::WIDTH)));
		int playerChunkZ = static_cast<int>(std::floor(playerPos.z / static_cast<float>(Chunk::DEPTH)));
		playerCenter_ = {playerChunkX, playerChunkZ};

		int terrainDist = CHUNK_LOAD_DISTANCE + chunkOutgenDistance;
		glm::vec2 forward2D = glm::normalize(glm::vec2(camera->front.x, camera->front.z));
		chunkManager->LoadChunksInsideRange(playerCenter_, terrainDist, forward2D);
		chunkManager->UnloadChunksOutsideRange(playerCenter_, terrainDist);
	}

	void SyncCameraToPlayer()
	{
		if (!player || freeCamera)
			return;

		const glm::vec3 headOffset(0.0f, 1.6f, 0.0f);
		camera->SetPos(player->state.position + headOffset);
	}

	void PlaceBlock(bool remove)
	{
		glm::vec3 cameraPos = camera->GetPos();
		glm::vec3 rayDir = glm::normalize(camera->front);
		float reachDistance = 5.0f;
		float stepSize = 0.1f;

		glm::vec3 selectedBlockPos = glm::vec3(0.0f);
		glm::vec3 previousBlockPos = glm::vec3(0.0f);
		bool blockFound = false;

		for (float i = 0.0f; i < reachDistance; i += stepSize)
		{
			glm::vec3 samplePoint = cameraPos + rayDir * i;
			glm::vec3 currentBlock = glm::floor(samplePoint);

			if (chunkManager->IsBlockSolid(currentBlock))
			{
				selectedBlockPos = currentBlock;
				blockFound = true;
				break;
			}

			previousBlockPos = currentBlock;
		}

		if (blockFound)
		{
			if (remove)
				chunkManager->SetBlock(selectedBlockPos, BlockType::Air);
			else
				chunkManager->SetBlock(previousBlockPos, placedBlockType);
		}
	}

	void ProcessInput()
	{
		SDL_Event event;
		fe::SDLWindow *window = GetWindow<fe::SDLWindow>();
		while (window->PollSDLEvent(&event))
		{
			ImGui_ImplSDL3_ProcessEvent(&event);
			auto io = ImGui::GetIO();
			switch (event.type)
			{
			case SDL_EVENT_QUIT:
				window->PrepareClose();
				break;
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
				if (event.button.button == SDL_BUTTON_LEFT && !io.WantCaptureMouse)
				{
					window->StartMouseCapture();
					RefreshJoysticks();
				}
				if (window->IsCapturingMouse())
				{
					if (event.button.button == SDL_BUTTON_LEFT)
						PlaceBlock(true);
					else if (event.button.button == SDL_BUTTON_RIGHT)
						PlaceBlock(false);
				}
				break;
			case SDL_EVENT_WINDOW_RESIZED:
			case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
				Redraw();
				break;
			case SDL_EVENT_MOUSE_MOTION:
			{
				if (!window->IsCapturingMouse())
					break;
				float sensitivity = 0.1f;
				camera->yaw += event.motion.xrel * sensitivity;
				camera->pitch -= event.motion.yrel * sensitivity;
				camera->UpdateDirection();
				camera->pitch = std::clamp(camera->pitch, -89.0f, 89.0f);
				break;
			}
			case SDL_EVENT_KEY_DOWN:
				if (event.key.key == SDLK_F11)
				{
					window->ToggleFullscreen();
				}
				else if (event.key.key == SDLK_F3)
				{
					showDebugUI = !showDebugUI;
				}
				else if (event.key.key == SDLK_F2)
				{
					freeCamera = !freeCamera;
					window->StartMouseCapture();
					RefreshJoysticks();
				}
				else if (event.key.key == SDLK_F4)
				{
					ToggleXR();
				}
				else if (event.key.key == SDLK_F5)
				{
					CycleDebugView();
				}
				else if (event.key.key == SDLK_G)
				{
					SelectNextBlock();
				}
				break;
			}
		}

		if (!freeCamera)
		{
			bool sprinting = window->IsKeyDown(SDL_SCANCODE_LCTRL) || window->IsKeyDown(SDL_SCANCODE_RCTRL);
			this->player->moveSpeed = sprinting ? sprintSpeed : walkSpeed;

			if (window->IsKeyDown(SDL_SCANCODE_W))
				this->player->Move(fe::Direction::Forwards, camera.get());
			if (window->IsKeyDown(SDL_SCANCODE_A))
				this->player->Move(fe::Direction::Left, camera.get());
			if (window->IsKeyDown(SDL_SCANCODE_S))
				this->player->Move(fe::Direction::Backwards, camera.get());
			if (window->IsKeyDown(SDL_SCANCODE_D))
				this->player->Move(fe::Direction::Right, camera.get());

			if (window->IsKeyDown(SDL_SCANCODE_SPACE))
				this->player->Move(fe::Direction::Up, camera.get());
			if (window->IsKeyDown(SDL_SCANCODE_LSHIFT))
				this->player->Move(fe::Direction::Down, camera.get());

			if (!joysticks.empty())
			{
				auto& joy = joysticks[0];

				// Prefer the SDL gamepad mapping so stick/button layout stays
				// consistent no matter how the raw joystick axes are arranged.
				glm::vec2 stick = joy.IsGamepad() ? joy.GetLeftStick() : joy.GetAxis();
				const float deadzone = 0.15f;
				if (glm::length(stick) > deadzone)
				{
					glm::vec3 horizontalFront = glm::normalize(glm::vec3(camera->front.x, 0.0f, camera->front.z));
					glm::vec3 right = glm::normalize(glm::cross(horizontalFront, camera->up));
					this->player->pendingMovement += horizontalFront * -stick.y + right * stick.x;
				}

				glm::vec2 rightStick = joy.IsGamepad() ? joy.GetRightStick() : glm::vec2(joy.GetAxis(2), joy.GetAxis(3));
				if (glm::length(rightStick) > deadzone)
				{
					// Per-second sensitivity so turning feels the same regardless
					// of frame rate (e.g. vsync on/off or an open menu).
					const float sensitivity = 100.4f;
					float dt = static_cast<float>(std::max(fpsCounter.deltaTime, 0.0001));
					camera->yaw += rightStick.x * sensitivity * dt;
					camera->pitch -= rightStick.y * sensitivity * dt;
					camera->UpdateDirection();
					camera->pitch = std::clamp(camera->pitch, -89.0f, 89.0f);
				}

				if (joy.IsGamepad() ? joy.GetGamepadButton(SDL_GAMEPAD_BUTTON_SOUTH) : joy.GetButton(0))
					this->player->Move(fe::Direction::Up, camera.get());

				// Gamepad (Start) button toggles the XR session. Useful on
				// Android where there is no keyboard, and on desktop too.
				bool startDown = joy.IsGamepad() && joy.GetGamepadButton(SDL_GAMEPAD_BUTTON_START);
				if (startDown && !gamepadStartWasDown_)
					xrToggleRequested = true;
				gamepadStartWasDown_ = startDown;

				if (joy.IsGamepad())
				{
					// The two ANALOG trigger buttons (L2/R2 on the DualShock)
					// break/place a block, mirroring the mouse buttons, raised
					// once per press.
					bool ltDown = joy.GetGamepadAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 0.5f;
					if (ltDown && !gamepadLtWasDown_)
						PlaceBlock(true);
					gamepadLtWasDown_ = ltDown;

					bool rtDown = joy.GetGamepadAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.5f;
					if (rtDown && !gamepadRtWasDown_)
						PlaceBlock(false);
					gamepadRtWasDown_ = rtDown;

					// The two PUSH shoulder buttons (L1/R1) switch the hotbar
					// selection (previous/next block), like the number keys.
					bool lbDown = joy.GetGamepadButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
					if (lbDown && !gamepadLeftShoulderWasDown_)
						SelectPreviousBlock();
					gamepadLeftShoulderWasDown_ = lbDown;

					bool rbDown = joy.GetGamepadButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
					if (rbDown && !gamepadRightShoulderWasDown_)
						SelectNextBlock();
					gamepadRightShoulderWasDown_ = rbDown;

					// D-pad arrows cycle the debug rendering mode.
					bool dpadDown = joy.GetGamepadButton(SDL_GAMEPAD_BUTTON_DPAD_UP)
						|| joy.GetGamepadButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN)
						|| joy.GetGamepadButton(SDL_GAMEPAD_BUTTON_DPAD_LEFT)
						|| joy.GetGamepadButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
					if (dpadDown && !gamepadDpadWasDown_)
						CycleDebugView();
					gamepadDpadWasDown_ = dpadDown;
				}
			}
		}

		if (window->IsKeyDown(SDL_SCANCODE_ESCAPE))
			window->StopMouseCapture();
		if (ImGui::GetIO().WantCaptureMouse)
			window->StopMouseCapture();

		// Consume edge-triggered XR action requests. These are raised by
		// PollActionsAndUpdateMovement during the previous frame's draw.
		if (xrBreakBlockRequested)
		{
			PlaceBlock(true);
			xrBreakBlockRequested = false;
		}
		if (xrPlaceBlockRequested)
		{
			PlaceBlock(false);
			xrPlaceBlockRequested = false;
		}
		if (xrPrevBlockRequested)
		{
			SelectPreviousBlock();
			xrPrevBlockRequested = false;
		}
		if (xrNextBlockRequested)
		{
			SelectNextBlock();
			xrNextBlockRequested = false;
		}
	}

	void Init() override {
		auto window = this->GetWindow<fe::SDLWindow>();
		window->Show();
		window->DisableVSync();
		RefreshJoysticks();

		player->state.position.z = 5;
		player->state.position.y = 35;
		if (player->physicsCharacter)
		{
			player->physicsCharacter->SetPosition(player->state.position);
		}
		camera->farDist = farPlane;
		camera->SetAspect(camera->aspect);
		player->state.velocity = glm::vec3(0.0f);
		SyncCameraToPlayer();
	}

	void Step() override {
		auto window = GetWindow<fe::SDLWindow>();
		ProcessInput();

		UpdateLoadedChunks();

		{
			glm::vec2 forward2D = glm::normalize(glm::vec2(camera->front.x, camera->front.z));
			chunkManager->UpdatePausedState(playerCenter_, forward2D);
		}

		chunkManager->Update(1, GetPhysicsFactory(), this->scene.get(),
								playerCenter_, CHUNK_LOAD_DISTANCE, physicsDistance);

		if (freeCamera)
		{
			double dt = fpsCounter.deltaTime;
			float spd = freeCamSpeed * dt;
			glm::vec3 cp = camera->GetPos();
			glm::vec3 right = glm::normalize(glm::cross(camera->front, camera->up));
			if (window->IsKeyDown(SDL_SCANCODE_W))
				cp += camera->front * spd;
			if (window->IsKeyDown(SDL_SCANCODE_S))
				cp -= camera->front * spd;
			if (window->IsKeyDown(SDL_SCANCODE_A))
				cp -= right * spd;
			if (window->IsKeyDown(SDL_SCANCODE_D))
				cp += right * spd;
			if (window->IsKeyDown(SDL_SCANCODE_SPACE))
				cp += camera->up * spd;
			if (window->IsKeyDown(SDL_SCANCODE_LSHIFT))
				cp -= camera->up * spd;
			camera->SetPos(cp);
		}
		else
		{
		}

		Update();

		if (player->physicsCharacter)
		{
			glm::vec3 ppos = player->physicsCharacter->GetPosition();
			if (ppos.y < -2.0f)
			{
				int surface = chunkManager->GetSurfaceHeight(static_cast<int>(ppos.x), static_cast<int>(ppos.z));
				if (surface > 0)
				{
					ppos.y = static_cast<float>(surface) + 1.0f;
					player->physicsCharacter->SetPosition(ppos);
					player->physicsCharacter->SetLinearVelocity(glm::vec3(0.0f));
					player->state.position = ppos;
				}
			}
		}

		if (!freeCamera)
		{
			SyncCameraToPlayer();
		}

		Redraw();
	}

	void InitUI() override {}

	void DrawUI() override
	{
		if (!showDebugUI)
			return;
		BeginFrame();

		ImGui::Begin("Chunks");
		{
			ImGui::DragInt("Render Distance", &CHUNK_LOAD_DISTANCE);
			ImGui::DragInt("Physics Distance", &physicsDistance, 0.5f, 0, 20);
			ImGui::SliderInt("Terrain Pre-gen", &chunkOutgenDistance, 1, 10);

			if (ImGui::Checkbox("Smooth Lighting", &smoothLighting))
			{
				chunkManager->SetSmoothLighting(smoothLighting);
				chunkManager->RemeshAll();
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(G places %s)",
				placedBlockType == BlockType::Glowstone ? "Glowstone"
				: placedBlockType == BlockType::Stone ? "Stone"
				: placedBlockType == BlockType::Dirt ? "Dirt"
				: placedBlockType == BlockType::Grass ? "Grass"
				: placedBlockType == BlockType::Bedrock ? "Bedrock"
				: "Cobblestone");
		}
		ImGui::End();

		DrawDebugUI();

		EndFrame();
	}
};
