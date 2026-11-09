/**
 * @file    gpu/settings.h
 * @brief   Graphics settings.
 * @license BSD 3-Clause, see LICENSE
 */
#pragma once

#include <rex/types.h>

namespace bd::gpu {

// The ratio BD's render target is fit to inside the window. Every mode but
// Original reprojects the scene to hold the vertical view while the 2D layer
// stays centered on its design canvas. Auto takes the window's own ratio, and
// Stretch fills it without reprojecting, so the picture distorts.
enum class AspectMode : i32 {
  Original = 0, // 16:9
  Standard = 1, // 4:3
  Wide = 2,     // 16:10
  Ultrawide = 3,
  SuperUltrawide = 4,
  Auto = 5,
  Stretch = 6,
};

// The field of view the game frames itself at, horizontal degrees at 16:9.
// bdCameraInit seeds every camera with 3*pi/20 of vertical view, 27 degrees,
// which is this across that ratio. bd_fov_offset moves off it, and 0 keeps it.
inline constexpr i32 kAuthoredFOVDegrees = 46;

enum class PostQuality : i32 {
  Low = 0,
  Medium = 1,
  High = 2,
};

enum class ReflectionQuality : i32 {
  Low = 0,
  Medium = 1,
  High = 2,
};

constexpr const char *ToString(PostQuality quality) {
  switch (quality) {
  case PostQuality::Low:
    return "opt.preset.low";
  case PostQuality::Medium:
    return "opt.preset.medium";
  case PostQuality::High:
    return "opt.preset.high";
  }
  return "";
}

constexpr const char *ToString(ReflectionQuality quality) {
  switch (quality) {
  case ReflectionQuality::Low:
    return "opt.preset.low";
  case ReflectionQuality::Medium:
    return "opt.preset.medium";
  case ReflectionQuality::High:
    return "opt.preset.high";
  }
  return "";
}

enum class QualityPreset : u32 {
  Low = 0,
  Medium = 1,
  High = 2,
  Ultra = 3,
  Custom = 4,
};
inline constexpr u32 kQualityPresetCount = 4; // Custom is a state, not a target

// The eight settings a preset spans, in cost order from Low. The AA path is
// the expensive one, so Low and Medium stay on multisampling and High and
// Ultra step onto supersampling on top of it. The 8192 shadow map costs real
// VRAM and fill, so it is Ultra only. Anisotropic filtering is near-free on
// modern GPUs and the menus offer it as a plain on/off, so every preset takes
// the full level.
struct PresetBundle {
  i32 superSampling;
  i32 msaa;
  i32 anisotropy;
  f64 shadowDistance;
  i32 shadowDimension;
  PostQuality postQuality;
  ReflectionQuality reflectionQuality;
};

inline constexpr PresetBundle kPresets[kQualityPresetCount] = {
    /* Low    */ {1, 0, 16, 1.0, 1024, PostQuality::Low,
                  ReflectionQuality::Low},
    /* Medium */ {1, 4, 16, 2.0, 4096, PostQuality::Medium,
                  ReflectionQuality::Medium},
    /* High   */ {2, 4, 16, 2.0, 4096, PostQuality::High,
                  ReflectionQuality::High},
    /* Ultra  */ {2, 8, 16, 4.0, 8192, PostQuality::High,
                  ReflectionQuality::High},
};

// Every cvar default below comes from this bundle, so the settings a fresh
// install boots on are a named tier rather than a set of values that happens
// to sit near one. The installer reads the same row the menu does, so the
// wizard opens on Medium instead of Custom.
inline constexpr QualityPreset kDefaultPreset = QualityPreset::Medium;
inline constexpr PresetBundle kDefaultSettings =
    kPresets[static_cast<u32>(kDefaultPreset)];

// Catalog keys, so the menu and the installer label a preset from one place.
constexpr const char *ToString(QualityPreset preset) {
  switch (preset) {
  case QualityPreset::Low:
    return "opt.preset.low";
  case QualityPreset::Medium:
    return "opt.preset.medium";
  case QualityPreset::High:
    return "opt.preset.high";
  case QualityPreset::Ultra:
    return "opt.preset.ultra";
  case QualityPreset::Custom:
    return "opt.preset.custom";
  }
  return "";
}

bool IsVsyncEnabled();
bool SetVsync(bool on);

f64 GetFOVTanScale();

PostQuality GetPostQuality();
ReflectionQuality GetReflectionQuality();

QualityPreset GetPreset();
bool SetPreset(QualityPreset preset);

bool SetShadowQuality(f64 distance, i32 dimension);

void WatchSettings();

} // namespace bd::gpu
