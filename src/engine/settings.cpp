/**
 * @file    engine/settings.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "engine/settings.h"

#include <algorithm>

#include <vector>


#include "core/cvar_table.h"
#include "core/settings.h"
#include "engine/config.h"
#include "engine/glyph_set.h"

namespace bd::engine {
namespace {

bd::SettingWatches g_callbacks;

constexpr i32 kHudModeLast = static_cast<i32>(engine::HudMode::Hidden);

} // namespace

Settings &Settings::Get() {
  static Settings s;
  return s;
}

void Settings::AdoptFPSLimit() { fpsLimit_ = static_cast<i32>(bd::CvarDouble("gpu.frame_limit", 0.0)); }

void Settings::AdoptSaveAnywhere() {
  saveAnywhere_ = bd::cvars::Get().save_anywhere.Get();
}

void Settings::AdoptDisableTutorials() {
  disableTutorials_ = bd::cvars::Get().disable_tutorials.Get();
}

void Settings::AdoptMapGimmickMarkers() {
  mapGimmickMarkers_ = bd::cvars::Get().map_gimmick_markers.Get();
}

void Settings::AdoptHudMode() {
  const i32 v = bd::cvars::Get().hud_mode.Get();
  hudMode_ = v >= 0 && v <= kHudModeLast ? static_cast<engine::HudMode>(v)
                                         : engine::HudMode::Always;
}

void Settings::AdoptHudFadeDelay() {
  hudFadeDelay_ = std::max(0.0, bd::cvars::Get().hud_fade_delay.Get());
}

void Settings::AdoptMouseInput() {
  mouseInput_ = bd::cvars::Get().mouse_input.Get();
}

void Settings::AdoptMouseCursorSFX() {
  mouseCursorSFX_ = bd::cvars::Get().mouse_cursor_sfx.Get();
}

void Settings::AdoptGlyphSetMode() {
  glyphSetMode_ = std::clamp(bd::cvars::Get().glyph_set.Get(), 0,
                             static_cast<i32>(GlyphSet::Keyboard));
}

void Settings::AdoptPadGlyphSet() {
  padGlyphSet_ =
      std::clamp(bd::cvars::Get().glyph_pad.Get(), kPadSetFirst, kPadSetLast);
}

void Settings::AdoptVibration() {
  const std::string v = bd::CvarGet("hid.vibration");
  vibration_ = !(v == "false" || v == "0");
}

void Settings::AdoptCameraSpeed() {
  cameraSpeed_ = std::clamp(bd::cvars::Get().camera_speed.Get(), kCameraSpeedMin,
                            kCameraSpeedMax);
  ApplyCameraSpeed();
}

void Settings::ApplyCameraSpeed() const {
  Config::Get().SetCamRollSpd(static_cast<f32>(cameraSpeed_));
}

void Settings::AdoptMouseCursorOpacity() {
  mouseCursorOpacity_ =
      std::clamp(bd::cvars::Get().mouse_cursor_opacity.Get(), kMouseCursorOpacityMin,
                 kMouseCursorOpacityMax);
}

bool Settings::SetFPSLimit(i32 v) {
  return bd::CvarSet("gpu.frame_limit", FormatCvar(v));
}

bool Settings::SetSaveAnywhere(bool v) {
  return bd::CvarSet("bd.save_anywhere", FormatCvar(v));
}

bool Settings::SetDisableTutorials(bool v) {
  return bd::CvarSet("bd.disable_tutorials", FormatCvar(v));
}

bool Settings::SetMapGimmickMarkers(bool v) {
  return bd::CvarSet("bd.map_gimmick_markers", FormatCvar(v));
}

bool Settings::SetHudMode(i32 v) {
  return bd::CvarSet("bd.hud_mode", FormatCvar(v));
}

bool Settings::SetHudFadeDelay(f64 v) {
  return bd::CvarSet("bd.hud_fade_delay", FormatCvar(v));
}

bool Settings::SetGlyphSetMode(i32 v) {
  return bd::CvarSet("bd.glyph_set", FormatCvar(v));
}

bool Settings::SetPadGlyphSet(i32 v) {
  return bd::CvarSet("bd.glyph_pad", FormatCvar(v));
}

bool Settings::SetMouseCursorOpacity(i32 v) {
  return bd::CvarSet("bd.mouse_cursor_opacity", FormatCvar(v));
}

bool Settings::SetMouseInput(bool v) {
  return bd::CvarSet("bd.mouse_input", FormatCvar(v));
}

bool Settings::SetVibration(bool v) {
  return bd::CvarSet("hid.vibration", FormatCvar(v));
}

bool Settings::SetCameraSpeed(f64 v) {
  return bd::CvarSet("bd.camera_speed", FormatCvar(v));
}

void Settings::AdoptCvars() {
  AdoptFPSLimit();
  AdoptSaveAnywhere();
  AdoptDisableTutorials();
  AdoptMapGimmickMarkers();
  AdoptHudMode();
  AdoptHudFadeDelay();
  AdoptMouseInput();
  AdoptMouseCursorSFX();
  AdoptMouseCursorOpacity();
  AdoptGlyphSetMode();
  AdoptPadGlyphSet();
  AdoptVibration();
  AdoptCameraSpeed();
}

void Settings::Init() {
  AdoptCvars();

  auto reg = [](const char *name, void (Settings::*adopt)()) {
    bd::CvarWatch(g_callbacks, name,
                  [adopt](std::string_view, std::string_view) {
                    (Settings::Get().*adopt)();
                  });
  };
  reg("gpu.frame_limit", &Settings::AdoptFPSLimit);
  reg("bd.save_anywhere", &Settings::AdoptSaveAnywhere);
  reg("bd.disable_tutorials", &Settings::AdoptDisableTutorials);
  reg("bd.map_gimmick_markers", &Settings::AdoptMapGimmickMarkers);
  reg("bd.hud_mode", &Settings::AdoptHudMode);
  reg("bd.hud_fade_delay", &Settings::AdoptHudFadeDelay);
  reg("bd.mouse_input", &Settings::AdoptMouseInput);
  reg("bd.mouse_cursor_sfx", &Settings::AdoptMouseCursorSFX);
  reg("bd.mouse_cursor_opacity", &Settings::AdoptMouseCursorOpacity);
  reg("bd.glyph_set", &Settings::AdoptGlyphSetMode);
  reg("bd.glyph_pad", &Settings::AdoptPadGlyphSet);
  reg("hid.vibration", &Settings::AdoptVibration);
  reg("bd.camera_speed", &Settings::AdoptCameraSpeed);
}

} // namespace bd::engine
