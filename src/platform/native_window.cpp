/**
 * @file    platform/native_window.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "platform/native_window.h"

#include <SDL3/SDL_video.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <SDL3/SDL_metal.h>

#if TARGET_OS_IOS && !TARGET_OS_MACCATALYST
#include "platform/ios_support.h"
#else
#include <mach-o/dyld.h>
#endif
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#endif

#include <rex/ui/window.h>

#include "core/logging.h"
#include "platform/host_resources.h"

namespace bd::platform {

#if defined(_WIN32)

bool GetNativeRenderWindow(rex::ui::Window *window, plume::RenderWindow &out) {
  out = static_cast<plume::RenderWindow>(window->GetNativeWindowHandle());
  if (!out) {
    BD_ERROR("Window has no native HWND yet");
    return false;
  }
  return true;
}

#elif defined(__APPLE__)

namespace {

void SetenvIfUnset(const char *name, const char *value) {
  if (const char *cur = std::getenv(name); !cur || !cur[0])
    setenv(name, value, 1);
}

// volk dlopen's libvulkan/libMoltenVK by leaf name, and dyld snapshots
// DYLD_LIBRARY_PATH at launch, so setting it in-process is too late: the run
// that needs it has to be a fresh exec. Priority 101 (the lowest the attribute
// allows) puts this ahead of every other initializer in the image, so the exec
// cannot tear down state one of them already built.
__attribute__((constructor(101))) void
ConfigureMoltenVKAndReexec(int /*argc*/, char **argv) {
  RaiseFDLimit();

  SetenvIfUnset("SDL_MAC_PRESS_AND_HOLD", "0");

  SetenvIfUnset("MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS", "1");
  // Style 3 is the single-Metal-queue mode.
  SetenvIfUnset("MVK_CONFIG_SEMAPHORE_SUPPORT_STYLE", "3");
  SetenvIfUnset("MVK_CONFIG_PRESENT_WITH_COMMAND_BUFFER", "1");

#if TARGET_OS_IOS && !TARGET_OS_MACCATALYST
  // iOS loads Frameworks/libMoltenVK.dylib via @rpath and forbids execv.
  return;
#else
  if (std::getenv("REBLUE_MOLTENVK_RELAUNCHED"))
    return; // already relaunched

  char exe_buf[4096];
  uint32_t exe_size = sizeof(exe_buf);
  if (_NSGetExecutablePath(exe_buf, &exe_size) != 0)
    return;
  const std::string exe(exe_buf);
  const std::string exe_dir = exe.substr(0, exe.find_last_of('/'));

  const std::string dirs[] = {
      exe_dir + "/vulkan/lib", // SDK-staged runtime (rexglue_configure_target)
      "/opt/homebrew/lib",     // Homebrew molten-vk
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
    return; // nothing to add, volk will report the failure

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
  // execv only returns on failure, fall through and let startup continue.
#endif
}

} // namespace

bool GetNativeRenderWindow(rex::ui::Window *window, plume::RenderWindow &out) {
  // The SDK owns the single SDL window, fetch it the same way the X11 path and
  // reblue_app.cpp's ApplyWindowSizeConstraints do.
  (void)window;
  int count = 0;
  SDL_Window **windows = SDL_GetWindows(&count);
  SDL_Window *sdl_window = (windows && count > 0) ? windows[0] : nullptr;
  SDL_free(windows);
  if (!sdl_window) {
    BD_ERROR("No SDL window available for the Vulkan surface");
    return false;
  }

  SDL_PropertiesID props = SDL_GetWindowProperties(sdl_window);
#if TARGET_OS_IOS && !TARGET_OS_MACCATALYST
  out.window = SDL_GetPointerProperty(
      props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
#else
  out.window = SDL_GetPointerProperty(
      props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#endif

  // The view is a subview of the SDL window and dies with it, so it is cached
  // against the window it was made for rather than unconditionally: a
  // recreated window gets a fresh view instead of a dangling one.
  static SDL_Window *view_owner = nullptr;
  static SDL_MetalView metal_view = nullptr;
  if (!metal_view || view_owner != sdl_window) {
    metal_view = SDL_Metal_CreateView(sdl_window);
    view_owner = sdl_window;
  }
  if (!metal_view) {
    BD_ERROR("SDL_Metal_CreateView failed: {}", SDL_GetError());
    return false;
  }
#if TARGET_OS_IOS && !TARGET_OS_MACCATALYST
  ios::FitMetalView(out.window, metal_view);
#endif
  out.view = SDL_Metal_GetLayer(metal_view);

  if (!out.window || !out.view) {
    BD_ERROR(
        "SDL window exposed no NSWindow/CAMetalLayer for the Metal surface");
    return false;
  }
  return true;
}

#else

bool GetNativeRenderWindow(rex::ui::Window *window, plume::RenderWindow &out) {
  // The SDK exposes no SDL_Window accessor and GetNativeWindowHandle is null
  // off Windows. The app owns a single window, fetched the same way
  // ApplyWindowSizeConstraints does. plume takes it whole so the surface
  // follows whichever video driver SDL picked.
  (void)window;
  int count = 0;
  SDL_Window **windows = SDL_GetWindows(&count);
  out = (windows && count > 0) ? windows[0] : nullptr;
  SDL_free(windows);
  if (!out) {
    BD_ERROR("No SDL window available for the Vulkan surface");
    return false;
  }
  return true;
}

#endif

} // namespace bd::platform
