/**
 * @file    gpu/settings.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "gpu/settings.h"

#include <charconv>
#include <cmath>
#include <numbers>
#include <string>
#include <string_view>

#include <vector>

#include <rex/cvar.h>

#include "core/logging.h"
#include "core/cvar_table.h"
#include "gpu/output.h"
#include "gpu/pipeline/pso_predictor.h"

namespace bd::gpu {
namespace {

std::vector<rex::cvar::CallbackHandle> g_callbacks;

std::string FormatCvar(f64 v) {
  char buf[32];
  auto [end, ec] = std::to_chars(buf, buf + sizeof(buf), v);
  return ec == std::errc() ? std::string(buf, end) : std::string("0");
}

std::string FormatCvar(i32 v) { return std::to_string(v); }
std::string FormatCvar(bool v) { return v ? "true" : "false"; }

constexpr f64 kShadowDistanceEpsilon = 0.01;

} // namespace

Settings &Settings::Get() {
  static Settings s;
  return s;
}

void Settings::AdoptAnisotropy() { anisotropy_ = REXCVAR_GET(bd::cvars::anisotropy); }
void Settings::AdoptNTSCFilter() { ntscFilter_ = REXCVAR_GET(bd::cvars::ntsc_filter); }
void Settings::AdoptDOFStrength() {
  dofStrength_ = REXCVAR_GET(bd::cvars::dof_strength);
}
void Settings::AdoptShadowDistance() {
  shadowDistance_ = REXCVAR_GET(bd::cvars::shadow_distance);
}
void Settings::AdoptVsync() { vsync_ = REXCVAR_GET(bd::cvars::vsync); }
void Settings::AdoptAspectRatio() {
  aspectRatio_ = REXCVAR_GET(bd::cvars::aspect_ratio);
  Output::Recompute();
}
void Settings::AdoptFOVOffset() { fovOffset_ = REXCVAR_GET(bd::cvars::fov_offset); }
void Settings::AdoptShadowDimension() {
  shadowDimension_ = REXCVAR_GET(bd::cvars::shadow_dimension);
}
void Settings::AdoptPSOPrecache() {
  psoPrecache_ = REXCVAR_GET(bd::cvars::pso_precache);
}
void Settings::AdoptGeometryGPUUpload() {
  geometryGPUUpload_ = REXCVAR_GET(bd::cvars::geometry_gpu_upload);
}
void Settings::AdoptDRED() { dred_ = REXCVAR_GET(bd::cvars::dred); }
void Settings::AdoptSceneColorR11G11B10() {
  sceneColorR11G11B10_ = REXCVAR_GET(bd::cvars::scene_color_r11g11b10);
}
void Settings::AdoptSuperSampling() {
  superSampling_ = REXCVAR_GET(bd::cvars::supersampling);
}
void Settings::AdoptMSAA() {
  const i32 was = msaa_;
  msaa_ = REXCVAR_GET(bd::cvars::msaa);
  if (msaa_ != was && msaa_ != 0)
    ReemitPredictions();
}
void Settings::AdoptPostQuality() {
  postQuality_ = REXCVAR_GET(bd::cvars::post_quality);
}
void Settings::AdoptReflectionQuality() {
  reflectionQuality_ = REXCVAR_GET(bd::cvars::reflection_quality);
}

bool Settings::SetAnisotropy(i32 v) {
  return bd::CvarSet("bd.anisotropy", FormatCvar(v));
}

bool Settings::SetNTSCFilter(bool v) {
  return bd::CvarSet("bd.ntsc_filter", FormatCvar(v));
}

bool Settings::SetDOFStrength(f64 v) {
  return bd::CvarSet("bd.dof_strength", FormatCvar(v));
}

bool Settings::SetShadowDistance(f64 v) {
  return bd::CvarSet("bd.shadow_distance", FormatCvar(v));
}

bool Settings::SetShadowQuality(f64 distance, i32 dimension) {
  const bool dist = SetShadowDistance(distance);
  const bool dim =
      bd::CvarSet("bd.shadow_dimension", FormatCvar(dimension));
  return dist && dim;
}

bool Settings::SetVsync(bool v) {
  return bd::CvarSet("bd.vsync", FormatCvar(v));
}

bool Settings::SetAspectRatio(i32 v) {
  return bd::CvarSet("bd.aspect_ratio", FormatCvar(v));
}

bool Settings::SetFOVOffset(i32 v) {
  return bd::CvarSet("bd.fov_offset", FormatCvar(v));
}

// Both angles halved, so the ratio of their tangents is what scales a camera's
// own half-angle. The authored value short-circuits rather than dividing a
// tangent by itself.
f64 Settings::FOVTanScale() const {
  if (fovOffset_ == 0)
    return 1.0;
  constexpr f64 kHalfDegreesToRadians = std::numbers::pi / 360.0;
  return std::tan((kAuthoredFOVDegrees + fovOffset_) * kHalfDegreesToRadians) /
         std::tan(kAuthoredFOVDegrees * kHalfDegreesToRadians);
}

bool Settings::SetSuperSampling(i32 v) {
  return bd::CvarSet("bd.supersampling", FormatCvar(v));
}

bool Settings::SetMSAA(i32 v) {
  return bd::CvarSet("bd.msaa", FormatCvar(v));
}

bool Settings::SetPostQuality(i32 v) {
  return bd::CvarSet("bd.post_quality", FormatCvar(v));
}

bool Settings::SetReflectionQuality(i32 v) {
  return bd::CvarSet("bd.reflection_quality", FormatCvar(v));
}

gpu::QualityPreset Settings::QualityPreset() const {
  for (u32 i = 0; i < kQualityPresetCount; ++i) {
    const PresetBundle &p = kPresets[i];
    if (superSampling_ == p.superSampling && msaa_ == p.msaa &&
        anisotropy_ == p.anisotropy &&
        std::abs(shadowDistance_ - p.shadowDistance) < kShadowDistanceEpsilon &&
        shadowDimension_ == p.shadowDimension &&
        postQuality_ == static_cast<i32>(p.postQuality) &&
        reflectionQuality_ == static_cast<i32>(p.reflectionQuality)) {
      return static_cast<gpu::QualityPreset>(i);
    }
  }
  return gpu::QualityPreset::Custom;
}

bool Settings::SetQualityPreset(gpu::QualityPreset preset) {
  const u32 i = static_cast<u32>(preset);
  if (i >= kQualityPresetCount)
    return false; // Custom is a state, not a target
  const PresetBundle &p = kPresets[i];
  bool ok = SetSuperSampling(p.superSampling);
  ok = SetMSAA(p.msaa) && ok;
  ok = SetAnisotropy(p.anisotropy) && ok;
  ok = SetShadowDistance(p.shadowDistance) && ok;
  ok = bd::CvarSet("bd.shadow_dimension",
                                FormatCvar(p.shadowDimension)) &&
       ok;
  ok = SetPostQuality(static_cast<i32>(p.postQuality)) && ok;
  ok = SetReflectionQuality(static_cast<i32>(p.reflectionQuality)) && ok;
  REXLOG_DEBUG(bd, "config quality preset = {}", ToString(preset));
  return ok;
}

void Settings::AdoptCvars() {
  AdoptAnisotropy();
  AdoptNTSCFilter();
  AdoptDOFStrength();
  AdoptShadowDistance();
  AdoptVsync();
  AdoptAspectRatio();
  AdoptFOVOffset();
  AdoptShadowDimension();
  AdoptPSOPrecache();
  AdoptGeometryGPUUpload();
  AdoptDRED();
  AdoptSceneColorR11G11B10();
  AdoptSuperSampling();
  AdoptMSAA();
  AdoptPostQuality();
  AdoptReflectionQuality();
}

void Settings::Init() {
  AdoptCvars();

  auto reg = [](const char *name, void (Settings::*adopt)()) {
    bd::CvarWatch(g_callbacks, name,
                  [adopt](std::string_view, std::string_view) {
                    (Settings::Get().*adopt)();
                  });
  };
  reg("bd.anisotropy", &Settings::AdoptAnisotropy);
  reg("bd.ntsc_filter", &Settings::AdoptNTSCFilter);
  reg("bd.dof_strength", &Settings::AdoptDOFStrength);
  reg("bd.shadow_distance", &Settings::AdoptShadowDistance);
  reg("bd.vsync", &Settings::AdoptVsync);
  reg("bd.aspect_ratio", &Settings::AdoptAspectRatio);
  reg("bd.fov_offset", &Settings::AdoptFOVOffset);
  reg("bd.shadow_dimension", &Settings::AdoptShadowDimension);
  reg("bd.pso_precache", &Settings::AdoptPSOPrecache);
  reg("bd.geometry_gpu_upload", &Settings::AdoptGeometryGPUUpload);
  reg("bd.dred", &Settings::AdoptDRED);
  reg("bd.scene_color_r11g11b10", &Settings::AdoptSceneColorR11G11B10);
  reg("bd.supersampling", &Settings::AdoptSuperSampling);
  reg("bd.msaa", &Settings::AdoptMSAA);
  reg("bd.post_quality", &Settings::AdoptPostQuality);
  reg("bd.reflection_quality", &Settings::AdoptReflectionQuality);

  bd::CvarWatch(g_callbacks,
      "ui.resolution", [](std::string_view, std::string_view) {
        Output::Recompute();
      });
}

} // namespace bd::gpu
