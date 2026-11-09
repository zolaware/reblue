/**
 * @file    gpu/settings.cpp
 * @license BSD 3-Clause, see LICENSE
 */
#include "gpu/settings.h"

#include <cmath>
#include <numbers>
#include <string_view>

#include "core/cvar_table.h"
#include "core/logging.h"
#include "gpu/output.h"
#include "gpu/pipeline/pso_predictor.h"

namespace bd::gpu {
namespace {

bd::SettingWatches g_callbacks;

constexpr f64 kShadowDistanceEpsilon = 0.01;

} // namespace

bool IsVsyncEnabled() { return bd::CvarGet("gpu.vsync") != "false"; }

bool SetVsync(bool on) { return bd::CvarSet("gpu.vsync", on ? "true" : "false"); }

f64 GetFOVTanScale() {
  const i32 offset = bd::cvars::Get().fov_offset.Get();
  if (offset == 0)
    return 1.0;
  constexpr f64 kHalfDegreesToRadians = std::numbers::pi / 360.0;
  return std::tan((kAuthoredFOVDegrees + offset) * kHalfDegreesToRadians) /
         std::tan(kAuthoredFOVDegrees * kHalfDegreesToRadians);
}

PostQuality GetPostQuality() {
  return static_cast<PostQuality>(bd::cvars::Get().post_quality.Get());
}

ReflectionQuality GetReflectionQuality() {
  return static_cast<ReflectionQuality>(
      bd::cvars::Get().reflection_quality.Get());
}

QualityPreset GetPreset() {
  auto &c = bd::cvars::Get();
  for (u32 i = 0; i < kQualityPresetCount; ++i) {
    const PresetBundle &p = kPresets[i];
    if (c.supersampling.Get() == p.superSampling && c.msaa.Get() == p.msaa &&
        c.anisotropy.Get() == p.anisotropy &&
        std::abs(c.shadow_distance.Get() - p.shadowDistance) <
            kShadowDistanceEpsilon &&
        c.shadow_dimension.Get() == p.shadowDimension &&
        c.post_quality.Get() == static_cast<i32>(p.postQuality) &&
        c.reflection_quality.Get() == static_cast<i32>(p.reflectionQuality)) {
      return static_cast<QualityPreset>(i);
    }
  }
  return QualityPreset::Custom;
}

bool SetShadowQuality(f64 distance, i32 dimension) {
  auto &c = bd::cvars::Get();
  const bool dist = c.shadow_distance.Set(distance).has_value();
  const bool dim = c.shadow_dimension.Set(dimension).has_value();
  return dist && dim;
}

bool SetPreset(QualityPreset preset) {
  const u32 i = static_cast<u32>(preset);
  if (i >= kQualityPresetCount)
    return false; // Custom is a state, not a target
  const PresetBundle &p = kPresets[i];
  auto &c = bd::cvars::Get();
  bool ok = c.supersampling.Set(p.superSampling).has_value();
  ok = c.msaa.Set(p.msaa).has_value() && ok;
  ok = c.anisotropy.Set(p.anisotropy).has_value() && ok;
  ok = SetShadowQuality(p.shadowDistance, p.shadowDimension) && ok;
  ok = c.post_quality.Set(static_cast<i32>(p.postQuality)).has_value() && ok;
  ok = c.reflection_quality.Set(static_cast<i32>(p.reflectionQuality))
           .has_value() &&
       ok;
  REXBD_DEBUG("config quality preset = {}", ToString(preset));
  return ok;
}

void WatchSettings() {
  auto &c = bd::cvars::Get();
  g_callbacks.push_back(c.aspect_ratio.OnChange([](const i32 &) { Output::Recompute(); }));
  g_callbacks.push_back(c.msaa.OnChange([](const i32 &now) {
    if (now != 0)
      ReemitPredictions();
  }));
  bd::CvarWatch(g_callbacks, "ui.resolution",
                [](std::string_view, std::string_view) { Output::Recompute(); });
}

} // namespace bd::gpu
