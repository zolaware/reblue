// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace bd::launcher {
namespace fs = std::filesystem;
inline constexpr const char *kOwner = ".recomplauncher-content";
using Sources = std::array<fs::path, 3>;
using Inspector = std::function<std::array<bool, 3>(const fs::path &)>;
using Progress = std::function<void(double, const std::string &)>;
using Extractor = std::function<void(const Sources &, const fs::path &,
                                    const Progress &)>;

struct DLCSource {
  fs::path path;
  std::string name;
};
using DLCValidator = std::function<std::string(const fs::path &)>;
// Validate the complete optional selection before preparing game files.
std::vector<DLCSource> ValidateDLC(const std::vector<fs::path> &input,
                                   const fs::path &install, const DLCValidator &validate);

Sources FindDiscs(const fs::path &input, const Inspector &inspect);
// Explicit slots must each contain exactly the requested disc; no sibling search.
Sources ValidateDiscs(const Sources &input, const Inspector &inspect);
bool Overlaps(const fs::path &a, const fs::path &b);
bool Owned(const fs::path &game);
fs::path PreparedGame(const fs::path &install);
void Prepare(const fs::path &input, const fs::path &install,
             const Inspector &inspect, const Extractor &extract,
             const Progress &progress);
void Prepare(const Sources &input, const fs::path &install,
             const Inspector &inspect, const Extractor &extract,
             const Progress &progress);
} // namespace bd::launcher
