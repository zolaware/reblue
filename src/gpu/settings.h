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
  Off = 0,
  Low = 1,
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
  case ReflectionQuality::Off:
    return "opt.off";
  case ReflectionQuality::Low:
    return "opt.preset.low";
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
  i32 renderScale;
  PostQuality postQuality;
  ReflectionQuality reflectionQuality;
};

inline constexpr PresetBundle kPresets[kQualityPresetCount] = {
    /* Low    */ {1, 0, 16, 1.0, 1024, 75, PostQuality::Low,
                  ReflectionQuality::Off},
    /* Medium */ {1, 4, 16, 2.0, 4096, 100, PostQuality::Medium,
                  ReflectionQuality::Low},
    /* High   */ {2, 4, 16, 2.0, 4096, 100, PostQuality::High,
                  ReflectionQuality::High},
    /* Ultra  */ {2, 8, 16, 4.0, 8192, 100, PostQuality::High,
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

  i32 Anisotropy() const { return anisotropy_; }
  bool SetAnisotropy(i32 v);

  bool NTSCFilter() const { return ntscFilter_; }
  bool SetNTSCFilter(bool v);

  // Fraction of BD's own depth-of-field blur to keep, 1 being all of it.
  f64 DOFStrength() const { return dofStrength_; }
  bool SetDOFStrength(f64 v);

  f64 ShadowDistance() const { return shadowDistance_; }
  bool SetShadowDistance(f64 v);

  // Coverage and map dimension are one setting between them: coverage is live
  // and the dimension restart-bound, but a step that moved only one would
  // leave texel density wrong for as long as the coverage change is visible.
  bool SetShadowQuality(f64 distance, i32 dimension);

  bool Vsync() const { return vsync_; }
  bool SetVsync(bool v);

  // The raw cvar value, for the menu row that cycles it.
  // Output::ConfiguredAspect turns it into a ratio.
  i32 AspectRatio() const { return aspectRatio_; }
  bool SetAspectRatio(i32 v);

  // Horizontal degrees added to the game's own framing at 16:9, which the menu
  // counts off 45. An offset rather than an absolute, so 0 is the authored view
  // whatever a scene frames itself at.
  i32 FOVOffset() const { return fovOffset_; }
  bool SetFOVOffset(i32 v);

  // What a camera's half-angle tangent is multiplied by to reach that field of
  // view. Exactly 1 at the default, so the projection is left alone rather than
  // rebuilt and moved by the float residue, and a scene framed tighter or wider
  // than the default keeps its relationship to it.
  f64 FOVTanScale() const;

  // Restart-bound: read once when the device and its targets are built.
  i32 ShadowDimension() const { return shadowDimension_; }
  bool PSOPrecache() const { return psoPrecache_; }
  bool GeometryGPUUpload() const { return geometryGPUUpload_; }
  bool DRED() const { return dred_; }
  bool SceneColorR11G11B10() const { return sceneColorR11G11B10_; }
  i32 SuperSampling() const { return superSampling_; }
  bool SetSuperSampling(i32 v);
  i32 MSAA() const { return msaa_; }
  bool SetMSAA(i32 v);

  i32 RenderScale() const { return renderScale_; }
  bool SetRenderScale(i32 v);

  gpu::PostQuality PostQuality() const {
    return static_cast<gpu::PostQuality>(postQuality_);
  }
  bool SetPostQuality(i32 v);

  gpu::ReflectionQuality ReflectionQuality() const {
    return static_cast<gpu::ReflectionQuality>(reflectionQuality_);
  }
  bool SetReflectionQuality(i32 v);

  // The preset the five quality settings currently match, or Custom.
  gpu::QualityPreset QualityPreset() const;
  bool SetQualityPreset(gpu::QualityPreset preset);

private:
  Settings() = default;
  Settings(const Settings &) = delete;
  Settings &operator=(const Settings &) = delete;

  // Each pulls its own setting from cvar storage and nothing else. AdoptCvars
  // calls every one. Each setting's registered change callback calls only its
  // own, so one setting changing never re-reads the others.
  void AdoptAnisotropy();
  void AdoptNTSCFilter();
  void AdoptDOFStrength();
  void AdoptShadowDistance();
  void AdoptVsync();
  void AdoptAspectRatio();
  void AdoptFOVOffset();
  void AdoptShadowDimension();
  void AdoptPSOPrecache();
  void AdoptGeometryGPUUpload();
  void AdoptDRED();
  void AdoptSceneColorR11G11B10();
  void AdoptSuperSampling();
  void AdoptMSAA();
  void AdoptRenderScale();
  void AdoptPostQuality();
  void AdoptReflectionQuality();

  // Seeded from the same bundle the cvar defaults come from, so a reader that
  // runs before Init, the installer wizard among them, sees the default preset
  // rather than a second set of values.
  i32 anisotropy_ = kDefaultSettings.anisotropy;
  i32 superSampling_ = kDefaultSettings.superSampling;
  i32 msaa_ = kDefaultSettings.msaa;
  i32 renderScale_ = kDefaultSettings.renderScale;
  i32 postQuality_ = static_cast<i32>(kDefaultSettings.postQuality);
  i32 reflectionQuality_ = static_cast<i32>(kDefaultSettings.reflectionQuality);
  bool ntscFilter_ = false;
  f64 dofStrength_ = 1.0;
  i32 shadowDimension_ = kDefaultSettings.shadowDimension;
  f64 shadowDistance_ = kDefaultSettings.shadowDistance;
  i32 aspectRatio_ = static_cast<i32>(AspectMode::Auto);
  i32 fovOffset_ = 0;
  bool vsync_ = true;
  bool psoPrecache_ = true;
  bool geometryGPUUpload_ = true;
  bool dred_ = true;
  bool sceneColorR11G11B10_ = false;
};

} // namespace bd::gpu
