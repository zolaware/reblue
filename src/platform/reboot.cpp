/**
 * @file    platform/reboot.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "platform/reboot.h"
#include "core/app_root.h"
#include "core/encoding.h"
#include "core/logging.h"

#include <atomic>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <string>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/platform/env.h>

#if defined(_WIN32)
#include "core/windows_lean.h"
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <vector>
#endif

namespace bd::platform {
namespace {

std::function<void()> g_handler;
std::mutex g_handler_mutex;
std::atomic<bool> g_requested{false};
std::string g_active_profile;
std::filesystem::path g_config_path;

#if defined(_WIN32)
constexpr wchar_t kInstanceLockName[] = L"Local\\reblue-single-instance";
HANDLE g_instance_lock = nullptr;
#else
constexpr char kInstanceLockFile[] = "reblue.lock";
int g_instance_lock = -1;
#endif

void ReleaseInstanceLock() {
#if defined(_WIN32)
  if (!g_instance_lock)
    return;
  ::CloseHandle(g_instance_lock);
  g_instance_lock = nullptr;
#else
  if (g_instance_lock < 0)
    return;
  ::close(g_instance_lock);
  g_instance_lock = -1;
#endif
}

// /proc/self/exe inside an AppImage is the inner binary in a FUSE mount that
// dies with this process, so relaunching must use the AppImage file itself.
std::filesystem::path ExecutablePath() {
#if !defined(_WIN32) && !defined(__APPLE__)
  if (auto appimage = rex::platform::env::get("APPIMAGE");
      appimage && !appimage->empty())
    return std::filesystem::path(*appimage);
#endif
  return rex::filesystem::GetExecutablePath();
}

bool DetectSteamGameMode() {
  // gamescope-session, the session Game Mode runs, stamps itself here.
  if (auto desktop = rex::platform::env::get("XDG_CURRENT_DESKTOP")) {
    std::string lowered = *desktop;
    for (char &c : lowered)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lowered.find("gamescope") != std::string::npos)
      return true;
  }
  // Exported by gamescope to everything it launches. Desktop Mode does not set
  // it unless the user asked for a gamescope wrapper, where exiting instead of
  // relaunching is a harmless degradation.
  if (auto display = rex::platform::env::get("GAMESCOPE_WAYLAND_DISPLAY");
      display && !display->empty())
    return true;
  return false;
}

#if defined(_WIN32)
bool SpawnProcess(const std::filesystem::path &exe, bool repair) {
  std::wstring cmdline = L"\"" + exe.wstring() + L"\"";
  if (!g_active_profile.empty())
    cmdline +=
        L" --profile \"" + bd::Utf8ToWide(g_active_profile) + L"\"";
  if (repair)
    cmdline += L" --repair";
  std::wstring workdir = exe.parent_path().wstring();

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};

  if (!::CreateProcessW(exe.c_str(), cmdline.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, workdir.c_str(), &si, &pi)) {
    BD_ERROR("[reboot] CreateProcessW failed (err {})", ::GetLastError());
    return false;
  }
  // Without this the replacement opens behind whatever the user switched to
  // while this process was quitting: Windows denies SetForegroundWindow to a
  // process that never held it.
  ::AllowSetForegroundWindow(pi.dwProcessId);
  ::CloseHandle(pi.hThread);
  ::CloseHandle(pi.hProcess);
  return true;
}
#else
bool SpawnProcess(const std::filesystem::path &exe, bool repair) {
  std::vector<std::string> args;
  args.push_back(exe.string());
  if (!g_active_profile.empty()) {
    args.push_back("--profile");
    args.push_back(g_active_profile);
  }
  if (repair)
    args.push_back("--repair");
  std::vector<char *> argv;
  argv.reserve(args.size() + 1);
  for (auto &a : args)
    argv.push_back(a.data());
  argv.push_back(nullptr);

  pid_t pid = ::fork();
  if (pid < 0) {
    BD_ERROR("[reboot] fork failed (errno {})", errno);
    return false;
  }
  if (pid == 0) {
    ::setsid(); // detach so the child outlives the exiting parent
    std::error_code ec;
    std::filesystem::current_path(exe.parent_path(), ec);
    ::execv(exe.c_str(), argv.data());
    ::_exit(127); // execv returns only on failure
  }
  return true;
}
#endif

} // namespace

bool AcquireInstanceLock() {
#if defined(_WIN32)
  HANDLE h = ::CreateMutexW(nullptr, FALSE, kInstanceLockName);
  if (!h) {
    BD_WARN("[reboot] instance lock unavailable (err {})", ::GetLastError());
    return true;
  }
  if (::GetLastError() == ERROR_ALREADY_EXISTS) {
    ::CloseHandle(h);
    return false;
  }
  g_instance_lock = h;
  return true;
#else
  std::string dir = "/tmp";
  if (auto runtime = rex::platform::env::get("XDG_RUNTIME_DIR");
      runtime && !runtime->empty())
    dir = *runtime;
  const std::string path = dir + "/" + kInstanceLockFile;

  const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  if (fd < 0) {
    BD_WARN("[reboot] instance lock {} unavailable (errno {})", path, errno);
    return true;
  }
  if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
    ::close(fd);
    return false;
  }
  g_instance_lock = fd;
  return true;
#endif
}

bool SpawnReplacement(const std::filesystem::path &exe, bool repair) {
  ReleaseInstanceLock();
  if (SpawnProcess(exe, repair))
    return true;
  AcquireInstanceLock();
  return false;
}

bool IsSteamGameMode() {
  static const bool game_mode = DetectSteamGameMode();
  return game_mode;
}

std::filesystem::path ConfigFilePath() {
  if (!g_config_path.empty())
    return g_config_path;
  return AppRootFolder() / "reblue.toml";
}

void SetProfileContext(std::string profile_name,
                       std::filesystem::path config_path) {
  g_active_profile = std::move(profile_name);
  g_config_path = std::move(config_path);
}

void SetWarmRebootHandler(std::function<void()> handler) {
  std::lock_guard<std::mutex> lk(g_handler_mutex);
  g_handler = std::move(handler);
}

void RequestWarmReboot() {
  bool expected = false;
  if (!g_requested.compare_exchange_strong(expected, true))
    return;

  std::function<void()> handler;
  {
    std::lock_guard<std::mutex> lk(g_handler_mutex);
    handler = g_handler;
  }
  if (!handler) {
    BD_ERROR("[reboot] no handler registered, cannot relaunch");
    g_requested.store(false);
    return;
  }
  BD_WARN("[reboot] warm reboot requested");
  handler();
}

bool RelaunchSelf(bool repair) {
  return SpawnReplacement(ExecutablePath(), repair);
}

[[noreturn]] void PerformWarmReboot(const std::function<void()> &quiesce) {
  // 1. Settings are not auto-persisted, so write them before relaunch.
  rex::cvar::SaveConfig(ConfigFilePath());

  // 2. Quiesce and release the GPU first: the replacement creates its own
  //    device and swap chain, and two processes owning the window's surface at
  //    once is exactly the overlap that wedges drivers.
  if (quiesce)
    quiesce();

  // 3. Steam owns the launched process in Game Mode, so a spawn from here would
  //    start outside the session with no focus while Steam counts the game as
  //    stopped. Exit clean and let the user press play again.
  if (IsSteamGameMode()) {
    BD_WARN("[reboot] steam game mode: exiting instead of relaunching");
    rex::FlushLogging();
    std::_Exit(0);
  }

  // 4. Spawn after the drain: if it fails there is nothing left to render with,
  //    so this is a hard failure rather than the old stay-in-session fallback.
  if (!SpawnReplacement(ExecutablePath(), false)) {
    BD_ERROR("[reboot] relaunch failed after teardown, exiting");
    rex::FlushLogging();
    std::_Exit(1);
  }

  // 5. Flush the writers that stay up. No subsystem teardown beyond the
  // renderer (it
  //    would deadlock on a host lock a straggler still holds).
  rex::FlushLogging();

  // 6. Exit without running destructors.
  std::_Exit(0);
}

} // namespace bd::platform
