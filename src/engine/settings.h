/**
 * @file    engine/settings.h
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include <rex/types.h>

namespace bd::engine {

// What the field HUD does when nothing is being pressed.
enum class HudMode : i32 {
  Always = 0,   // drawn exactly as the game authored it
  AutoHide = 1, // fades out once idle, back on any face or shoulder button
  Hidden = 2,   // never drawn
};

class Settings {
public:
  static Settings &Get();

  // Adopts the current cvar values, then registers a change callback per
  // setting so console, config file and launch argument writes reach here.
  // Called once from ReblueApp::OnPostInitLogging, the first consumer hook
  // after rex::cvar::LoadConfig has run.
  void Init();

  // Re-reads every setting from cvar storage. rex::cvar::ResetToDefault and
  // ResetAllToDefaults write the storage without firing a change callback, so
  // anything that calls them calls this after.
  void AdoptCvars();

  // 0 = unlimited. Above 30 the fixed 30Hz simulation is interpolated.
  i32 FPSLimit() const { return fpsLimit_; }
  bool SetFPSLimit(i32 v);

  bool SaveAnywhere() const { return saveAnywhere_; }
  bool SetSaveAnywhere(bool v);

  // Skips the SCA tutorial pages. The story letters, the warp map and the
  // loading animation share the same opcode and are left alone.
  bool DisableTutorials() const { return disableTutorials_; }
  bool SetDisableTutorials(bool v);

  // The counts in the area map's header are not gated on this, only the
  // markers over the floor itself.
  bool MapGimmickMarkers() const { return mapGimmickMarkers_; }
  bool SetMapGimmickMarkers(bool v);

  // Hovering a menu row moves the game's own cursor onto it.
  bool MouseMenu() const { return mouseMenu_; }

  // The cursor move sound effect on a mouse-driven row change.
  bool MouseCursorSFX() const { return mouseCursorSFX_; }

  // Percent, applied to every layer of the drawn pointer. Floored well above
  // zero: the system arrow is hidden while the pointer is up, so a transparent
  // one would leave nothing to aim with.
  i32 MouseCursorOpacity() const { return mouseCursorOpacity_; }
  bool SetMouseCursorOpacity(i32 v);
  static constexpr i32 kMouseCursorOpacityMin = 20;
  static constexpr i32 kMouseCursorOpacityMax = 100;

  // Which block of the button glyph sheet every prompt draws from, as a
  // GlyphSet. Auto follows the device the player last used.
  i32 GlyphSetMode() const { return glyphSetMode_; }
  bool SetGlyphSetMode(i32 v);

  // Which controller's art the prompts wear while a pad is driving, as a
  // PadSet. The 360's is the block the disc ships.
  i32 PadGlyphSet() const { return padGlyphSet_; }
  bool SetPadGlyphSet(i32 v);

  // The compass, the dungeon minimap and the party cards. Battle is unaffected.
  engine::HudMode HudMode() const { return hudMode_; }
  bool SetHudMode(i32 v);

  // Seconds of no button before the HUD starts fading.
  f64 HudFadeDelay() const { return hudFadeDelay_; }
  bool SetHudFadeDelay(f64 v);

  bool Vibration() const { return vibration_; }
  bool SetVibration(bool v);

  f64 CameraSpeed() const { return cameraSpeed_; }
  bool SetCameraSpeed(f64 v);
  void ApplyCameraSpeed() const;
  static constexpr f64 kCameraSpeedMin = 0.5;
  static constexpr f64 kCameraSpeedMax = 5.0;
  static constexpr f64 kCameraSpeedDefault = 1.2;

private:
  Settings() = default;
  Settings(const Settings &) = delete;
  Settings &operator=(const Settings &) = delete;

  void AdoptFPSLimit();
  void AdoptSaveAnywhere();
  void AdoptDisableTutorials();
  void AdoptMapGimmickMarkers();
  void AdoptHudMode();
  void AdoptHudFadeDelay();
  void AdoptMouseMenu();
  void AdoptMouseCursorSFX();
  void AdoptMouseCursorOpacity();
  void AdoptGlyphSetMode();
  void AdoptPadGlyphSet();
  void AdoptVibration();
  void AdoptCameraSpeed();

  i32 fpsLimit_ = 0;
  i32 glyphSetMode_ = 0;
  i32 padGlyphSet_ = 0;
  bool saveAnywhere_ = false;
  bool disableTutorials_ = false;
  bool mapGimmickMarkers_ = false;
  engine::HudMode hudMode_ = engine::HudMode::Always;
  f64 hudFadeDelay_ = 5.0;
  bool mouseMenu_ = true;
  bool mouseCursorSFX_ = true;
  i32 mouseCursorOpacity_ = 80;
  bool vibration_ = true;
  f64 cameraSpeed_ = kCameraSpeedDefault;
};

} // namespace bd::engine
