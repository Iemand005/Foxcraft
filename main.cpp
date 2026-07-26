#include "XRGame.hpp"
#ifdef _WIN32
// #define _WINSOCKAPI_
// #include <winsock2.h>
// #include <windows.h>
#else
#include <X11/Xlib.h>
#endif
#include <string>
#include <cstring>
#include <iostream>
#include "Foxcraft.hpp"

#include <fstream>
#include <sstream>
#include <chrono>
#include <Graphics/VulkanDevice.hpp>

void LogToFile(const std::string& message)
{
	std::cout << message << std::endl;
#ifdef _WIN32
	std::string logpath = "C:\\Temp\\Cake_screensaver.log";
#else
	std::string logpath = "/tmp/Cake_screensaver.log";
#endif
	try {
		std::ofstream file(logpath, std::ios::app);
		if (!file.is_open()) return;
		auto now = std::chrono::system_clock::now();
		auto time = std::chrono::system_clock::to_time_t(now);
		file << "[" << std::ctime(&time) << "] " << message << "\n";
		file.close();
	} catch (...) { }
}

int main() {

	std::cout << "Hiii" << std::endl;

	try {
		LogToFile("Creating Foxcraft game instance...");

		// VulkanDevice::SetPreferIntegratedGPU(true);

		fe::XRGameOptions options(1000, 1000);
		options.useVulkan = false;
		Foxcraft game(options);

		LogToFile("Running game...");
		game.Run();

		LogToFile("Game exited normally");
	} catch (const std::exception& e) {
		LogToFile(std::string("Exception caught: ") + e.what());
	} catch (...) {
		LogToFile("Unknown exception caught");
	}

	return 0;
}

#ifdef _WIN32

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR lpCmdLine, int) {
	return main();
}

#endif
