/**
 * @file    engine/settings.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "engine/settings.h"

#include <algorithm>

#include <rex/cvar.h>

#include "core/settings.h" // kCvarGroup
#include "engine/glyph_set.h"

REXCVAR_DECLARE(i32, bd_fps_limit);
REXCVAR_DECLARE(bool, bd_save_anywhere);
REXCVAR_DECLARE(bool, bd_disable_tutorials);
REXCVAR_DECLARE(bool, bd_map_gimmick_markers);
REXCVAR_DECLARE(i32, bd_hud_mode);
REXCVAR_DECLARE(f64, bd_hud_fade_delay);
REXCVAR_DECLARE(bool, bd_mouse_menu);
REXCVAR_DECLARE(bool, bd_mouse_cursor_sfx);
REXCVAR_DECLARE(i32, bd_mouse_cursor_opacity);
REXCVAR_DECLARE(i32, bd_glyph_set);
REXCVAR_DECLARE(bool, bd_vibration);

REXCVAR_DEFINE_INT32(bd_fps_limit, 0, kCvarGroup,
                     "Frame-rate cap: 0 = unlimited, above 30 the fixed 30Hz "
                     "simulation is interpolated.");

REXCVAR_DEFINE_BOOL(bd_save_anywhere, false, kCvarGroup,
                    "Keep the camp menu's Save entry usable away from save "
                    "points. May cause softlocks and other oddities.");

REXCVAR_DEFINE_BOOL(bd_disable_tutorials, false, kCvarGroup,
                    "Skip the tutorial pages. The story letters, the warp map "
                    "and the loading animation still play.");

REXCVAR_DEFINE_BOOL(bd_map_gimmick_markers, false, kCvarGroup,
                    "Mark the area map with the search points, chests and "
                    "barriers the current map still has untouched.");

REXCVAR_DEFINE_INT32(bd_hud_mode, 0, kCvarGroup,
                     "Field HUD: 0 always on, 1 fades out while idle and "
                     "returns on any face or shoulder button, 2 never drawn.");

REXCVAR_DEFINE_DOUBLE(bd_hud_fade_delay, 5.0, kCvarGroup,
                      "Seconds of no button before the idle field HUD starts "
                      "fading.");

REXCVAR_DEFINE_BOOL(bd_mouse_menu, true, kCvarGroup,
                    "Hovering a menu row moves the game's own cursor onto "
                    "it.");

REXCVAR_DEFINE_BOOL(bd_mouse_cursor_sfx, true, kCvarGroup,
                    "Play the cursor-move sound effect on a mouse-driven "
                    "row change.");

REXCVAR_DEFINE_INT32(bd_mouse_cursor_opacity, 80, kCvarGroup,
                     "Opacity of the drawn mouse pointer, in percent.");

REXCVAR_DEFINE_INT32(bd_glyph_set, 0, kCvarGroup,
                     "Button prompt glyphs: 0 = follow the input device, "
                     "1 = controller, 2 = keyboard.");

REXCVAR_DEFINE_INT32(bd_glyph_pad, -1, kCvarGroup,
                     "Controller the prompt glyphs draw: -1 = follow the "
                     "connected pad, 0 = Xbox 360, 1 = Xbox, 2 = PlayStation, "
                     "3 = Switch, 4 = Steam Deck.");

REXCVAR_DEFINE_BOOL(bd_vibration, true, kCvarGroup,
                    "Pad rumble. Off stops the motors without touching any "
                    "other pad input.");

namespace bd::engine {
namespace {

constexpr i32 kHudModeLast = static_cast<i32>(engine::HudMode::Hidden);

} // namespace

Settings &Settings::Get() {
  static Settings s;
  return s;
}

void Settings::AdoptFPSLimit() { fpsLimit_ = REXCVAR_GET(bd_fps_limit); }

void Settings::AdoptSaveAnywhere() {
  saveAnywhere_ = REXCVAR_GET(bd_save_anywhere);
}

void Settings::AdoptDisableTutorials() {
  disableTutorials_ = REXCVAR_GET(bd_disable_tutorials);
}

void Settings::AdoptMapGimmickMarkers() {
  mapGimmickMarkers_ = REXCVAR_GET(bd_map_gimmick_markers);
}

void Settings::AdoptHudMode() {
  const i32 v = REXCVAR_GET(bd_hud_mode);
  hudMode_ = v >= 0 && v <= kHudModeLast ? static_cast<engine::HudMode>(v)
                                         : engine::HudMode::Always;
}

void Settings::AdoptHudFadeDelay() {
  hudFadeDelay_ = std::max(0.0, REXCVAR_GET(bd_hud_fade_delay));
}

void Settings::AdoptMouseMenu() { mouseMenu_ = REXCVAR_GET(bd_mouse_menu); }

void Settings::AdoptMouseCursorSFX() {
  mouseCursorSFX_ = REXCVAR_GET(bd_mouse_cursor_sfx);
}

void Settings::AdoptGlyphSetMode() {
  glyphSetMode_ = std::clamp(REXCVAR_GET(bd_glyph_set), 0,
                             static_cast<i32>(GlyphSet::Keyboard));
}

void Settings::AdoptPadGlyphSet() {
  padGlyphSet_ =
      std::clamp(REXCVAR_GET(bd_glyph_pad), kPadSetFirst, kPadSetLast);
}

void Settings::AdoptVibration() { vibration_ = REXCVAR_GET(bd_vibration); }

void Settings::AdoptMouseCursorOpacity() {
  mouseCursorOpacity_ =
      std::clamp(REXCVAR_GET(bd_mouse_cursor_opacity), kMouseCursorOpacityMin,
                 kMouseCursorOpacityMax);
}

bool Settings::SetFPSLimit(i32 v) {
  return rex::cvar::SetFlagByName("bd_fps_limit", FormatCvar(v));
}

bool Settings::SetSaveAnywhere(bool v) {
  return rex::cvar::SetFlagByName("bd_save_anywhere", FormatCvar(v));
}

bool Settings::SetDisableTutorials(bool v) {
  return rex::cvar::SetFlagByName("bd_disable_tutorials", FormatCvar(v));
}

bool Settings::SetMapGimmickMarkers(bool v) {
  return rex::cvar::SetFlagByName("bd_map_gimmick_markers", FormatCvar(v));
}

bool Settings::SetHudMode(i32 v) {
  return rex::cvar::SetFlagByName("bd_hud_mode", FormatCvar(v));
}

bool Settings::SetHudFadeDelay(f64 v) {
  return rex::cvar::SetFlagByName("bd_hud_fade_delay", FormatCvar(v));
}

bool Settings::SetGlyphSetMode(i32 v) {
  return rex::cvar::SetFlagByName("bd_glyph_set", FormatCvar(v));
}

bool Settings::SetPadGlyphSet(i32 v) {
  return rex::cvar::SetFlagByName("bd_glyph_pad", FormatCvar(v));
}

bool Settings::SetMouseCursorOpacity(i32 v) {
  return rex::cvar::SetFlagByName("bd_mouse_cursor_opacity", FormatCvar(v));
}

bool Settings::SetVibration(bool v) {
  return rex::cvar::SetFlagByName("bd_vibration", FormatCvar(v));
}

void Settings::AdoptCvars() {
  AdoptFPSLimit();
  AdoptSaveAnywhere();
  AdoptDisableTutorials();
  AdoptMapGimmickMarkers();
  AdoptHudMode();
  AdoptHudFadeDelay();
  AdoptMouseMenu();
  AdoptMouseCursorSFX();
  AdoptMouseCursorOpacity();
  AdoptGlyphSetMode();
  AdoptPadGlyphSet();
  AdoptVibration();
}

void Settings::Init() {
  AdoptCvars();

  auto reg = [](const char *name, void (Settings::*adopt)()) {
    rex::cvar::RegisterChangeCallback(
        name, [adopt](std::string_view, std::string_view) {
          (Settings::Get().*adopt)();
        });
  };
  reg("bd_fps_limit", &Settings::AdoptFPSLimit);
  reg("bd_save_anywhere", &Settings::AdoptSaveAnywhere);
  reg("bd_disable_tutorials", &Settings::AdoptDisableTutorials);
  reg("bd_map_gimmick_markers", &Settings::AdoptMapGimmickMarkers);
  reg("bd_hud_mode", &Settings::AdoptHudMode);
  reg("bd_hud_fade_delay", &Settings::AdoptHudFadeDelay);
  reg("bd_mouse_menu", &Settings::AdoptMouseMenu);
  reg("bd_mouse_cursor_sfx", &Settings::AdoptMouseCursorSFX);
  reg("bd_mouse_cursor_opacity", &Settings::AdoptMouseCursorOpacity);
  reg("bd_glyph_set", &Settings::AdoptGlyphSetMode);
  reg("bd_glyph_pad", &Settings::AdoptPadGlyphSet);
  reg("bd_vibration", &Settings::AdoptVibration);
}

} // namespace bd::engine
