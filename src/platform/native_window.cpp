/**
 * @file    platform/native_window.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "platform/native_window.h"

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#endif

#include <rex/ui/ui_driver.h>

#include "core/logging.h"
#include <rex/platform/process.h>

namespace bd::platform {

#if defined(_WIN32)

bool GetNativeRenderWindow(const rex::NativeWindow &native,
                           plume::RenderWindow &out) {
  if (native.type != rex::NativeWindow::Type::kWin32 || !native.window) {
    REXBD_ERROR("Window has no native HWND yet");
    return false;
  }
  out = static_cast<plume::RenderWindow>(native.window);
  return true;
}

#elif defined(__APPLE__)

namespace {

void SetenvIfUnset(const char *name, const char *value) {
  if (const char *cur = std::getenv(name); !cur || !cur[0])
    setenv(name, value, 1);
}

__attribute__((constructor(101))) void
ConfigureMoltenVKAndReexec(int /*argc*/, char **argv) {
  (void)rex::platform::RaiseOpenFileLimit();

  SetenvIfUnset("SDL_MAC_PRESS_AND_HOLD", "0");

  SetenvIfUnset("MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS", "1");
  SetenvIfUnset("MVK_CONFIG_SEMAPHORE_SUPPORT_STYLE", "3");
  SetenvIfUnset("MVK_CONFIG_PRESENT_WITH_COMMAND_BUFFER", "1");

  if (std::getenv("REBLUE_MOLTENVK_RELAUNCHED"))
    return;

  char exe_buf[4096];
  uint32_t exe_size = sizeof(exe_buf);
  if (_NSGetExecutablePath(exe_buf, &exe_size) != 0)
    return;
  const std::string exe(exe_buf);
  const std::string exe_dir = exe.substr(0, exe.find_last_of('/'));

  const std::string dirs[] = {
      exe_dir + "/vulkan/lib",
      "/opt/homebrew/lib",
      "/usr/local/lib",
  };
  std::string vk_path;
  for (const std::string &dir : dirs) {
    if (access((dir + "/libMoltenVK.dylib").c_str(), R_OK) != 0)
      continue;
    if (!vk_path.empty())
      vk_path += ':';
    vk_path += dir;
  }
  if (vk_path.empty())
    return;

  std::string dyld_path = vk_path;
  if (const char *existing = std::getenv("DYLD_LIBRARY_PATH");
      existing && *existing) {
    if (std::string(existing).find(vk_path) != std::string::npos)
      return;
    dyld_path += ':';
    dyld_path += existing;
  }
  setenv("DYLD_LIBRARY_PATH", dyld_path.c_str(), 1);
  setenv("REBLUE_MOLTENVK_RELAUNCHED", "1", 1);
  execv(exe.c_str(), argv);
}

} // namespace

bool GetNativeRenderWindow(const rex::NativeWindow &native,
                           plume::RenderWindow &out) {
  if (native.type != rex::NativeWindow::Type::kCocoa || !native.window ||
      !native.layer) {
    REXBD_ERROR(
        "Window exposed no NSWindow/CAMetalLayer for the Metal surface");
    return false;
  }
  out.window = native.window;
  out.view = native.layer;
  return true;
}

#else

bool GetNativeRenderWindow(const rex::NativeWindow &native,
                           plume::RenderWindow &out) {
  out = static_cast<SDL_Window *>(native.sdl);
  if (!out) {
    REXBD_ERROR("No SDL window available for the Vulkan surface");
    return false;
  }
  return true;
}

#endif

} // namespace bd::platform
