/**
 * @file    engine/settings.h
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include <rex/types.h>

namespace bd::engine {

enum class HudMode : i32 {
  Always = 0,
  AutoHide = 1,
  Hidden = 2,
};

inline constexpr i32 kMouseCursorOpacityMin = 20;
inline constexpr i32 kMouseCursorOpacityMax = 100;

inline constexpr f64 kCameraSpeedMin = 0.5;
inline constexpr f64 kCameraSpeedMax = 5.0;

HudMode GetHudMode();

i32 GetFPSLimit();
bool SetFPSLimit(i32 limit);

bool IsVibrationEnabled();
bool SetVibration(bool on);

void ApplyCameraSpeed();

void WatchSettings();

} // namespace bd::engine
