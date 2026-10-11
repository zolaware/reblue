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

namespace bd {

class SettingsMigration {
public:
  static void Apply();
};

} // namespace bd
