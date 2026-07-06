#ifdef _WIN32
#define _WINSOCKAPI_
#include <winsock2.h>
#include <windows.h>
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

void LogToFile(const std::string& message)
{
  #ifdef _WIN32
  std::string logpath = "C:\\Temp\\Cake_screensaver.log";
  #else
  std::string logpath = "/tmp/Cake_screensaver.log";
  #endif
  try
  {
    std::ofstream file(logpath, std::ios::app);
    if (file.is_open())
    {
      auto now = std::chrono::system_clock::now();
      auto time = std::chrono::system_clock::to_time_t(now);
      file << "[" << std::ctime(&time) << "] " << message << "\n";
      file.close();
    }
  }
  catch (...) { }
}

int main() {

  std::cout << "Hiii" << std::endl;

  Foxcraft game;

  game.Run();
  return 0;
}

#ifdef _WIN32

int WINAPI WinMain(
  HINSTANCE,
  HINSTANCE,
  LPSTR lpCmdLine,
  int
)
{
  LogToFile("=== Screensaver started ===");
  LogToFile(std::string("Command line: ") + (lpCmdLine ? lpCmdLine : "(null)"));

  ScreenSaverMode mode = ScreenSaverMode::Window;
  HWND previewHwnd = nullptr;

  std::string cmd = lpCmdLine ? lpCmdLine : "";
  std::string cur;

  for (char c : cmd)
  {
    if (c == ' ')
    {
      if (!cur.empty())
      {
        if (_stricmp(cur.c_str(), "/s") == 0)
          mode = ScreenSaverMode::Fullscreen;
        else if (_stricmp(cur.c_str(), "/c") == 0)
          mode = ScreenSaverMode::Config;
        else if (_stricmp(cur.c_str(), "/p") == 0)
          mode = ScreenSaverMode::Preview;
        else if (mode == ScreenSaverMode::Preview)
        {
          // Parse the window handle safely
          try
          {
            unsigned long long handle = std::stoull(cur);
            previewHwnd = (HWND)handle;
          }
          catch (const std::exception& e)
          {
            OutputDebugStringA("Failed to parse preview window handle: ");
            OutputDebugStringA(e.what());
            OutputDebugStringA("\n");
          }
        }
        cur.clear();
      }
    }
    else
    {
      cur += c;
    }
  }

  if (!cur.empty())
  {
    if (_stricmp(cur.c_str(), "/s") == 0)
      mode = ScreenSaverMode::Fullscreen;
    else if (_stricmp(cur.c_str(), "/c") == 0)
      mode = ScreenSaverMode::Config;
    else if (_stricmp(cur.c_str(), "/p") == 0)
      mode = ScreenSaverMode::Preview;
    else if (mode == ScreenSaverMode::Preview)
    {
      try
      {
        unsigned long long handle = std::stoull(cur);
        previewHwnd = (HWND)handle;
      }
      catch (const std::exception& e)
      {
        OutputDebugStringA("Failed to parse preview window handle: ");
        OutputDebugStringA(e.what());
        OutputDebugStringA("\n");
      }
    }
  }

  try
  {
    LogToFile("Creating game instance...");
    Cake game;

    LogToFile("Activating screensaver mode...");
    game.ActivateScreenSaverMode(mode, previewHwnd);

    LogToFile("Running game...");
    game.Run();

    LogToFile("Game exited normally");
  }
  catch (const std::exception& e)
  {
    LogToFile(std::string("Exception caught: ") + e.what());
  }
  catch (...)
  {
    LogToFile("Unknown exception caught");
  }


  return 0;
}

#endif
