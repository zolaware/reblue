/**
 * @file    core/settings_migration.h
 * @brief   Carries a config written by an older build forward to what this
 *          one reads.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <filesystem>

namespace bd {

class SettingsMigration {
public:
  // Rewrites a flat pre-3 config file into [scope] tables before rex::Init
  // reads it. Keys already inside a table pass through, keys no build reads
  // any more are dropped with a log line each.
  static void RewriteFile(const std::filesystem::path &file);

  // The value steps, run against the registry once rex::Init has applied the
  // file.
  static void Apply();
};

} // namespace bd
