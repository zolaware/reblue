/**
 * @file    engine/settings.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "engine/settings.h"

#include <string>
#include <string_view>

#include "core/cvar_table.h"
#include "engine/config.h"

namespace bd::engine {
namespace {

bd::SettingWatches g_callbacks;

} // namespace

HudMode GetHudMode() {
  return static_cast<HudMode>(bd::cvars::Get().hud_mode.Get());
}

i32 GetFPSLimit() {
  return static_cast<i32>(bd::CvarDouble("gpu.frame_limit", 0.0));
}

bool SetFPSLimit(i32 limit) {
  return bd::CvarSet("gpu.frame_limit", std::to_string(limit));
}

bool IsVibrationEnabled() {
  const std::string value = bd::CvarGet("hid.vibration");
  return !(value == "false" || value == "0");
}

bool SetVibration(bool on) {
  return bd::CvarSet("hid.vibration", on ? "true" : "false");
}

void ApplyCameraSpeed() {
  Config::Get().SetCamRollSpd(
      static_cast<f32>(bd::cvars::Get().camera_speed.Get()));
}

void WatchSettings() {
  g_callbacks.push_back(bd::cvars::Get().camera_speed.OnChange(
      [](const f64 &) { ApplyCameraSpeed(); }));
  ApplyCameraSpeed();
}

} // namespace bd::engine
