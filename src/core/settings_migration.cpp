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

#include <rex/cvar.h>
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
  for (const rex::cvar::VarBase *var : Cvars().Enumerate()) {
    if (Cvars().GetSource(var->GetDesc().name) == rex::cvar::CvarSource::kConfig)
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

// SDK names before sub-project 5 of the ReXGlue 1.0 program and the scoped
// name each became. An empty new name is a setting no build reads any more.
constexpr std::pair<std::string_view, std::string_view> kSdkRenames[] = {
    {"audio_maxqframes", "apu.max_queued_frames"},
    {"audio_mute", "apu.mute"},
    {"ffmpeg_verbose", "runtime.ffmpeg_verbose"},
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
    {"clock_no_scaling", "clock.no_scaling"},
    {"clock_source_raw", ""},
    {"log_level", "log.level"},
    {"log_file", "log.file"},
    {"log_verbose", "log.verbose"},
    {"log_noisy", "log.noisy"},
    {"log_flush_interval", "log.flush_interval"},
    {"perf_log_csv", ""},
    {"dump_source", ""},
    {"dump_path", ""},
    {"allow_game_relative_writes", ""},
    {"input_backend", ""},
    {"guide_button", "hid.guide_button"},
    {"vibration", "hid.vibration"},
    {"left_stick_deadzone_percentage", "hid.left_stick_deadzone"},
    {"right_stick_deadzone_percentage", "hid.right_stick_deadzone"},
    {"mnk_mode", "hid.mnk_mode"},
    {"mnk_mouse", "hid.mnk_mouse"},
    {"mnk_sensitivity", "hid.mnk_sensitivity"},
    {"mnk_passthrough", "hid.mnk_passthrough"},
    {"keybind_a", "hid.keybind_a"},
    {"keybind_b", "hid.keybind_b"},
    {"keybind_x", "hid.keybind_x"},
    {"keybind_y", "hid.keybind_y"},
    {"keybind_left_trigger", "hid.keybind_left_trigger"},
    {"keybind_right_trigger", "hid.keybind_right_trigger"},
    {"keybind_left_shoulder", "hid.keybind_left_shoulder"},
    {"keybind_right_shoulder", "hid.keybind_right_shoulder"},
    {"keybind_lstick_up", "hid.keybind_lstick_up"},
    {"keybind_lstick_down", "hid.keybind_lstick_down"},
    {"keybind_lstick_left", "hid.keybind_lstick_left"},
    {"keybind_lstick_right", "hid.keybind_lstick_right"},
    {"keybind_lstick_press", "hid.keybind_lstick_press"},
    {"keybind_rstick_up", "hid.keybind_rstick_up"},
    {"keybind_rstick_down", "hid.keybind_rstick_down"},
    {"keybind_rstick_left", "hid.keybind_rstick_left"},
    {"keybind_rstick_right", "hid.keybind_rstick_right"},
    {"keybind_rstick_press", "hid.keybind_rstick_press"},
    {"keybind_dpad_up", "hid.keybind_dpad_up"},
    {"keybind_dpad_down", "hid.keybind_dpad_down"},
    {"keybind_dpad_left", "hid.keybind_dpad_left"},
    {"keybind_dpad_right", "hid.keybind_dpad_right"},
    {"keybind_back", "hid.keybind_back"},
    {"keybind_start", "hid.keybind_start"},
    {"keybind_guide", "hid.keybind_guide"},
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
    {"writable_code_segments", "runtime.writable_code_segments"},
    {"protect_zero", "runtime.protect_zero"},
    {"protect_on_release", "runtime.protect_on_release"},
    {"scribble_heap", "runtime.scribble_heap"},
    {"ignore_thread_priorities", "runtime.ignore_thread_priorities"},
    {"ignore_thread_affinities", "runtime.ignore_thread_affinities"},
    {"echo", "console.echo"},
    {"find", "console.find"},
    {"window_width", "ui.window_width"},
    {"window_height", "ui.window_height"},
    {"fullscreen", "ui.fullscreen"},
    {"fullscreen_exclusive", "ui.fullscreen_exclusive"},
    {"monitor", "ui.monitor"},
    {"video_driver", ""},
    {"video_mode_width", "ui.video_mode_width"},
    {"video_mode_height", "ui.video_mode_height"},
    {"resolution", "ui.resolution"},
    {"video_mode_refresh_rate", "ui.video_mode_refresh_rate"},
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
    REXLOG_WARN(bd, "config {} not rewritten: {}", file.string(),
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
    REXLOG_WARN(bd, "config {} could not be rewritten", file.string());
    return;
  }
  REXLOG_INFO(bd, "config {} rewritten with scoped keys", file.string());
  for (const std::string &key : dropped)
    REXLOG_INFO(bd, "config dropped '{}', no longer a setting", key);
}

void SettingsMigration::Apply() {
  const i32 from = REXCVAR_GET(bd::cvars::config_version);
  if (from >= kConfigVersion)
    return;
  if (ConfigWasLoaded()) {
    for (const Step &step : kSteps) {
      if (step.version <= from)
        continue;
      step.Apply();
      REXLOG_INFO(bd, "config migrated to v{}: {}", step.version, step.what);
    }
  }
  CvarSet("bd.config_version", std::to_string(kConfigVersion));
}

} // namespace bd
