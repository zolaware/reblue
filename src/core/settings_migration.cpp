/**
 * @file    core/settings_migration.cpp
 * @brief   The config steps, in version order, and the stamp recording which
 *          of them a config has already been through.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "core/settings_migration.h"

#include <string>

#include <rex/cvar.h>
#include <rex/types.h>
#include <rex/ui/flags.h>

#include "core/logging.h"
#include "gpu/gpu.h"

REXCVAR_DEFINE_INT32(config_version, 0, "reblue",
                     "Config schema version this file was last written by")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace bd {
namespace {

constexpr i32 kConfigVersion = 2;

struct Step {
  i32 version;
  const char *what;
  void (*Apply)();
};

bool ConfigWasLoaded() {
  for (const rex::cvar::FlagEntry &flag : rex::cvar::GetRegistry()) {
    if (flag.source == rex::cvar::Source::kConfig)
      return true;
  }
  return false;
}

void SplitWindowSizeFromResolution() {
  if (rex::cvar::HasNonDefaultValue("resolution") ||
      !rex::cvar::HasNonDefaultValue("window_width"))
    return;
  const i32 w = REXCVAR_GET(window_width);
  const i32 h = REXCVAR_GET(window_height);
  if (w <= 0 || h <= 0)
    return;
  rex::cvar::SetFlagByName("resolution",
                           std::to_string(w) + "x" + std::to_string(h));
}

void PreserveUngatedPostAndReflections() {
  rex::cvar::SetFlagByName(
      "bd_post_quality",
      std::to_string(static_cast<i32>(gpu::PostQuality::High)));
  rex::cvar::SetFlagByName(
      "bd_reflection_quality",
      std::to_string(static_cast<i32>(gpu::ReflectionQuality::High)));
}

constexpr Step kSteps[] = {
    {1, "window size split from render resolution",
     SplitWindowSizeFromResolution},
    {2, "post and reflection cost held at what it was",
     PreserveUngatedPostAndReflections}};

} // namespace

void SettingsMigration::Apply() {
  const i32 from = REXCVAR_GET(config_version);
  if (from >= kConfigVersion)
    return;
  if (ConfigWasLoaded()) {
    for (const Step &step : kSteps) {
      if (step.version <= from)
        continue;
      step.Apply();
      BD_INFO("[config] migrated to v{}: {}", step.version, step.what);
    }
  }
  rex::cvar::SetFlagByName("config_version", std::to_string(kConfigVersion));
}

} // namespace bd
