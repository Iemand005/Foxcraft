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

#include <fstream>
#include <sstream>
#include <chrono>
#ifdef __ANDROID__
#include <SDL3/SDL_system.h>
#include <SDL3/SDL_filesystem.h>
#include <unistd.h>
#include <sys/stat.h>
#endif
#ifdef FC_INCLUDE_VULKAN
#include <Graphics/VulkanDevice.hpp>
#endif

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

#ifdef __ANDROID__
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <fcntl.h>

static void AndroidCopyFile(AAssetManager* assets, const std::string& destRoot, const std::string& relPath)
{
	AAsset* asset = AAssetManager_open(assets, relPath.c_str(), AASSET_MODE_STREAMING);
	if (!asset) return;

	std::string outPath = destRoot + "/" + relPath;
	size_t lastSlash = outPath.find_last_of('/');
	if (lastSlash != std::string::npos) {
		std::string dir = outPath.substr(0, lastSlash);
		if (!SDL_CreateDirectory(dir.c_str())) {
			SDL_ClearError();
		}
	}
	int fd = open(outPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd >= 0) {
		char buf[16384];
		int n;
		while ((n = AAsset_read(asset, buf, sizeof(buf))) > 0) {
			write(fd, buf, n);
		}
		close(fd);
	}
	AAsset_close(asset);
}

static void AndroidExtractDir(AAssetManager* assets, const std::string& destRoot, const std::string& relDir)
{
	AAssetDir* dir = AAssetManager_openDir(assets, relDir.c_str());
	if (!dir) return;
	const char* name;
	while ((name = AAssetDir_getNextFileName(dir)) != NULL) {
		std::string rel = relDir.empty() ? std::string(name) : relDir + "/" + std::string(name);

		// Open as a file; if we get NULL it's a directory, so recurse into it.
		AAsset* probe = AAssetManager_open(assets, rel.c_str(), AASSET_MODE_UNKNOWN);
		if (probe) {
			AAsset_close(probe);
			AndroidCopyFile(assets, destRoot, rel);
		} else {
			AndroidExtractDir(assets, destRoot, rel);
		}
	}
	AAssetDir_close(dir);
}

static bool AndroidExtractAssets()
{
	JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
	if (!env) return false;
	jobject activity = (jobject)SDL_GetAndroidActivity();
	if (!activity) return false;
	jclass activityClass = env->GetObjectClass(activity);
	jmethodID getAssets = env->GetMethodID(activityClass, "getAssets", "()Landroid/content/res/AssetManager;");
	if (!getAssets) return false;
	jobject assetManager = env->CallObjectMethod(activity, getAssets);
	if (!assetManager) return false;

	AAssetManager* mgr = AAssetManager_fromJava(env, assetManager);
	env->DeleteLocalRef(assetManager);
	if (!mgr) return false;

	const char* internalPath = SDL_GetAndroidInternalStoragePath();
	if (!internalPath) return false;
	std::string destRoot(internalPath);

	AndroidExtractDir(mgr, destRoot, "resources");

	if (chdir(destRoot.c_str()) != 0) {
		return false;
	}
	return true;
}
#endif

int main(int argc, char* argv[]) {

	std::cout << "Hiii" << std::endl;

#ifdef __ANDROID__
	if (AndroidExtractAssets()) {
		std::cout << "Assets extracted to internal storage" << std::endl;
	} else {
		std::cerr << "Failed to extract assets" << std::endl;
	}
#endif

	try {
		LogToFile("Creating Foxcraft game instance...");

		fe::XRGameOptions options(1200, 800);
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
	return main(0, nullptr);
}

#endif
