// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace bd::launcher {
namespace fs = std::filesystem;
// Marks game/ as made by this setup. Its first line is kContentVersion.
inline constexpr const char *kOwner = ".reblue-content";
// The version of what setup extracts into game/. Bump it whenever the files or
// layout that extraction produces change, so existing installs count as not set
// up and are extracted again. Keep it apart from the release version: every
// release reruns setup, and an unchanged build must be kept.
inline constexpr const char *kContentVersion = "reblue-content-v1";
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

// Each slot must contain exactly the requested disc; no sibling search.
Sources ValidateDiscs(const Sources &input, const Inspector &inspect);
bool Overlaps(const fs::path &a, const fs::path &b);
// True when game/ was made by this setup, at any content version.
bool Owned(const fs::path &game);
// True when game/ was made by this setup at the current content version.
bool Current(const fs::path &game);
fs::path PreparedGame(const fs::path &install);
// True when a completed, managed game folder of the current content version is
// already published.
bool IsPrepared(const fs::path &install);
// Finish what a stopped setup left behind: put the previous game back if it was
// moved aside and nothing replaced it, then remove the leftover transaction.
// Throws when another setup is running for this installation, or the leftover
// cannot be removed. Prepare does this itself.
void RecoverSetup(const fs::path &install);
void Prepare(const Sources &input, const fs::path &install,
             const Inspector &inspect, const Extractor &extract,
             const Progress &progress);
} // namespace bd::launcher
