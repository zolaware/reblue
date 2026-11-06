// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <string>

namespace bd::launcher {
namespace fs = std::filesystem;
inline constexpr const char *kOwner = ".recomplauncher-content";
using Sources = std::array<fs::path, 3>;
using Inspector = std::function<std::array<bool, 3>(const fs::path &)>;
using Progress = std::function<void(double, const std::string &)>;
using Extractor = std::function<void(const Sources &, const fs::path &,
                                    const Progress &)>;

Sources FindDiscs(const fs::path &input, const Inspector &inspect);
bool Overlaps(const fs::path &a, const fs::path &b);
bool Owned(const fs::path &game);
fs::path PreparedGame(const fs::path &install);
void Prepare(const fs::path &input, const fs::path &install,
             const Inspector &inspect, const Extractor &extract,
             const Progress &progress);
} // namespace bd::launcher
