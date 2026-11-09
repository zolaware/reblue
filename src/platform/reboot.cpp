/**
 * @file    platform/reboot.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "platform/reboot.h"
#include "core/app_root.h"
#include "core/cvar_table.h"
#include "core/encoding.h"
#include "core/logging.h"

#include <filesystem>
#include <string>
#include <vector>

#include <rex/api.h>
#include <rex/app.h>
#include <rex/cvar.h>
#include <rex/logging.h>

#if defined(_WIN32)
#include "core/windows_lean.h"
#else
#include <cerrno>
#include <unistd.h>
#endif

namespace bd::platform {
namespace {

std::string g_active_profile;
std::filesystem::path g_config_path;

#if defined(_WIN32)
bool SpawnProcess(const std::filesystem::path &exe, bool repair) {
  std::wstring cmdline = L"\"" + exe.wstring() + L"\"";
  if (!g_active_profile.empty())
    cmdline +=
        L" --bd.profile \"" + bd::Utf8ToWide(g_active_profile) + L"\"";
  if (repair)
    cmdline += L" --repair";
  std::wstring workdir = exe.parent_path().wstring();

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};

  if (!::CreateProcessW(exe.c_str(), cmdline.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, workdir.c_str(), &si, &pi)) {
    REXLOG_ERROR(bd, "reboot CreateProcessW failed (err {})", ::GetLastError());
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
    args.push_back("--bd.profile");
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
    REXLOG_ERROR(bd, "reboot fork failed (errno {})", errno);
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

bool SpawnReplacement(const std::filesystem::path &exe, bool repair) {
  return SpawnProcess(exe, repair);
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

void RequestWarmReboot() {
  if (auto saved = rex::SaveConfigFile(bd::Cvars(), ConfigFilePath()); !saved)
    REXLOG_WARN(bd, "config save failed: {}", saved.error().message);
  rex::RequestRestart(ProfileArgs());
}

std::vector<std::string> ProfileArgs() {
  return {"--bd.profile", g_active_profile};
}

} // namespace bd::platform
