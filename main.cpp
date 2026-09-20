#include "XRGame.hpp"
#if defined(_WIN32)
// #define _WINSOCKAPI_
// #include <winsock2.h>
// #include <windows.h>
#elif !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
#include <X11/Xlib.h>
#endif
#include <string>
#include <cstring>
#include <iostream>
#include "Foxcraft.hpp"

#ifdef __ANDROID__
// On Android SDL3 renames main -> SDL_main (SDL_MAIN_NEEDED) and the Java glue
// loads libmain.so and calls the exported SDL_main symbol.
#include <SDL3/SDL_main.h>
#endif

#include <chrono>
#include <exception>

#include "Log.hpp"
#include "Android.hpp"

#ifdef FC_INCLUDE_VULKAN
#include <Graphics/VulkanDevice.hpp>
#endif

int main(int argc, char* argv[]) {

	fe::LogSetTag("Foxcraft");
	fe::Log("Foxcraft starting");

	// The engine extracts APK assets + chdir for us inside Game/XRGame
	// construction, so games don't need any per-platform bootstrap here.
	// On desktop this is a no-op.
	fe::AndroidSetupAssets("resources");

	try {
		fe::LogToFile("Creating Foxcraft game instance...");

		fe::XRGameOptions options(1200, 800);
		options.useVulkan = false;
		Foxcraft game(options);

		fe::LogToFile("Running game...");

		game.Run();

		fe::LogToFile("Game exited normally");
	} catch (const std::exception& e) {
		fe::LogToFile(std::string("Exception caught: ") + e.what());
	} catch (...) {
		fe::LogToFile("Unknown exception caught");
	}

	return 0;
}

#ifdef _WIN32

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR lpCmdLine, int) {
	return main(0, nullptr);
}

#endif