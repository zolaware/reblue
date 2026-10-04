/**
 * @file    installer/install_registry.h
 * @brief   Persists the install record through one of three connectors.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>

#include <rex/types.h>

namespace bd::installer {

// Bump when an older record can no longer be brought forward in memory.
// Read migrates what it can and stamps this on success, so a caller that sees
// anything else is holding a record it cannot use.
constexpr int kInstallSchemaVersion = 3;

inline constexpr int kDiscCount = 3;

#if defined(_WIN32)
inline constexpr const char *kGameExecutable = "reblue.exe";
#endif

inline constexpr const char *kPortableRecordName = ".reblue_install";

enum class InstallConnector { kPortableFile, kRegistry, kConfigFile };

#if defined(_WIN32)
inline constexpr InstallConnector kPlatformConnector =
    InstallConnector::kRegistry;
#else
inline constexpr InstallConnector kPlatformConnector =
    InstallConnector::kConfigFile;
#endif

const char *ToString(InstallConnector connector);

struct InstallConfig {
  std::filesystem::path install_root;
  std::array<std::string, kDiscCount> iso_fingerprints;
  int schema_version = 0;
  std::string app_version;
  InstallConnector connector = kPlatformConnector;

  std::filesystem::path game_data_path() const { return install_root / "game"; }
  std::filesystem::path user_data_path() const { return install_root / "user"; }
  bool Portable() const {
    return connector == InstallConnector::kPortableFile;
  }

  // Probes the portable file beside the exe, then the platform store. Nullopt
  // when neither holds a record whose install_root/game/default.xex exists.
  static std::optional<InstallConfig> Read();

  // Writes through the record's own connector, stamping this build's version.
  bool Write() const;

  // True on success or if absent.
  static bool Clear(InstallConnector connector);
};

} // namespace bd::installer
