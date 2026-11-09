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
#include "core/logging.h"

#include <filesystem>
#include <string>
#include <vector>

#include <rex/api.h>
#include <rex/app.h>
#include <rex/cvar.h>
#include <rex/logging.h>

namespace bd::platform {
namespace {

std::string g_active_profile;
std::filesystem::path g_config_path;

} // namespace

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
