/**
 * @file    platform/reboot.h
 * @brief   Warm reboot (host process relaunch) for restart-bound settings and
 * DLC changes.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace bd::platform {

// The active profile's reblue.toml once SetProfileContext has run, else
// <exe_dir>/reblue.toml.
std::filesystem::path ConfigFilePath();

// So warm reboot persists to the profile's config and re-passes --bd.profile
// to the fresh instance. Call once during path setup.
void SetProfileContext(std::string profile_name,
                       std::filesystem::path config_path);

// Saves the config and requests a restart with the active profile. Callable
// from any thread.
void RequestWarmReboot();

// The arguments that select the active profile in a new process.
std::vector<std::string> ProfileArgs();

// Spawns exe in place of this process, forwarding the active profile and
// --repair if requested. True if it launched. The caller shuts down either way.
bool SpawnReplacement(const std::filesystem::path &exe, bool repair);

} // namespace bd::platform
