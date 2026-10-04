/**
 * @file    core/app_root.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "core/app_root.h"

#include <fstream>

#include <rex/filesystem.h>
#include <rex/platform/env.h>

#include "core/settings.h"

namespace bd {

namespace {

#if defined(__APPLE__)
bool IsMacAppBundle() {
  const auto executable_dir = rex::filesystem::GetExecutableFolder();
  if (executable_dir.filename() != "MacOS")
    return false;
  const auto contents_dir = executable_dir.parent_path();
  return contents_dir.filename() == "Contents" &&
         contents_dir.parent_path().extension() == ".app";
}
#endif

std::filesystem::path g_app_root_override;

} // namespace

bool IsPackagedApplication() {
#if !defined(_WIN32)
  if (rex::platform::env::get("APPIMAGE"))
    return true;
#if defined(__APPLE__)
  return IsMacAppBundle();
#endif
#endif
  return false;
}

std::filesystem::path UserConfigFolder() {
#if !defined(_WIN32)
  if (auto xdg = rex::platform::env::get("XDG_CONFIG_HOME");
      xdg && !xdg->empty())
    return std::filesystem::path(*xdg) / "reblue";
  if (auto home = rex::platform::env::get("HOME"); home && !home->empty())
    return std::filesystem::path(*home) / ".config" / "reblue";
#endif
  return {};
}

void SetAppRoot(const std::filesystem::path &root) {
  if (!root.empty())
    g_app_root_override = root;
}

std::filesystem::path CacheRootFor(const std::filesystem::path &root) {
  const auto &override_path = bd::Settings::Get().CachePath();
  return override_path.empty() ? root / "cache"
                               : std::filesystem::path(override_path);
}

std::filesystem::path AppRootFolder() {
  if (!g_app_root_override.empty())
    return g_app_root_override;
#if !defined(_WIN32)
  // An AppImage mount is a read-only FUSE, so nothing can be written beside the
  // executable. Everything else keeps the exe dir layout.
  if (rex::platform::env::get("APPIMAGE")) {
    if (auto config = UserConfigFolder(); !config.empty())
      return config;
  }
#if defined(__APPLE__)
  if (IsMacAppBundle()) {
    if (auto home = rex::platform::env::get("HOME"); home && !home->empty())
      return std::filesystem::path(*home) / "Library" / "Application Support" /
             "reblue";
  }
#endif
#endif
  return rex::filesystem::GetExecutableFolder();
}

bool DirectoryWritable(const std::filesystem::path &dir) {
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec))
    return false;
  const auto probe = dir / ".reblue_write_probe";
  {
    std::ofstream out(probe, std::ios::trunc);
    if (!out)
      return false;
  }
  std::filesystem::remove(probe, ec);
  return true;
}

} // namespace bd
