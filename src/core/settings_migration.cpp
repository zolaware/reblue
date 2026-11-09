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

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <toml++/toml.hpp>

#include <rex/settings_registry.h>
#include <rex/types.h>

#include "core/cvar_table.h"
#include "core/logging.h"
#include "gpu/gpu.h"

namespace bd {
namespace {

constexpr i32 kConfigVersion = 4;

struct Step {
  i32 version;
  const char *what;
  void (*Apply)();
};

bool ConfigWasLoaded() {
  for (const rex::SettingBase *setting : rex::GetSettingsRegistry().Enumerate()) {
    if (setting->GetLayer() == rex::SettingLayer::kFile)
      return true;
  }
  return false;
}

void SplitWindowSizeFromResolution() {
  if (CvarIsSet("ui.resolution") || !CvarIsSet("ui.window_width"))
    return;
  const i32 w = CvarInt("ui.window_width", 0);
  const i32 h = CvarInt("ui.window_height", 0);
  if (w <= 0 || h <= 0)
    return;
  CvarSet("ui.resolution", std::to_string(w) + "x" + std::to_string(h));
}

void PreserveUngatedPostAndReflections() {
  CvarSet("bd.post_quality",
          std::to_string(static_cast<i32>(gpu::PostQuality::High)));
  CvarSet("bd.reflection_quality",
          std::to_string(static_cast<i32>(gpu::ReflectionQuality::High)));
}

// Version 3 moved every key into a [scope] table. RewriteFile carries the file
// itself, so no registry step exists for it.
constexpr Step kSteps[] = {
    {1, "window size split from render resolution",
     SplitWindowSizeFromResolution},
    {2, "post and reflection cost held at what it was",
     PreserveUngatedPostAndReflections}};

// Flat names the SDK has no alias for and the scoped name each became. An empty
// new name is a setting no build reads any more. The SDK alias table covers the
// rest, so a flat key it knows stays flat for the loader to resolve.
constexpr std::pair<std::string_view, std::string_view> kSdkRenames[] = {
    {"max_file_size_bytes", ""},
    {"progress_log_frequency", ""},
    {"max_discovery_iterations", ""},
    {"max_vtable_iterations", ""},
    {"max_resolve_iterations", ""},
    {"max_eh_states", ""},
    {"max_eh_try_blocks", ""},
    {"max_eh_ip_map_entries", ""},
    {"max_seh_scope_entries", ""},
    {"backward_scan_limit", ""},
    {"max_jump_table_entries", ""},
    {"max_blocks_per_function", ""},
    {"clock_source_raw", ""},
    {"log_verbose", ""},
    {"log_noisy", ""},
    {"perf_log_csv", ""},
    {"dump_source", ""},
    {"dump_path", ""},
    {"allow_game_relative_writes", ""},
    {"input_backend", ""},
    {"hid_mappings_file", ""},
    {"rexcrt_heap_enable", ""},
    {"rexcrt_heap_size_mb", ""},
    {"license_mask", ""},
    {"user_country", ""},
    {"headless", ""},
    {"user_language", ""},
    {"kernel_pix", ""},
    {"log_high_frequency_kernel_calls", ""},
    {"cl", ""},
    {"kernel_debug_monitor", ""},
    {"kernel_cert_monitor", ""},
    {"game_data_root", ""},
    {"user_data_root", ""},
    {"update_data_root", ""},
    {"cache_root", ""},
    {"metadata_root", ""},
    {"echo", "console.echo"},
    {"find", "console.find"},
    {"video_driver", ""},
    {"bd_cursor_hide_seconds", "ui.cursor_timeout"},
};

constexpr std::string_view kReblueBareKeys[] = {"profile", "config_version",
                                                "no_installer"};

// The scoped spelling of a flat key, or empty when the key is dropped.
std::string ScopedName(std::string_view key) {
  if (key.find('.') != std::string_view::npos)
    return std::string(key);
  for (const auto &[old_name, new_name] : kSdkRenames) {
    if (old_name == key)
      return std::string(new_name);
  }
  if (key.starts_with("bd_"))
    return "bd." + std::string(key.substr(3));
  if (key.starts_with("bind_"))
    return "bind." + std::string(key.substr(5));
  for (std::string_view bare : kReblueBareKeys) {
    if (bare == key)
      return "bd." + std::string(key);
  }
  return {};
}

} // namespace

void SettingsMigration::RewriteFile(const std::filesystem::path &file) {
  std::error_code ec;
  if (file.empty() || !std::filesystem::exists(file, ec))
    return;

  toml::table parsed;
  try {
    parsed = toml::parse_file(file.string());
  } catch (const toml::parse_error &error) {
    REXBD_WARN("config {} not rewritten: {}", file.string(),
            error.description());
    return;
  }

  toml::table out;
  std::vector<std::string> dropped;
  bool moved = false;
  for (auto &&[key, node] : parsed) {
    if (const toml::table *table = node.as_table()) {
      toml::table copy = *table;
      if (key.str() == "bd") {
        if (const toml::node *hide = copy.get("cursor_hide_seconds")) {
          toml::table *ui = out["ui"].as_table();
          if (!ui) {
            out.insert("ui", toml::table{});
            ui = out["ui"].as_table();
          }
          hide->visit(
              [&](auto &value) { ui->insert_or_assign("cursor_timeout", value); });
          copy.erase("cursor_hide_seconds");
          moved = true;
        }
      }
      if (toml::table *existing = out[key.str()].as_table()) {
        for (auto &&[leaf, value] : copy)
          existing->insert_or_assign(leaf, value);
      } else {
        out.insert(key.str(), std::move(copy));
      }
      continue;
    }
    const std::string name = ScopedName(key.str());
    if (name.empty()) {
      if (rex::GetSettingsRegistry().IsAlias(key.str())) {
        node.visit([&](auto &value) {
          out.insert_or_assign(std::string(key.str()), value);
        });
        continue;
      }
      dropped.emplace_back(key.str());
      continue;
    }
    const auto dot = name.find('.');
    const std::string scope = name.substr(0, dot);
    const std::string leaf = name.substr(dot + 1);
    toml::table *target = out[scope].as_table();
    if (!target) {
      out.insert(scope, toml::table{});
      target = out[scope].as_table();
    }
    node.visit([&](auto &value) { target->insert_or_assign(leaf, value); });
    moved = true;
  }
  if (!moved && dropped.empty())
    return;

  std::ofstream os(file, std::ios::binary | std::ios::trunc);
  os << out;
  if (!os) {
    REXBD_WARN("config {} could not be rewritten", file.string());
    return;
  }
  REXBD_INFO("config {} rewritten with scoped keys", file.string());
  for (const std::string &key : dropped)
    REXBD_INFO("config dropped '{}', no longer a setting", key);
}

void SettingsMigration::Apply() {
  const i32 from = bd::cvars::Get().config_version.Get();
  if (from >= kConfigVersion)
    return;
  if (ConfigWasLoaded()) {
    for (const Step &step : kSteps) {
      if (step.version <= from)
        continue;
      step.Apply();
      REXBD_INFO("config migrated to v{}: {}", step.version, step.what);
    }
  }
  CvarSet("bd.config_version", std::to_string(kConfigVersion));
}

} // namespace bd
