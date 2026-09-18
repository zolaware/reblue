/**
 * @file    installer/korean_import.h
 * @brief   Optional Korean retail data import for an NTSC-U re:Blue install.
 *
 * The static recompilation remains based on the NTSC-U executable.  This
 * importer copies only Korean language/audio assets from discs owned by the
 * user and converts the regional two-audio-stream Sofdec movies to the
 * three-stream topology expected by the NTSC-U runtime.
 */
#pragma once

#include <array>
#include <filesystem>

#include "installer/install_registry.h"

namespace bd::installer {

struct InstallProgress;

// Returns true when every source can be opened as its corresponding Blue
// Dragon disc and advertises Korean content in bd_boot.ini.
bool ValidateKoreanRetailSources(
    const std::array<std::filesystem::path, kDiscCount> &sources,
    std::string *error = nullptr);

// Imports Korean-only data into an already installed NTSC-U game_data_dest.
// The function runs on the install worker thread and updates progress in place.
// It never replaces default.xex or the base shipped pack archives.
bool ImportKoreanRetailData(
    const std::array<std::filesystem::path, kDiscCount> &sources,
    const std::filesystem::path &game_data_dest, InstallProgress &progress);

// Returns the 1-based KR entry in [Voice] from an installed bd_boot.ini, or 0.
int KoreanVoiceIndex(const std::filesystem::path &game_data_dest);

} // namespace bd::installer
