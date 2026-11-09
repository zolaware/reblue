/**
 * @file    engine/settings.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "engine/settings.h"

#include <algorithm>

#include <vector>

#include <rex/cvar.h>

#include "core/cvar_table.h"
#include "core/settings.h"
#include "engine/config.h"
#include "engine/glyph_set.h"

namespace bd::engine {
namespace {

std::vector<rex::cvar::CallbackHandle> g_callbacks;

constexpr i32 kHudModeLast = static_cast<i32>(engine::HudMode::Hidden);

} // namespace

Settings &Settings::Get() {
  static Settings s;
  return s;
}

void Settings::AdoptFPSLimit() { fpsLimit_ = REXCVAR_GET(bd::cvars::fps_limit); }

void Settings::AdoptSaveAnywhere() {
  saveAnywhere_ = REXCVAR_GET(bd::cvars::save_anywhere);
}

void Settings::AdoptDisableTutorials() {
  disableTutorials_ = REXCVAR_GET(bd::cvars::disable_tutorials);
}

void Settings::AdoptMapGimmickMarkers() {
  mapGimmickMarkers_ = REXCVAR_GET(bd::cvars::map_gimmick_markers);
}

void Settings::AdoptHudMode() {
  const i32 v = REXCVAR_GET(bd::cvars::hud_mode);
  hudMode_ = v >= 0 && v <= kHudModeLast ? static_cast<engine::HudMode>(v)
                                         : engine::HudMode::Always;
}

void Settings::AdoptHudFadeDelay() {
  hudFadeDelay_ = std::max(0.0, REXCVAR_GET(bd::cvars::hud_fade_delay));
}

void Settings::AdoptMouseInput() {
  mouseInput_ = REXCVAR_GET(bd::cvars::mouse_input);
}

void Settings::AdoptMouseCursorSFX() {
  mouseCursorSFX_ = REXCVAR_GET(bd::cvars::mouse_cursor_sfx);
}

void Settings::AdoptGlyphSetMode() {
  glyphSetMode_ = std::clamp(REXCVAR_GET(bd::cvars::glyph_set), 0,
                             static_cast<i32>(GlyphSet::Keyboard));
}

void Settings::AdoptPadGlyphSet() {
  padGlyphSet_ =
      std::clamp(REXCVAR_GET(bd::cvars::glyph_pad), kPadSetFirst, kPadSetLast);
}

void Settings::AdoptVibration() { vibration_ = REXCVAR_GET(bd::cvars::vibration); }

void Settings::AdoptCameraSpeed() {
  cameraSpeed_ = std::clamp(REXCVAR_GET(bd::cvars::camera_speed), kCameraSpeedMin,
                            kCameraSpeedMax);
  ApplyCameraSpeed();
}

void Settings::ApplyCameraSpeed() const {
  Config::Get().SetCamRollSpd(static_cast<f32>(cameraSpeed_));
}

void Settings::AdoptMouseCursorOpacity() {
  mouseCursorOpacity_ =
      std::clamp(REXCVAR_GET(bd::cvars::mouse_cursor_opacity), kMouseCursorOpacityMin,
                 kMouseCursorOpacityMax);
}

bool Settings::SetFPSLimit(i32 v) {
  return bd::CvarSet("bd.fps_limit", FormatCvar(v));
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
  return bd::CvarSet("bd.vibration", FormatCvar(v));
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
  reg("bd.fps_limit", &Settings::AdoptFPSLimit);
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
  reg("bd.vibration", &Settings::AdoptVibration);
  reg("bd.camera_speed", &Settings::AdoptCameraSpeed);
}

} // namespace bd::engine
