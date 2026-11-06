// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace bd::launcher {
// Handles setup before SDL/window creation. Play rewrites args for native boot.
std::optional<int> Configure(std::vector<std::string> &args);
bool ExternalLaunch();
void AttachStandardStreams();
#ifdef REBLUE_BUILD_INSTALLER
// Uses the same SDK disc reader and title validation as headless preparation.
std::array<bool, 3> InspectDiscSource(const std::filesystem::path &path);
#endif
} // namespace bd::launcher
